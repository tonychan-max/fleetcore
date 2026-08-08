// Produces one real LockRequest byte stream on stdout.
//
//   ./build/msggen 1001 | xxd
//   ./build/msggen 1001 > /tmp/lock.bin
//
// Used to feed gateway with something that actually came off a "wire",
// rather than a struct built in the same process.

#include "common/wire.h"

#include <charconv>
#include <cstdio>
#include <cstring>
#include <unistd.h>

using namespace fleetcore;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <term_no(4 chars)> [seq]\n", argv[0]);
        return 1;
    }
    if (std::strlen(argv[1]) != 4) {
        std::fprintf(stderr, "term_no must be exactly 4 characters\n");
        return 1;
    }

    uint16_t seq = 1;
    if (argc >= 3) {
        const char* first = argv[2];
        const char* last  = first + std::strlen(first);
        // from_chars reports failure instead of silently returning 0,
        // which is what atoi would do for a non-numeric argument.
        const auto res = std::from_chars(first, last, seq);
        if (res.ec != std::errc{} || res.ptr != last) {
            std::fprintf(stderr, "seq must be a number in 0..65535\n");
            return 1;
        }
    }

    const LockRequest m = make_lock_request(argv[1], seq);

    uint8_t buf[MAX_MESSAGE_LEN];
    const std::size_t n = pack_lock_request(m, buf, sizeof(buf));
    if (n == 0) {
        std::fprintf(stderr, "pack failed\n");
        return 1;
    }

    // Write raw bytes, not text. stdout is a byte stream here.
    if (::write(STDOUT_FILENO, buf, n) != static_cast<ssize_t>(n)) {
        std::fprintf(stderr, "write failed\n");
        return 1;
    }
    return 0;
}