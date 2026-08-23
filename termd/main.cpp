// termd -- one process per terminal (ARCHITECTURE section 3-2: transform)
//
// The queue key embeds the terminal number, so `ipcs -q` shows at a glance
// which queue belongs to which terminal.
//
// Step 6 added the TCP connection to the terminal. This process now has two
// input sources: the message queue (commands from locator) and the socket
// (responses from the terminal). Neither may block the other, so the socket
// is serviced from on_idle().

#include "common/ipc_message.h"
#include "common/process.h"
#include "common/wire.h"
#include "common/stream_buffer.h"
#include "common/tcp_server.h"

#include <charconv>
#include <cstdio>
#include <cstring>

using namespace fleetcore;

namespace {

class Termd : public Process {
public:
    Termd(int32_t slot)
        : Process("termd " + std::to_string(IDX_TERMD_BASE + slot),
                  IDX_TERMD_BASE + slot), slot_(slot) {}

protected:
    bool on_init() override {
        register_handler(IPC_LOCK_REQUEST,
            [this](const void* d, std::size_t n) {
                return on_lock_request(d, n);
            });

        // Port carries the slot number for the same reason the process index
        // does: `ss -tln` shows which terminal each listener belongs to.
        const uint16_t port = static_cast<uint16_t>(TERM_PORT_BASE + slot_);
        if (!tcp_.listen(port)) {
            log_msg(__func__, Severity::Process, "cannot listen on %u", port);
            return false;
        }
        log_trc(__func__, "listening for terminal on port %u", port);

        return true;
    }

    // Called every time the queue receive times out (200 ms). This is where
    // the socket gets serviced: like gateway, this process has two input
    // sources and neither may block the other.
    void on_idle() override {
        if (tcp_.poll_accept()) {
            log_trc(__func__, "terminal connected");
        }

        uint8_t chunk[1024];
        const long n = tcp_.poll_read(chunk, sizeof(chunk));
        if (n < 0) {
            log_msg(__func__, Severity::Peer, "terminal disconnected");
            rx_.reset();
            return;
        }
        if (n == 0) return;

        rx_.append(chunk, static_cast<std::size_t>(n));

        const uint8_t* msg = nullptr;
        std::size_t    len = 0;
        while (rx_.next(&msg, &len)) {
            handle_from_terminal(msg, len);
            rx_.consume(len);
        }
    }

    void handle_from_terminal(const uint8_t* buf, std::size_t len) {
        Header h{};
        const WireError e = unpack_header(buf, len, h);
        if (e != WireError::Ok) {
            log_msg(__func__, Severity::Stream,
                    "%s -- dropping connection", to_string(e));
            tcp_.drop_client();
            rx_.reset();
            return;
        }
        log_trc(__func__, "from terminal: code 0x%02X seq=%u", h.code, h.seq);
    }

private:
  
    TcpServer    tcp_;
    StreamBuffer rx_;
    int32_t      slot_ = 0;

    Severity on_lock_request(const void* data, std::size_t len) {
        if (len != sizeof(IpcLockRequest)) {
            log_msg(__func__, Severity::Message,
                    "unexpected size %zu", len);
            return Severity::Message;
        }

        IpcLockRequest m{};
        std::memcpy(&m, data, sizeof(m));

        // Transform: this is where the internal form would become whatever
        // the terminal expects. Step 6 packs it back onto a TCP socket.
        std::printf("[%s] LOCK term_no=%.4s seq=%u ts=%llu\n",
                    name().c_str(),
                    m.msg.term_no,
                    m.msg.header.seq,
                    static_cast<unsigned long long>(m.msg.header.timestamp));
        std::fflush(stdout);
       
        // Transform: internal form becomes the wire form the terminal
        // expects (ARCHITECTURE section 3-2).
        if (tcp_.has_client()) {
            uint8_t out[MAX_MESSAGE_LEN];
            const std::size_t n = pack_lock_request(m.msg, out, sizeof(out));
            if (n == 0 || !tcp_.send(out, n)) {
                log_msg(__func__, Severity::Peer, "send to terminal failed");
            }
        } else {
            log_msg(__func__, Severity::Peer, "no terminal connected");
        }

        // Uplink: report that the command was carried out.
        //
        // ARCHITECTURE section 3-6: uplink status may be dropped. If the
        // send fails we log it and move on -- a newer status will supersede
        // this one anyway. Downlink commands get the opposite treatment
        // (store-and-forward, Phase 4).
        IpcLockResponse rsp{};
        rsp.ipc.code       = IPC_LOCK_RESPONSE;
        rsp.ipc.dest_index = IDX_GATEWAY;
        rsp.msg = make_lock_response(m.msg.term_no,
                                     m.msg.header.seq,
                                     RESULT_OK);

        if (!send_to(IDX_GATEWAY, &rsp, sizeof(rsp))) {
            log_msg(__func__, Severity::Message,
                    "uplink dropped: gateway not running");
        }
        return Severity::Message;   // handled
    }

};

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <terminal slot number>\n", argv[0]);
        return 1;
    }

    int32_t slot = 0;
    const char* first = argv[1];
    const char* last  = first + std::strlen(first);
    const auto res = std::from_chars(first, last, slot);
    if (res.ec != std::errc{} || res.ptr != last || slot <= 0) {
        std::fprintf(stderr, "slot must be a positive number\n");
        return 1;
    }

    Termd p(slot);
    if (!p.init()) return 1;
    return p.run();
}