#include "common/process.h"

#include "common/ipc_message.h"
#include "common/proto.h"

#include <cstdarg>
#include <cstdio>
#include <utility>

namespace fleetcore {

namespace {

// How long to wait for a message before checking the shutdown flags.
// See docs/CONFIG.md: mq_recv_timeout_ms.
constexpr int RECV_TIMEOUT_MS = 200;

}  // namespace

Process::Process(std::string name, int32_t index)
    : name_(std::move(name)), index_(index) {}

bool Process::init() {
    if (!self_queue_.open(MSGQ_KEY_BASE + index_, true)) {
        log_msg(__func__, Severity::Process,
                "cannot create queue 0x%X", MSGQ_KEY_BASE + index_);
        return false;
    }
    log_trc(__func__, "listening on queue 0x%X", MSGQ_KEY_BASE + index_);
    return on_init();
}

void Process::register_handler(uint8_t code, Handler h) {
    handlers_[code] = std::move(h);
}

Severity Process::dispatch(const void* data, std::size_t len) {
    if (len < sizeof(IpcHeader)) {
        log_msg(__func__, Severity::Message, "short ipc message: %zu", len);
        return Severity::Message;
    }

    // The internal message code sits at a known offset; see ipc_message.h.
    const uint8_t code = *(static_cast<const uint8_t*>(data)
                           + offsetof(IpcHeader, code));

    const auto it = handlers_.find(code);
    if (it == handlers_.end()) {
        // Unknown code is not fatal: a newer peer may send things we do not
        // handle yet. Log it and move on.
        log_msg(__func__, Severity::Message, "no handler for code 0x%02X", code);
        return Severity::Message;
    }
    return it->second(data, len);
}

int Process::run() {
    // Sized for the largest internal message; matches the queue payload cap.
    uint8_t buf[MAX_MESSAGE_LEN];

    while (!system_end_ && !job_end_) {
        long n = 0;
        const auto r = self_queue_.recv_timeout(buf, sizeof(buf),
                                                RECV_TIMEOUT_MS, &n);

        if (r == MsgQueue::RecvResult::Timeout) {
            // Not an error. Loop back so the shutdown flags get checked.
            on_idle();
            continue;
        }
        if (r == MsgQueue::RecvResult::Error) {
            log_msg(__func__, Severity::Process, "receive failed");
            return 1;
        }

        const Severity sev = dispatch(buf, static_cast<std::size_t>(n));
        if (sev == Severity::Process) {
            log_msg(__func__, Severity::Process, "handler asked to stop");
            return 1;
        }
    }

    log_trc(__func__, "stopped (job_end=%d system_end=%d)",
            job_end_ ? 1 : 0, system_end_ ? 1 : 0);
    return 0;
}

bool Process::send_to(int32_t dest_index, const void* data, std::size_t len) {
    // Opened per send for now. Caching the handles belongs with the shared
    // terminal table in Step 3.
    MsgQueue q;
    if (!q.open(MSGQ_KEY_BASE + dest_index, false)) {
        log_msg(__func__, Severity::Peer,
                "process %d not running", dest_index);
        return false;
    }
    return q.send(data, len);
}

void Process::log_msg(const char* func, Severity sev, const char* fmt, ...) {
    std::fprintf(stderr, "[%s][%s][%s] ",
                 name_.c_str(), func, to_string(sev));
    va_list args;
    va_start(args, fmt);
    std::vfprintf(stderr, fmt, args);
    va_end(args);
    std::fputc('\n', stderr);
}

void Process::log_trc(const char* func, const char* fmt, ...) {
    std::fprintf(stderr, "[%s][%s] ", name_.c_str(), func);
    va_list args;
    va_start(args, fmt);
    std::vfprintf(stderr, fmt, args);
    va_end(args);
    std::fputc('\n', stderr);
}

// ---- StreamProcess ----

int StreamProcess::run_stream(int fd) {
    while (!system_end_ && !job_end_) {
        const Severity sev = on_stream_data(fd);

        if (sev == Severity::Stream) {
            log_trc(__func__, "stream ended");
            break;
        }
        if (sev == Severity::Process) {
            return 1;
        }
    }
    return 0;
}

}  // namespace fleetcore