#include "common/wire.h"

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

using namespace fleetcore;

// ---- byte order ----
//
// The wire is big-endian (ARCHITECTURE 4-3). These are the only place where
// that is decided, so they are worth pinning down.

TEST(Wire, Store16WritesHighByteFirst) {
    uint8_t buf[2] = {};
    store_be16(buf, 0x0018);           // 24, the size of a LockRequest

    EXPECT_EQ(buf[0], 0x00);
    EXPECT_EQ(buf[1], 0x18);
}

TEST(Wire, Store64WritesHighByteFirst) {
    uint8_t buf[8] = {};
    store_be64(buf, 0x0123456789ABCDEFull);

    const uint8_t expected[8] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};
    EXPECT_EQ(std::memcmp(buf, expected, sizeof(buf)), 0);
}

TEST(Wire, LoadIsTheInverseOfStore) {
    uint8_t buf[8] = {};

    store_be16(buf, 0xBEEF);
    EXPECT_EQ(load_be16(buf), 0xBEEF);

    store_be32(buf, 0xDEADBEEF);
    EXPECT_EQ(load_be32(buf), 0xDEADBEEFu);

    store_be64(buf, 0xFEEDFACECAFEBEEFull);
    EXPECT_EQ(load_be64(buf), 0xFEEDFACECAFEBEEFull);
}

// ---- round trip ----

TEST(Wire, LockRequestSurvivesPackAndUnpack) {
    const LockRequest sent = make_lock_request("1001", 42);

    uint8_t buf[MAX_MESSAGE_LEN];
    const std::size_t n = pack_lock_request(sent, buf, sizeof(buf));
    ASSERT_EQ(n, sizeof(LockRequest));

    LockRequest got{};
    ASSERT_EQ(unpack_lock_request(buf, n, got), WireError::Ok);

    EXPECT_EQ(got.header.seq, 42);
    EXPECT_EQ(got.header.code, CODE_LOCK_REQUEST);
    EXPECT_EQ(got.header.length, sizeof(LockRequest));
    EXPECT_EQ(got.header.timestamp, sent.header.timestamp);
    EXPECT_EQ(std::memcmp(got.term_no, "1001", 4), 0);
}

TEST(Wire, PackedLockRequestMatchesTheContract) {
    const LockRequest req = make_lock_request("1001", 1);

    uint8_t buf[MAX_MESSAGE_LEN];
    ASSERT_GT(pack_lock_request(req, buf, sizeof(buf)), 0u);

    // Byte for byte against docs/ARCHITECTURE.md section 4-1. A change here
    // is a change to the contract, and every peer has to agree to it.
    EXPECT_EQ(buf[0], 'F');
    EXPECT_EQ(buf[1], 'C');
    EXPECT_EQ(buf[2], PROTO_VERSION);
    EXPECT_EQ(buf[3], CODE_LOCK_REQUEST);
    EXPECT_EQ(buf[4], 0x00);                      // length, high byte
    EXPECT_EQ(buf[5], 0x18);                      // length, low byte: 24
    EXPECT_EQ(std::memcmp(buf + 20, "1001", 4), 0);
}

TEST(Wire, PackFailsRatherThanOverrunASmallBuffer) {
    const LockRequest req = make_lock_request("1001", 1);

    uint8_t small[8];
    EXPECT_EQ(pack_lock_request(req, small, sizeof(small)), 0u);
}

// ---- rejection paths ----
//
// Every one of these is a reason to drop something that arrived from
// outside. Bytes off the wire are never trusted, and each refusal has to
// say which rule was broken -- "it failed" is not enough to decide whether
// to skip one message or drop the connection.

namespace {

std::vector<uint8_t> valid_frame() {
    const LockRequest req = make_lock_request("1001", 1);
    std::vector<uint8_t> out(sizeof(LockRequest));
    pack_lock_request(req, out.data(), out.size());
    return out;
}

}  // namespace

TEST(Wire, ShortBufferIsTooShort) {
    const auto frame = valid_frame();
    Header h{};

    EXPECT_EQ(unpack_header(frame.data(), sizeof(Header) - 1, h),
              WireError::TooShort);
}

TEST(Wire, WrongMagicIsRejected) {
    auto frame = valid_frame();
    frame[0] = 'X';

    Header h{};
    EXPECT_EQ(unpack_header(frame.data(), frame.size(), h),
              WireError::BadMagic);
}

TEST(Wire, NewerVersionIsRejected) {
    auto frame = valid_frame();
    frame[2] = PROTO_VERSION + 1;

    Header h{};
    EXPECT_EQ(unpack_header(frame.data(), frame.size(), h),
              WireError::UnsupportedVersion);
}

// A length below the header size would underflow the "how much is left to
// read" calculation in gateway and termd. Catching it here is what makes
// that subtraction safe there.
TEST(Wire, LengthBelowHeaderSizeIsRejected) {
    auto frame = valid_frame();
    store_be16(frame.data() + offsetof(Header, length), 4);

    Header h{};
    EXPECT_EQ(unpack_header(frame.data(), frame.size(), h),
              WireError::BadLength);
}

// An absurd length is a request to wait for bytes that will never come, or
// to allocate memory on a peer's say-so.
TEST(Wire, LengthAboveTheMaximumIsRejected) {
    auto frame = valid_frame();
    store_be16(frame.data() + offsetof(Header, length), MAX_MESSAGE_LEN + 1);

    Header h{};
    EXPECT_EQ(unpack_header(frame.data(), frame.size(), h),
              WireError::BadLength);
}

TEST(Wire, WrongCodeIsNotALockRequest) {
    auto frame = valid_frame();
    frame[3] = CODE_LOCK_RESPONSE;

    LockRequest req{};
    EXPECT_EQ(unpack_lock_request(frame.data(), frame.size(), req),
              WireError::UnknownCode);
}

TEST(Wire, LengthDisagreeingWithTheCodeIsRejected) {
    auto frame = valid_frame();
    store_be16(frame.data() + offsetof(Header, length), sizeof(Header));

    LockRequest req{};
    EXPECT_EQ(unpack_lock_request(frame.data(), frame.size(), req),
              WireError::LengthMismatch);
}

// ---- LockResponse ----

TEST(Wire, LockResponseSurvivesPackAndUnpack) {
    const LockResponse sent = make_lock_response("1002", 7, RESULT_OK);

    uint8_t buf[MAX_MESSAGE_LEN];
    const std::size_t n = pack_lock_response(sent, buf, sizeof(buf));
    ASSERT_EQ(n, sizeof(LockResponse));

    LockResponse got{};
    ASSERT_EQ(unpack_lock_response(buf, n, got), WireError::Ok);

    EXPECT_EQ(got.header.seq, 7);
    EXPECT_EQ(got.result, RESULT_OK);
    EXPECT_EQ(std::memcmp(got.term_no, "1002", 4), 0);
}

// The reserved bytes must be zero on the wire: a future version may give
// them meaning, and a peer that sees garbage there cannot tell the
// difference between old and broken.
TEST(Wire, LockResponseReservedBytesAreZero) {
    const LockResponse rsp = make_lock_response("1001", 1, RESULT_OK);

    uint8_t buf[MAX_MESSAGE_LEN];
    ASSERT_GT(pack_lock_response(rsp, buf, sizeof(buf)), 0u);

    EXPECT_EQ(buf[25], 0);
    EXPECT_EQ(buf[26], 0);
    EXPECT_EQ(buf[27], 0);
}