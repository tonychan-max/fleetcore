// gateway -- the outward-facing process (ARCHITECTURE section 3-2: parse)
//
// Listens for maintenance terminals, validates whatever arrives against the
// wire contract, and forwards decoded commands to locator. Acknowledgements
// come back on the message queue and go out on the socket.
//
// Two input sources, neither allowed to block the other: the queue is
// serviced by the base class loop, the socket from on_idle().

#include "common/ipc_message.h"
#include "common/process.h"
#include "common/stream_buffer.h"
#include "common/tcp_server.h"
#include "common/wire.h"

#include <cstdio>
#include <cstring>

using namespace fleetcore;

namespace {

class Gateway : public Process {
public:
    Gateway() : Process("gateway", IDX_GATEWAY) {}

protected:
    bool on_init() override {
        register_handler(IPC_LOCK_RESPONSE,
            [this](const void* d, std::size_t n) {
                return on_lock_response(d, n);
            });

        if (!tcp_.listen(GATEWAY_PORT)) {
            log_msg(__func__, Severity::Process,
                    "cannot listen on %u", GATEWAY_PORT);
            return false;
        }
        log_trc(__func__, "listening for maintenance terminals on port %u",
                GATEWAY_PORT);
        return true;
    }

    // Called whenever the queue receive times out. Everything socket-related
    // happens here, so a quiet queue does not stall the socket and vice versa.
    void on_idle() override {
        if (tcp_.poll_accept()) {
            log_trc(__func__, "maintenance terminal connected");
            rx_.reset();
        }

        uint8_t chunk[1024];
        const long n = tcp_.poll_read(chunk, sizeof(chunk));
        if (n < 0) {
            log_msg(__func__, Severity::Peer, "maintenance terminal disconnected");
            rx_.reset();
            return;
        }
        if (n == 0) return;

        rx_.append(chunk, static_cast<std::size_t>(n));

        const uint8_t* msg = nullptr;
        std::size_t    len = 0;
        while (rx_.next(&msg, &len)) {
            const Severity sev = handle_from_maint(msg, len);
            rx_.consume(len);

            // A desynchronised stream cannot be resumed by skipping ahead:
            // there is no way to tell where the next message starts. Dropping
            // the connection makes the terminal reconnect, which resets both
            // ends to a known state.
            if (sev == Severity::Stream) {
                tcp_.drop_client();
                rx_.reset();
                return;
            }
        }
    }

private:
    Severity handle_from_maint(const uint8_t* buf, std::size_t len) {
        Header h{};
        WireError e = unpack_header(buf, len, h);
        if (e != WireError::Ok) {
            log_msg(__func__, Severity::Stream, "%s", to_string(e));
            return Severity::Stream;
        }

        if (h.code != CODE_LOCK_REQUEST) {
            // Known length, unhandled code: skip it and carry on. Without the
            // length field this would force a disconnect.
            log_msg(__func__, Severity::Message,
                    "code 0x%02X not handled", h.code);
            return Severity::Message;
        }

        LockRequest req{};
        e = unpack_lock_request(buf, len, req);
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

    Severity on_lock_response(const void* data, std::size_t len) {
        if (len != sizeof(IpcLockResponse)) {
            log_msg(__func__, Severity::Message, "unexpected size %zu", len);
            return Severity::Message;
        }

        IpcLockResponse m{};
        std::memcpy(&m, data, sizeof(m));

        log_trc(__func__, "LOCK-ACK term_no=%.4s seq=%u result=0x%02X",
                m.msg.term_no, m.msg.header.seq, m.msg.result);

        // Uplink status may be dropped (ARCHITECTURE section 3-6): if no
        // maintenance terminal is listening, log and move on. A newer status
        // supersedes this one anyway.
        if (!tcp_.has_client()) {
            log_msg(__func__, Severity::Peer,
                    "no maintenance terminal connected");
            return Severity::Message;
        }

        uint8_t out[MAX_MESSAGE_LEN];
        const std::size_t n = pack_lock_response(m.msg, out, sizeof(out));
        if (n == 0 || !tcp_.send(out, n)) {
            log_msg(__func__, Severity::Peer, "send to maintenance terminal failed");
        }
        return Severity::Message;
    }

    TcpServer    tcp_;
    StreamBuffer rx_;
};

}  // namespace

int main() {
    Gateway p;
    if (!p.init()) return 1;
    return p.run();
}
