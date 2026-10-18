// locator -- resolves a terminal number into a process index
// (ARCHITECTURE section 3-2: resolve)
//
// The lookup table is hard-coded for now. Step 3 replaces it with the
// shared-memory terminal table; nothing around this file changes when it
// does, which is why resolution has its own process.

#include "common/ipc_message.h"
#include "common/process.h"
#include "common/shm_table.h"

#include <cstdio>
#include <cstring>

using namespace fleetcore;

namespace {

class Locator : public Process {
public:
    Locator() : Process("locator", IDX_LOCATOR) {}

protected:
    bool on_init() override {
        if (!table_.open()) {
            log_msg(__func__, Severity::Process, "cannot open terminal table");
            return false;
        }
        register_handler(IPC_LOCK_REQUEST,
            [this](const void* d, std::size_t n) {
                return on_lock_request(d, n);
            });
        return true;
    }

private:
    ShmTable table_;
    // Resolves a terminal number to the termd that serves it.
    //
    // Returns 0 for a terminal that is not registered at all. A registered
    // terminal that is currently disconnected still resolves: its termd is
    // running and can receive the command. Whether to deliver or hold it is
    // termd's decision, not locator's.
    int32_t resolve(const char term_no_ascii[4]) const {
        uint32_t n = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = term_no_ascii[i];
            if (c < '0' || c > '9') return 0;
            n = n * 10 + static_cast<uint32_t>(c - '0');
        }

        TermEntry e{};
        if (!table_.find(n, e)) return 0;

        return IDX_TERMD_BASE + static_cast<int32_t>(n - 1000);
    }

    Severity on_lock_request(const void* data, std::size_t len) {
        if (len != sizeof(IpcLockRequest)) {
            log_msg(__func__, Severity::Message, "unexpected size %zu", len);
            return Severity::Message;
        }

        IpcLockRequest m{};
        std::memcpy(&m, data, sizeof(m));

        const int32_t idx = resolve(m.msg.term_no);
        if (idx == 0) {
            log_msg(__func__, Severity::Message,
                    "unknown terminal %.4s", m.msg.term_no);
            return Severity::Message;
        }

        log_trc(__func__, "%.4s -> index %d", m.msg.term_no, idx);

        m.ipc.dest_index = idx;
        send_to(idx, &m, sizeof(m));   // logs PEER itself on failure

        return Severity::Message;
    }
};

}  // namespace

int main() {
    Locator p;
    if (!p.init()) return 1;
    return p.run();
}