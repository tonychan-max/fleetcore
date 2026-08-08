#include "common/msgq.h"

#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/types.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <utility>

namespace fleetcore {

namespace {

// System V wants a struct whose first member is a long mtype.
// The payload size is bounded by the protocol's own maximum.
constexpr std::size_t MSGQ_MAX_PAYLOAD = 1024;

struct RawMessage {
    long mtype;
    char mtext[MSGQ_MAX_PAYLOAD];
};

}  // namespace

MsgQueue::~MsgQueue() {
    // Only the creator removes the queue. An attaching process leaving
    // must not destroy a queue others still use.
    if (id_ >= 0 && owner_) {
        msgctl(id_, IPC_RMID, nullptr);
    }
}

MsgQueue::MsgQueue(MsgQueue&& other) noexcept
    : id_(other.id_), owner_(other.owner_) {
    other.id_    = -1;
    other.owner_ = false;
}

MsgQueue& MsgQueue::operator=(MsgQueue&& other) noexcept {
    if (this != &other) {
        if (id_ >= 0 && owner_) msgctl(id_, IPC_RMID, nullptr);
        id_          = other.id_;
        owner_       = other.owner_;
        other.id_    = -1;
        other.owner_ = false;
    }
    return *this;
}

bool MsgQueue::open(int32_t key, bool create) {
    // 0600: owner read/write only. These queues never cross user accounts.
    const int flags = create ? (IPC_CREAT | 0600) : 0600;

    id_ = msgget(static_cast<key_t>(key), flags);
    if (id_ < 0) {
        std::fprintf(stderr, "msgget(key=0x%X, create=%d) failed: %s\n",
                     key, create ? 1 : 0, std::strerror(errno));
        return false;
    }
    owner_ = create;
    return true;
}

bool MsgQueue::send(const void* data, std::size_t len) {
    if (id_ < 0 || len > MSGQ_MAX_PAYLOAD) return false;

    RawMessage m{};
    m.mtype = MSGQ_TYPE_DEFAULT;
    std::memcpy(m.mtext, data, len);

    if (msgsnd(id_, &m, len, 0) < 0) {
        std::fprintf(stderr, "msgsnd failed: %s\n", std::strerror(errno));
        return false;
    }
    return true;
}

long MsgQueue::recv(void* out, std::size_t cap) {
    if (id_ < 0) return -1;

    RawMessage m{};
    const ssize_t n = msgrcv(id_, &m, sizeof(m.mtext), 0, 0);
    if (n < 0) {
        std::fprintf(stderr, "msgrcv failed: %s\n", std::strerror(errno));
        return -1;
    }
    if (static_cast<std::size_t>(n) > cap) return -1;

    std::memcpy(out, m.mtext, static_cast<std::size_t>(n));
    return n;
}

}  // namespace fleetcore