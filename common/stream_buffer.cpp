#include "common/stream_buffer.h"

#include "common/wire.h"

namespace fleetcore {

void StreamBuffer::append(const uint8_t* data, std::size_t len) {
    data_.insert(data_.end(), data, data + len);
}

bool StreamBuffer::next(const uint8_t** out, std::size_t* out_len) {
    // Not even a header yet.
    if (data_.size() < sizeof(Header)) return false;

    // Read the length field directly rather than unpacking the whole header:
    // at this point we only need to know how far the message extends.
    const uint16_t length =
        load_be16(data_.data() + offsetof(Header, length));

    // A length outside the valid range means the stream is not aligned to a
    // message start any more. Report the whole buffer so the caller can see
    // it, and let the caller decide to reset -- silently resynchronising
    // would hide a real problem.
    if (length < sizeof(Header) || length > MAX_MESSAGE_LEN) {
        *out     = data_.data();
        *out_len = data_.size();
        return true;   // caller will fail to unpack it and reset
    }

    // Whole message not here yet.
    if (data_.size() < length) return false;

    *out     = data_.data();
    *out_len = length;
    return true;
}

void StreamBuffer::consume(std::size_t len) {
    if (len >= data_.size()) {
        data_.clear();
    } else {
        data_.erase(data_.begin(), data_.begin() + static_cast<long>(len));
    }
}

}  // namespace fleetcore