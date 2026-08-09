#pragma once

namespace fleetcore {

// How far a failure reaches.
//
// The four levels correspond to four recovery strategies. Naming them
// makes the policy visible at the call site and in the log, instead of
// being implied by whether the code says continue or break.
enum class Severity {
    Message,   // this message is bad; drop it and read the next one
    Stream,    // the byte stream lost sync; nothing after this is parseable
    Peer,      // one terminal is unreachable; the others keep working
    Process,   // cannot continue; exit and let the supervisor restart us
};

inline const char* to_string(Severity s) {
    switch (s) {
        case Severity::Message: return "MESSAGE";
        case Severity::Stream:  return "STREAM";
        case Severity::Peer:    return "PEER";
        case Severity::Process: return "PROCESS";
    }
    return "?";
}

}  // namespace fleetcore