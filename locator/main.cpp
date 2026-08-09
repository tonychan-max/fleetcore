// locator -- resolves a terminal number into a process index
// (ARCHITECTURE section 3-2: resolve)
//
// The lookup table is hard-coded for now. Step 3 replaces it with the
// shared-memory terminal table; nothing around this file changes when it
// does, which is why resolution has its own process.

#include "common/ipc_message.h"
#include "common/process.h"

#include <cstdio>
#include <cstring>

using namespace fleetcore;

namespace {

struct TermMapping {
    const char* term_no;   // 4 ASCII characters
    int32_t     index;
};

// TODO(Step 3): replace with the shared-memory terminal table.
constexpr TermMapping TABLE[] = {
    { "1001", IDX_TERMD_BASE + 1 },
    { "1002", IDX_TERMD_BASE + 2 },
};

class Locator : public Process {
public:
    Locator() : Process("locator", IDX_LOCATOR) {}

protected:
    bool on_init() override {
        register_handler(IPC_LOCK_REQUEST,
            [this](const void* d, std::size_t n) {
                return on_lock_request(d, n);
            });
        return true;
    }

private:
    static int32_t resolve(const char term_no[4]) {
        for (const auto& m : TABLE) {
            if (std::memcmp(m.term_no, term_no, 4) == 0) return m.index;
        }
        return 0;
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