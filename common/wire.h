#pragma once

#include "common/proto.h"

#include <cstddef>
#include <cstdint>

namespace fleetcore {

// ============================================================
// Byte order helpers
//
// htons/htonl are deliberately not used: they live in <arpa/inet.h>,
// which does not exist on Windows (ADR-0001 requires one source for both),
// and there is no 64-bit variant for Header::timestamp.
//
// These shift-based versions have no platform dependency at all, and the
// code itself states which byte order is meant.
// ============================================================

uint16_t load_be16(const uint8_t* p);
uint32_t load_be32(const uint8_t* p);
uint64_t load_be64(const uint8_t* p);

void store_be16(uint8_t* p, uint16_t v);
void store_be32(uint8_t* p, uint32_t v);
void store_be64(uint8_t* p, uint64_t v);

// ============================================================
// Decode results
//
// Every value here is a reason to drop a message that arrived from
// outside. Bytes off the wire are never trusted.
// ============================================================
enum class WireError {
    Ok,
    TooShort,             // fewer bytes than a header
    BadMagic,             // not our protocol, or the stream lost sync
    UnsupportedVersion,   // newer than we know how to read
    BadLength,            // length field below header size or above MAX_MESSAGE_LEN
    LengthMismatch,       // length field disagrees with the message code
    UnknownCode,          // code outside every defined range
};

const char* to_string(WireError e);

// ============================================================
// Header
//
// pack/unpack copy the whole struct and then fix up the multi-byte fields.
// This assumes the struct layout is identical to the wire layout, which is
// exactly what the static_assert block in proto.h guarantees (ADR-0003).
//
// Returns the number of bytes written, or 0 if the buffer is too small.
// ============================================================
std::size_t pack_header(const Header& h, uint8_t* buf, std::size_t cap);
WireError   unpack_header(const uint8_t* buf, std::size_t len, Header& out);

// ============================================================
// LockRequest
// ============================================================
std::size_t pack_lock_request(const LockRequest& m, uint8_t* buf, std::size_t cap);
WireError   unpack_lock_request(const uint8_t* buf, std::size_t len, LockRequest& out);

// ============================================================
// LockResponse (uplink)
// ============================================================
std::size_t pack_lock_response(const LockResponse& m, uint8_t* buf, std::size_t cap);
WireError   unpack_lock_response(const uint8_t* buf, std::size_t len, LockResponse& out);

LockResponse make_lock_response(const char term_no[4], uint16_t seq, uint8_t result);

// Fills in magic, version, code, length and timestamp. seq is caller-supplied.
LockRequest make_lock_request(const char term_no[4], uint16_t seq);

}  // namespace fleetcore