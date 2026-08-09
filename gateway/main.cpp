// gateway -- the outward-facing process (ARCHITECTURE section 3-2: parse)
//
// Reads raw bytes from stdin, validates them against the wire contract, and
// forwards a decoded struct to locator. stdin stands in for the TCP listener
// that Step 6 will add.

#include "common/ipc_message.h"
#include "common/process.h"
#include "common/wire.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <unistd.h>

using namespace fleetcore;

namespace {

// Reads exactly n bytes unless the stream ends first.
// read() may return fewer bytes than asked for even when more are coming;
// treating a short read as "the message is short" is a classic bug.
ssize_t read_exact(int fd, uint8_t* buf, std::size_t n) {
    std::size_t got = 0;
    while (got < n) {
        const ssize_t r = ::read(fd, buf + got, n - got);
        if (r < 0) {
            // On Linux read() returns -1 simply because a signal arrived.
            // Treating that as a failure would drop a message every time
            // the process is signalled.
            if (errno == EINTR) continue;
            return -1;
        }
        if (r == 0) break;              // end of stream
        got += static_cast<std::size_t>(r);
    }
    return static_cast<ssize_t>(got);
}

class Gateway : public StreamProcess {
public:
    Gateway() : StreamProcess("gateway", IDX_GATEWAY) {}

protected:
    Severity on_stream_data(int fd) override {
        uint8_t buf[MAX_MESSAGE_LEN];

        // Read the header first: its length field says how much more to
        // read. This is exactly why the contract carries a length at all.
        const ssize_t n = read_exact(fd, buf, sizeof(Header));
        if (n == 0) return Severity::Stream;          // clean end of input
        if (n < static_cast<ssize_t>(sizeof(Header))) {
            log_msg(__func__, Severity::Stream,
                    "%s (%zd bytes)", to_string(WireError::TooShort), n);
            return Severity::Stream;
        }

        Header h{};
        WireError e = unpack_header(buf, sizeof(Header), h);
        if (e != WireError::Ok) {
            // The stream is out of sync; resynchronisation belongs in Step 6.
            log_msg(__func__, Severity::Stream, "%s", to_string(e));
            return Severity::Stream;
        }

        const std::size_t remain = h.length - sizeof(Header);
        if (remain > 0) {
            const ssize_t r = read_exact(fd, buf + sizeof(Header), remain);
            if (r < static_cast<ssize_t>(remain)) {
                log_msg(__func__, Severity::Stream, "truncated body");
                return Severity::Stream;
            }
        }

        if (h.code != CODE_LOCK_REQUEST) {
            // A message we do not handle but whose length we know: skip it.
            // Without the length field this would force a disconnect.
            log_msg(__func__, Severity::Message,
                    "code 0x%02X not handled", h.code);
            return Severity::Message;
        }

        LockRequest req{};
        e = unpack_lock_request(buf, h.length, req);
        if (e != WireError::Ok) {
            log_msg(__func__, Severity::Message, "%s", to_string(e));
            return Severity::Message;
        }

        log_trc(__func__, "parsed seq=%u term_no=%.4s",
                req.header.seq, req.term_no);

        IpcLockRequest out{};
        out.ipc.code       = IPC_LOCK_REQUEST;
        out.ipc.dest_index = 0;            // locator fills this in
        out.msg            = req;

        send_to(IDX_LOCATOR, &out, sizeof(out));
        return Severity::Message;
    }
};

}  // namespace

int main() {
    Gateway p;
    if (!p.init()) return 1;
    return p.run_stream(STDIN_FILENO);
}