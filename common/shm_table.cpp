#include "common/shm_table.h"

#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/types.h>

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <unistd.h>

namespace fleetcore {

namespace {

// System V semaphores do not come with a value, and semget does not let you
// set one atomically. Two processes starting together can therefore both
// create the set and both try to initialise it, with the second overwriting
// the first mid-operation.
//
// The standard way out: whoever creates it initialises it, everyone else
// waits for sem_otime to become non-zero -- the kernel sets that on the
// first semop, so it is the signal that initialisation finished.
constexpr int INIT_RETRIES   = 100;
constexpr int INIT_SLEEP_US  = 10000;   // 10 ms

uint64_t now_ms() {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
}

}  // namespace

ShmTable::~ShmTable() {
    // Detach only. Removing the segment is not this object's call: other
    // processes may still be attached and there is no way to know. Step 7
    // handles removal along with the message queues.
    if (rows_ != nullptr) {
        shmdt(rows_);
        rows_ = nullptr;
    }
}

bool ShmTable::open() {
    const std::size_t bytes = sizeof(TermEntry) * MAX_TERMINALS;

    // ---- shared memory ----
    // 0600: owner only. These never cross user accounts.
    shm_id_ = shmget(static_cast<key_t>(SHM_KEY), bytes, IPC_CREAT | 0600);
    if (shm_id_ < 0) {
        std::fprintf(stderr, "shmget(0x%X) failed: %s\n",
                     SHM_KEY, std::strerror(errno));
        return false;
    }

    void* addr = shmat(shm_id_, nullptr, 0);
    if (addr == reinterpret_cast<void*>(-1)) {
        std::fprintf(stderr, "shmat failed: %s\n", std::strerror(errno));
        return false;
    }
    rows_ = static_cast<TermEntry*>(addr);

    // A freshly created segment is zero-filled by the kernel, which means
    // every row has term_no == 0 and state == TERM_STATE_UNUSED. No
    // explicit initialisation is needed, and that is not an accident -- it
    // is why 0 was chosen to mean "free".

    // ---- semaphore ----
    bool created = false;

    sem_id_ = semget(static_cast<key_t>(SEM_KEY), 1, IPC_CREAT | IPC_EXCL | 0600);
    if (sem_id_ >= 0) {
        created = true;
    } else if (errno == EEXIST) {
        sem_id_ = semget(static_cast<key_t>(SEM_KEY), 1, 0600);
    }
    if (sem_id_ < 0) {
        std::fprintf(stderr, "semget(0x%X) failed: %s\n",
                     SEM_KEY, std::strerror(errno));
        return false;
    }

    if (created) {
        // Initialise with semop rather than SETVAL. SETVAL sets the value
        // but leaves sem_otime at zero, and sem_otime is what the other
        // processes wait on -- so with SETVAL they would sit out the whole
        // retry loop until somebody happened to lock for the first time.
        //
        // A new set starts at 0 on Linux, so +1 leaves it at 1 (unlocked).
        //
        // No SEM_UNDO here, deliberately. With it, the kernel would reverse
        // this +1 when the creator exits, dropping the value back to 0 and
        // deadlocking everyone else. This operation is not a lock being
        // taken; it is the initial state being set, and it must outlive the
        // process that set it.
        struct sembuf op{};
        op.sem_num = 0;
        op.sem_op  = 1;
        op.sem_flg = 0;

        if (semop(sem_id_, &op, 1) < 0) {
            std::fprintf(stderr, "semop init failed: %s\n", std::strerror(errno));
            return false;
        }
    } else {
        // Somebody else made it. It may not be initialised yet, so wait for
        // the kernel to report a first operation on it.
        bool ready = false;
        for (int i = 0; i < INIT_RETRIES; ++i) {
            struct semid_ds ds{};
            union semun {
                int             val;
                struct semid_ds* buf;
                unsigned short* array;
            } arg{};
            arg.buf = &ds;

            if (semctl(sem_id_, 0, IPC_STAT, arg) < 0) {
                std::fprintf(stderr, "semctl IPC_STAT failed: %s\n",
                             std::strerror(errno));
                return false;
            }
            if (ds.sem_otime != 0) {
                ready = true;
                break;
            }

            usleep(INIT_SLEEP_US);
        }
        if (!ready) {
            // The creator died between semget and its first semop, or is
            // stuck. Carrying on would mean locking a semaphore stuck at 0.
            std::fprintf(stderr, "semaphore 0x%X never initialised\n", SEM_KEY);
            return false;
        }
    }

    return true;
}

bool ShmTable::lock() const {
    if (sem_id_ < 0) return false;

    struct sembuf op{};
    op.sem_num = 0;
    op.sem_op  = -1;
    // SEM_UNDO: if this process is killed while holding the lock, the
    // kernel gives it back. Without it, one kill -9 deadlocks every other
    // process permanently -- a far worse failure than the half-written row
    // this can leave behind. The rows are small and the critical sections
    // are a few instructions, so that window is narrow.
    op.sem_flg = SEM_UNDO;

    while (semop(sem_id_, &op, 1) < 0) {
        if (errno == EINTR) continue;   // a signal, not a failure
        std::fprintf(stderr, "semop lock failed: %s\n", std::strerror(errno));
        return false;
    }
    return true;
}

bool ShmTable::unlock() const {
    if (sem_id_ < 0) return false;

    struct sembuf op{};
    op.sem_num = 0;
    op.sem_op  = 1;
    op.sem_flg = SEM_UNDO;

    while (semop(sem_id_, &op, 1) < 0) {
        if (errno == EINTR) continue;
        std::fprintf(stderr, "semop unlock failed: %s\n", std::strerror(errno));
        return false;
    }
    return true;
}

bool ShmTable::register_terminal(uint32_t term_no) {
    if (!is_open() || term_no == 0) return false;
    if (!lock()) return false;

    bool ok = false;

    // Already registered? Re-registering is normal: termd restarts.
    for (uint32_t i = 0; i < MAX_TERMINALS; ++i) {
        if (rows_[i].term_no == term_no) {
            ok = true;
            break;
        }
    }

    if (!ok) {
        for (uint32_t i = 0; i < MAX_TERMINALS; ++i) {
            if (rows_[i].term_no == 0) {
                TermEntry e{};
                e.term_no      = term_no;
                e.capabilities = 0;
                e.state        = TERM_STATE_DISCONNECTED;
                e.last_seen    = 0;
                rows_[i] = e;
                ok = true;
                break;
            }
        }
    }

    unlock();
    return ok;
}

bool ShmTable::set_state(uint32_t term_no, uint8_t state, uint32_t capabilities) {
    if (!is_open()) return false;
    if (!lock()) return false;

    bool ok = false;
    for (uint32_t i = 0; i < MAX_TERMINALS; ++i) {
        if (rows_[i].term_no == term_no) {
            rows_[i].state        = state;
            rows_[i].capabilities = capabilities;
            rows_[i].last_seen    = now_ms();
            ok = true;
            break;
        }
    }

    unlock();
    return ok;
}

bool ShmTable::touch(uint32_t term_no) {
    if (!is_open()) return false;
    if (!lock()) return false;

    bool ok = false;
    for (uint32_t i = 0; i < MAX_TERMINALS; ++i) {
        if (rows_[i].term_no == term_no) {
            rows_[i].last_seen = now_ms();
            ok = true;
            break;
        }
    }

    unlock();
    return ok;
}

bool ShmTable::find(uint32_t term_no, TermEntry& out) const {
    if (!is_open()) return false;
    if (!lock()) return false;

    bool ok = false;
    for (uint32_t i = 0; i < MAX_TERMINALS; ++i) {
        if (rows_[i].term_no == term_no) {
            // Copied while holding the lock. Returning a pointer instead
            // would hand the caller a row that another process may be
            // writing to.
            out = rows_[i];
            ok = true;
            break;
        }
    }

    unlock();
    return ok;
}

std::size_t ShmTable::snapshot(TermEntry* out, std::size_t cap) const {
    if (!is_open() || out == nullptr) return 0;
    if (!lock()) return 0;

    std::size_t n = 0;
    for (uint32_t i = 0; i < MAX_TERMINALS && n < cap; ++i) {
        if (rows_[i].term_no != 0) out[n++] = rows_[i];
    }

    unlock();
    return n;
}

}  // namespace fleetcore