#pragma once

#include <cstddef>
#include <cstdint>

namespace fleetcore {

// ============================================================
// Protocol identity
// ============================================================
inline constexpr uint8_t MAGIC0        = 'F';
inline constexpr uint8_t MAGIC1        = 'C';
inline constexpr uint8_t PROTO_VERSION = 1;
// Upper bound for a single message. A length field larger than this is
// treated as garbage rather than as a huge allocation request -- the peer
// may be malicious or simply out of sync.
inline constexpr uint16_t MAX_MESSAGE_LEN = 1024;

// ============================================================
// Message code space (see docs/ARCHITECTURE.md section 4-2)
//
// Only the range boundaries are fixed now; the ranges may stay empty.
// Re-partitioning them later would break compatibility with terminals
// already in the field, so this is one of the few things that must be
// decided up front.
// ============================================================
inline constexpr uint8_t CODE_CONN_BEGIN   = 0x01;  // connection management
inline constexpr uint8_t CODE_CONN_END     = 0x0F;
inline constexpr uint8_t CODE_CTRL_BEGIN   = 0x10;  // control commands (downlink, never dropped)
inline constexpr uint8_t CODE_CTRL_END     = 0x1F;
inline constexpr uint8_t CODE_STATUS_BEGIN = 0x20;  // status reports (uplink, droppable)
inline constexpr uint8_t CODE_STATUS_END   = 0x2F;
inline constexpr uint8_t CODE_POS_BEGIN    = 0x30;  // position and movement
inline constexpr uint8_t CODE_POS_END      = 0x3F;
inline constexpr uint8_t CODE_NAV_BEGIN    = 0x40;  // navigation
inline constexpr uint8_t CODE_NAV_END      = 0x4F;
inline constexpr uint8_t CODE_FILE_BEGIN   = 0x50;  // file transfer (claim-check)
inline constexpr uint8_t CODE_FILE_END     = 0x5F;
inline constexpr uint8_t CODE_TEST_BEGIN   = 0xF0;  // testing and fault injection
inline constexpr uint8_t CODE_TEST_END     = 0xFF;

// ============================================================
// Individual message codes
// Step 1 defines LockRequest only. Others are added when needed.
// ============================================================
inline constexpr uint8_t CODE_LOCK_REQUEST = 0x10;

static_assert(CODE_LOCK_REQUEST >= CODE_CTRL_BEGIN &&
              CODE_LOCK_REQUEST <= CODE_CTRL_END,
              "LockRequest must live in the control command range");

// ============================================================
// Wire structures
// ============================================================
#pragma pack(push, 1)

struct Header {
    uint8_t  magic[2];   // 'F','C' - framing and wrong-peer detection
    uint8_t  version;    // protocol version
    uint8_t  code;       // message type
    uint16_t length;     // total length including header; lets a peer skip unknown messages
    uint16_t seq;        // sequence number; retransmission and duplicate detection
    uint64_t timestamp;  // event time in milliseconds
    uint32_t reserved;   // reserved for future use (must be 0)
};

struct LockRequest {
    Header header;
    char   term_no[4];
};

#pragma pack(pop)

// ============================================================
// Layout verification (ADR-0003)
//
static_assert(sizeof(Header) == 20, "Header size");
static_assert(offsetof(Header, length) == 4, "offset of length");
static_assert(offsetof(Header, timestamp) == 8, "offset of timestamp");

static_assert(sizeof(LockRequest) == 24, "LockRequest total size");
static_assert(offsetof(LockRequest, term_no) == 20,
              "term_no must start immediately after the common header");

}  // namespace fleetcore