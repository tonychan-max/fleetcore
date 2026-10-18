// Takes the terminal-table lock and holds it until killed.
//
//   ./build/hold_lock              # SEM_UNDO, same as ShmTable::lock()
//   ./build/hold_lock --no-undo    # without it, for the control run
//
// Exists to test what happens when a process dies holding the lock. The
// real holders (termd, locator) keep it for microseconds, so killing one
// of them almost never catches it mid-section and proves nothing either
// way. This holds it indefinitely, so kill -9 always lands inside.
//
// Expected:
//   with SEM_UNDO   semval 0 while held, back to 1 after kill -9
//   --no-undo       semval stays 0 after kill -9; every other process
//                   blocks on its next lock. Recover with ipcrm -s and a
//                   restart of everything attached.

#include "common/shm_table.h"

#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/types.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <unistd.h>

using namespace fleetcore;

int main(int argc, char** argv) {
    bool undo = true;
    if (argc >= 2) {
        if (std::strcmp(argv[1], "--no-undo") != 0) {
            std::fprintf(stderr, "usage: %s [--no-undo]\n", argv[0]);
            return 1;
        }
        undo = false;
    }

    // Attach only. Creating the set here would leave it at 0 with nobody
    // to initialise it -- start termd first.
    const int sem_id = semget(static_cast<key_t>(SEM_KEY), 1, 0600);
    if (sem_id < 0) {
        std::fprintf(stderr, "semget(0x%X) failed: %s (is termd running?)\n",
                     SEM_KEY, std::strerror(errno));
        return 1;
    }

    struct sembuf op{};
    op.sem_num = 0;
    op.sem_op  = -1;
    op.sem_flg = undo ? SEM_UNDO : 0;

    std::printf("pid %d waiting for lock (SEM_UNDO=%s)\n",
                getpid(), undo ? "on" : "off");
    std::fflush(stdout);

    while (semop(sem_id, &op, 1) < 0) {
        if (errno == EINTR) continue;
        std::fprintf(stderr, "semop failed: %s\n", std::strerror(errno));
        return 1;
    }

    std::printf("holding lock -- kill -9 %d to test\n", getpid());
    std::fflush(stdout);

    // Never releases. Only a signal ends this, which is the point.
    for (;;) pause();
}
