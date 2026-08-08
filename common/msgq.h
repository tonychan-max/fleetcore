#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

namespace fleetcore {

// ============================================================
// Thin wrapper over System V message queues.
//
// This is the only file that knows msgget/msgsnd/msgrcv exist. Swapping
// the transport later (pipes, sockets, shared memory) should not require
// touching any business process.
//
// Queue keys follow msgq_key_base + process index, so that `ipcs -q`
// shows which queue belongs to which process (see docs/CONFIG.md).
// ============================================================

inline constexpr int32_t MSGQ_KEY_BASE = 0x46431000;

// Default message type. System V requires mtype >= 1; a single type is
// enough while each process owns its own queue.
inline constexpr long MSGQ_TYPE_DEFAULT = 1;

class MsgQueue {
public:
    MsgQueue() = default;
    ~MsgQueue();

    // Non-copyable: two owners of one descriptor invites double removal.
    MsgQueue(const MsgQueue&) = delete;
    MsgQueue& operator=(const MsgQueue&) = delete;

    MsgQueue(MsgQueue&& other) noexcept;
    MsgQueue& operator=(MsgQueue&& other) noexcept;

    // create = true  : this process owns the queue and removes it on exit
    // create = false : attach to a queue somebody else created
    bool open(int32_t key, bool create);

    bool send(const void* data, std::size_t len);

    // Blocks until a message arrives. Returns bytes received, or -1 on error.
    long recv(void* out, std::size_t cap);

    bool is_open() const { return id_ >= 0; }

private:
    int  id_    = -1;
    bool owner_ = false;
};

}  // namespace fleetcore