// termd -- one process per terminal (ARCHITECTURE section 3-2: transform)
//
// The queue key embeds the terminal number, so `ipcs -q` shows at a glance
// which queue belongs to which terminal.
//
// Step 6 adds the TCP connection to the real terminal. For now it prints.

#include "common/ipc_message.h"
#include "common/process.h"

#include <charconv>
#include <cstdio>
#include <cstring>

using namespace fleetcore;

namespace {

class Termd : public Process {
public:
    Termd(int32_t slot)
        : Process("termd " + std::to_string(IDX_TERMD_BASE + slot),
                  IDX_TERMD_BASE + slot) {}

protected:
    bool on_init() override {
        register_handler(IPC_LOCK_REQUEST,
            [this](const void* d, std::size_t n) {
                return on_lock_request(d, n);
            });
        return true;
    }

private:
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