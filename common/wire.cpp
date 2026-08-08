#include "common/wire.h"

#include <chrono>
#include <cstring>

namespace fleetcore {

// ---- byte order ----

uint16_t load_be16(const uint8_t* p) {
    return static_cast<uint16_t>(
        (static_cast<uint16_t>(p[0]) << 8) |
        (static_cast<uint16_t>(p[1])));
}

uint32_t load_be32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) <<  8) |
           (static_cast<uint32_t>(p[3]));
}

uint64_t load_be64(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) {
        v = (v << 8) | static_cast<uint64_t>(p[i]);
    }
    return v;
}

void store_be16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v >> 8);
    p[1] = static_cast<uint8_t>(v);
}

void store_be32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v >> 24);
    p[1] = static_cast<uint8_t>(v >> 16);
    p[2] = static_cast<uint8_t>(v >>  8);
    p[3] = static_cast<uint8_t>(v);
}

void store_be64(uint8_t* p, uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        p[i] = static_cast<uint8_t>(v >> (56 - 8 * i));
    }
}

// ---- error strings ----

const char* to_string(WireError e) {
    switch (e) {
        case WireError::Ok:                 return "ok";
        case WireError::TooShort:           return "shorter than the common header";
        case WireError::BadMagic:           return "magic mismatch";
        case WireError::UnsupportedVersion: return "unsupported protocol version";
        case WireError::BadLength:          return "length field out of range";
        case WireError::LengthMismatch:     return "length does not match the message code";
        case WireError::UnknownCode:        return "unknown message code";
    }
    return "unknown error";
}

// ---- header ----

std::size_t pack_header(const Header& h, uint8_t* buf, std::size_t cap) {
    if (cap < sizeof(Header)) return 0;

    // Single-byte fields and the magic array are already in wire form,
    // so copying the struct wholesale gets them right.
    std::memcpy(buf, &h, sizeof(Header));

    // Then overwrite every multi-byte field with its big-endian form.
    // offsetof is used directly here: the assertions in proto.h are what
    // make these positions trustworthy.
    store_be16(buf + offsetof(Header, length),    h.length);
    store_be16(buf + offsetof(Header, seq),       h.seq);
    store_be64(buf + offsetof(Header, timestamp), h.timestamp);
    store_be32(buf + offsetof(Header, reserved),  h.reserved);

    return sizeof(Header);
}

WireError unpack_header(const uint8_t* buf, std::size_t len, Header& out) {
    if (len < sizeof(Header)) return WireError::TooShort;

    // Note: the buffer is NOT cast to a Header*. The buffer may be
    // unaligned, and reading a uint64_t through a misaligned pointer is
    // undefined behaviour (and a hard fault on some architectures).
    // memcpy is the portable way; compilers optimise it away.
    std::memcpy(&out, buf, sizeof(Header));

    out.length    = load_be16(buf + offsetof(Header, length));
    out.seq       = load_be16(buf + offsetof(Header, seq));
    out.timestamp = load_be64(buf + offsetof(Header, timestamp));
    out.reserved  = load_be32(buf + offsetof(Header, reserved));

    if (out.magic[0] != MAGIC0 || out.magic[1] != MAGIC1) {
        return WireError::BadMagic;
    }
    if (out.version > PROTO_VERSION) {
        return WireError::UnsupportedVersion;
    }
    if (out.length < sizeof(Header) || out.length > MAX_MESSAGE_LEN) {
        return WireError::BadLength;
    }
    return WireError::Ok;
}

// ---- LockRequest ----

std::size_t pack_lock_request(const LockRequest& m, uint8_t* buf, std::size_t cap) {
    if (cap < sizeof(LockRequest)) return 0;

    std::memcpy(buf, &m, sizeof(LockRequest));
    // Only the header carries multi-byte fields; term_no is a byte array.
    store_be16(buf + offsetof(Header, length),    m.header.length);
    store_be16(buf + offsetof(Header, seq),       m.header.seq);
    store_be64(buf + offsetof(Header, timestamp), m.header.timestamp);
    store_be32(buf + offsetof(Header, reserved),  m.header.reserved);

    return sizeof(LockRequest);
}

WireError unpack_lock_request(const uint8_t* buf, std::size_t len, LockRequest& out) {
    Header h{};
    const WireError e = unpack_header(buf, len, h);
    if (e != WireError::Ok) return e;

    if (h.code != CODE_LOCK_REQUEST) return WireError::UnknownCode;
    if (h.length != sizeof(LockRequest)) return WireError::LengthMismatch;
    if (len < sizeof(LockRequest)) return WireError::TooShort;

    std::memcpy(&out, buf, sizeof(LockRequest));
    out.header = h;   // already byte-swapped
    return WireError::Ok;
}

LockRequest make_lock_request(const char term_no[4], uint16_t seq) {
    using namespace std::chrono;
    const auto now = duration_cast<milliseconds>(
        system_clock::now().time_since_epoch()).count();

    LockRequest m{};
    m.header.magic[0]  = MAGIC0;
    m.header.magic[1]  = MAGIC1;
    m.header.version   = PROTO_VERSION;
    m.header.code      = CODE_LOCK_REQUEST;
    m.header.length    = sizeof(LockRequest);
    m.header.seq       = seq;
    m.header.timestamp = static_cast<uint64_t>(now);
    m.header.reserved  = 0;
    std::memcpy(m.term_no, term_no, sizeof(m.term_no));
    return m;
}

}  // namespace fleetcore