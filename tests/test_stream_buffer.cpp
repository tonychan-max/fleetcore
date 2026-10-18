#include "common/stream_buffer.h"

#include "common/wire.h"

#include <gtest/gtest.h>

#include <vector>

using namespace fleetcore;

namespace {

// Builds a real LockRequest frame, the same bytes msggen would produce.
std::vector<uint8_t> make_frame(const char* term_no, uint16_t seq) {
    const LockRequest req = make_lock_request(term_no, seq);
    std::vector<uint8_t> out(sizeof(LockRequest));
    const std::size_t n = pack_lock_request(req, out.data(), out.size());
    EXPECT_EQ(n, sizeof(LockRequest));
    return out;
}

// Drains the buffer and returns the sequence number of every complete
// message it yielded.
std::vector<uint16_t> drain(StreamBuffer& buf) {
    std::vector<uint16_t> seqs;
    const uint8_t* msg = nullptr;
    std::size_t    len = 0;

    while (buf.next(&msg, &len)) {
        LockRequest req{};
        if (unpack_lock_request(msg, len, req) == WireError::Ok) {
            seqs.push_back(req.header.seq);
        }
        buf.consume(len);
    }
    return seqs;
}

}  // namespace

TEST(StreamBuffer, EmptyBufferYieldsNothing) {
    StreamBuffer buf;
    const uint8_t* msg = nullptr;
    std::size_t    len = 0;

    EXPECT_FALSE(buf.next(&msg, &len));
}

// TCP may deliver a message in pieces. Treating whatever one recv() returned
// as a whole message is the classic mistake when moving from message queues
// to a byte stream.
TEST(StreamBuffer, PartialFrameYieldsNothingUntilComplete) {
    const auto frame = make_frame("1001", 7);
    StreamBuffer buf;

    // Header is 20 bytes; feed less than that.
    buf.append(frame.data(), 10);
    EXPECT_TRUE(drain(buf).empty());

    // Enough for the header but not the body.
    buf.append(frame.data() + 10, 10);
    EXPECT_TRUE(drain(buf).empty());

    // The last four bytes complete it.
    buf.append(frame.data() + 20, frame.size() - 20);
    EXPECT_EQ(drain(buf), std::vector<uint16_t>{7});
}

// The opposite case: more than one message arrives at once.
TEST(StreamBuffer, SeveralFramesInOneChunkAllYield) {
    const auto a = make_frame("1001", 1);
    const auto b = make_frame("1002", 2);
    const auto c = make_frame("1003", 3);

    std::vector<uint8_t> chunk;
    chunk.insert(chunk.end(), a.begin(), a.end());
    chunk.insert(chunk.end(), b.begin(), b.end());
    chunk.insert(chunk.end(), c.begin(), c.end());

    StreamBuffer buf;
    buf.append(chunk.data(), chunk.size());

    const std::vector<uint16_t> expected{1, 2, 3};
    EXPECT_EQ(drain(buf), expected);
}

// One and a half: the leftover must survive until the rest arrives.
TEST(StreamBuffer, TrailingPartialFrameIsKeptForNextAppend) {
    const auto a = make_frame("1001", 11);
    const auto b = make_frame("1002", 12);

    std::vector<uint8_t> chunk;
    chunk.insert(chunk.end(), a.begin(), a.end());
    chunk.insert(chunk.end(), b.begin(), b.begin() + 6);   // half of b

    StreamBuffer buf;
    buf.append(chunk.data(), chunk.size());

    const std::vector<uint16_t> first{11};
    EXPECT_EQ(drain(buf), first);

    buf.append(b.data() + 6, b.size() - 6);

    const std::vector<uint16_t> second{12};
    EXPECT_EQ(drain(buf), second);
}

// A byte-at-a-time stream is the extreme case of fragmentation. If this
// works, no realistic split can break it.
TEST(StreamBuffer, ByteAtATimeStillYieldsTheFrame) {
    const auto frame = make_frame("1001", 99);
    StreamBuffer buf;

    for (std::size_t i = 0; i + 1 < frame.size(); ++i) {
        buf.append(frame.data() + i, 1);
        EXPECT_TRUE(drain(buf).empty()) << "yielded early at byte " << i;
    }

    buf.append(frame.data() + frame.size() - 1, 1);
    EXPECT_EQ(drain(buf), std::vector<uint16_t>{99});
}

TEST(StreamBuffer, ResetDiscardsPending) {
    const auto frame = make_frame("1001", 5);
    StreamBuffer buf;

    buf.append(frame.data(), 10);
    EXPECT_GT(buf.pending(), 0u);

    buf.reset();
    EXPECT_EQ(buf.pending(), 0u);

    const uint8_t* msg = nullptr;
    std::size_t    len = 0;
    EXPECT_FALSE(buf.next(&msg, &len));
}