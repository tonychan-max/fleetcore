#pragma once

#include "common/proto.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace fleetcore {

// ============================================================
// Reassembles whole messages out of a TCP byte stream.
//
// TCP has no message boundaries. A single recv() may return half a message,
// one and a half, or three at once -- the sender's writes and the receiver's
// reads are unrelated. Treating whatever one recv() returned as "a message"
// is the single most common mistake when moving from message queues to TCP.
//
// The header carries a length field precisely so the boundary can be
// recovered (ARCHITECTURE section 4-1). This class is where that pays off.
//
// Usage:
//     buf.append(chunk, n);
//     while (buf.next(msg, &len)) { handle(msg, len); }
// ============================================================

class StreamBuffer {
public:
    // Adds freshly received bytes.
    void append(const uint8_t* data, std::size_t len);

    // Extracts one complete message if there is one.
    // Returns false when more bytes are needed.
    // On true, *out points into the internal buffer and stays valid until
    // the next call to append() or next().
    bool next(const uint8_t** out, std::size_t* out_len);

    // Removes the message returned by the last successful next().
    void consume(std::size_t len);

    // Discards everything. Used when the stream has lost sync.
    void reset() { data_.clear(); }

    std::size_t pending() const { return data_.size(); }

private:
    std::vector<uint8_t> data_;
};

}  // namespace fleetcore