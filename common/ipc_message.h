#pragma once

#include "common/proto.h"

#include <cstdint>

namespace fleetcore {

// ============================================================
// Internal message format, used only between our own processes.
//
// This is deliberately NOT the wire format. The wire format is a contract
// with the outside world and must not change casually; this one is ours
// alone and may change whenever the internal design does.
//
// The payload is already in host byte order: gateway does the decoding
// once, so nothing downstream needs to know about endianness.
// ============================================================

// Process index base values (see docs/ARCHITECTURE.md section 2).
inline constexpr int32_t IDX_GATEWAY   = 1000;
inline constexpr int32_t IDX_LOCATOR   = 2000;
inline constexpr int32_t IDX_TERMD_BASE = 3000;

// Internal message codes. Separate space from the wire codes in proto.h:
// the two evolve independently, and a wire code is not meaningful inside.
inline constexpr uint8_t IPC_LOCK_REQUEST = 0x01;

struct IpcHeader {
    uint8_t code;         // one of IPC_*
    int32_t dest_index;   // 0 until locator resolves it
};

struct IpcLockRequest {
    IpcHeader   ipc;
    LockRequest msg;      // host byte order
};

}  // namespace fleetcore