#pragma once

#include "common/error_policy.h"
#include "common/msgq.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>

namespace fleetcore {

// ============================================================
// Common skeleton for a business process.
//
// Every process in this system does the same thing: own a queue, loop until
// told to stop, receive a message, and dispatch it by message code. Only the
// handlers differ. Putting the loop here means adding a process is a matter
// of registering handlers, not copying a main().
//
// Two independent shutdown flags:
//   job_end     this process has finished its work; the system runs on
//   system_end  the whole system is going down
// They are separate because the cleanup differs -- Step 7 will set
// system_end from a SIGTERM handler.
// ============================================================

class Process {
public:
    // Signature of a message handler. Returns how far a failure reaches;
    // Severity::Message means "handled, or dropped, carry on".
    using Handler = std::function<Severity(const void* data, std::size_t len)>;

    Process(std::string name, int32_t index);
    virtual ~Process() = default;

    Process(const Process&) = delete;
    Process& operator=(const Process&) = delete;

    // Creates this process's own queue and calls on_init().
    bool init();

    // Main loop. Returns the process exit code.
    int run();

    void request_job_end()    { job_end_ = true; }
    void request_system_end() { system_end_ = true; }

    // Logging. func is normally __func__ at the call site.
    void log_msg(const char* func, Severity sev, const char* fmt, ...);
    void log_trc(const char* func, const char* fmt, ...);

protected:
    // Subclasses register their handlers here.
    virtual bool on_init() { return true; }

    // Called once per loop iteration when no message arrived. Default does
    // nothing; a process with periodic work overrides it.
    virtual void on_idle() {}

    void register_handler(uint8_t code, Handler h);

    // Sends to another process's queue. Opens it on demand.
    bool send_to(int32_t dest_index, const void* data, std::size_t len);

    // Checks the queue once without blocking. Returns true if a message was
    // handled. Used by StreamProcess, which spends most of its time blocked
    // on a stream and cannot wait on the queue as well.
    bool poll_queue_once();

    const std::string& name() const { return name_; }
    int32_t index() const { return index_; }

    std::atomic<bool> job_end_{false};
    std::atomic<bool> system_end_{false};

private:
    Severity dispatch(const void* data, std::size_t len);

    std::string name_;
    int32_t     index_;
    MsgQueue    self_queue_;
    std::unordered_map<uint8_t, Handler> handlers_;
};

// ============================================================
// A process whose input is a byte stream rather than a queue.
//
// gateway reads from stdin today and from a TCP socket in Step 6. Neither is
// a message queue, so it needs a different loop -- but it still wants the
// shutdown flags, the logging and send_to().
// ============================================================
class StreamProcess : public Process {
public:
    using Process::Process;

    // Loop that reads from fd instead of the queue.
    int run_stream(int fd);

protected:
    // Reads and handles exactly one message.
    // Returning Severity::Stream ends the loop.
    virtual Severity on_stream_data(int fd) = 0;
};

}  // namespace fleetcore