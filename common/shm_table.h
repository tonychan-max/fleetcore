#pragma once

#include <cstddef>
#include <cstdint>

namespace fleetcore {

// ============================================================
// The terminal table: shared memory, read by everyone, written by termd.
//
// This is the snapshot half of ARCHITECTURE section 3-4. Message queues
// carry what happened; this carries what is. The distinction matters
// because the two have opposite read semantics: a message is consumed by
// exactly one reader and then gone, while state can be read repeatedly, by
// several processes, without disturbing it.
//
// The table exists so that the set of terminals is a property of the
// system rather than of whoever started the processes. A termd is running
// for every registered terminal whether or not that terminal is currently
// reachable -- which is what makes it possible to hold a command for one
// that is out of coverage (Phase 4).
// ============================================================

inline constexpr int32_t  SHM_KEY       = 0x46430001;
inline constexpr int32_t  SEM_KEY       = 0x46430002;
inline constexpr uint32_t MAX_TERMINALS = 64;

// Connection state. Deliberately not an enum class: this value lives in
// shared memory and is read by processes compiled separately, so its
// underlying type is part of the layout.
inline constexpr uint8_t TERM_STATE_UNUSED       = 0;
inline constexpr uint8_t TERM_STATE_DISCONNECTED = 1;
inline constexpr uint8_t TERM_STATE_CONNECTED    = 2;

// Capability bits, declared by the terminal when it connects rather than
// configured on the server (ARCHITECTURE section 5). Configuration can
// drift from reality; a declaration cannot.
inline constexpr uint32_t CAP_LOCK       = 1u << 0;
inline constexpr uint32_t CAP_POSITION   = 1u << 1;   // Phase 5
inline constexpr uint32_t CAP_NAVIGATION = 1u << 2;   // Phase 6

// ============================================================
// One row.
//
// Packed and asserted like a wire struct, for the same reason: processes
// compiled separately must agree on the layout byte for byte. Shared
// memory is a contract too, just an internal one -- which is why it gets
// the same treatment as proto.h (ADR-0003).
// ============================================================
#pragma pack(push, 1)

struct TermEntry {
    uint32_t term_no;       // 0 means the slot is free
    uint32_t capabilities;
    uint8_t  state;
    uint8_t  reserved[3];   // explicit padding, keeps the row 4-byte aligned
    uint64_t last_seen;     // milliseconds, wall clock
};

#pragma pack(pop)

static_assert(sizeof(TermEntry) == 20, "TermEntry row size");
static_assert(offsetof(TermEntry, state) == 8, "offset of state");
static_assert(offsetof(TermEntry, last_seen) == 12, "offset of last_seen");

// ============================================================
// Attaches to the table, creating it if nobody has yet.
//
// Every accessor takes the semaphore for the duration of the call. The
// rows are small and the operations are short, so one lock for the whole
// table is simpler than one per row and costs nothing measurable at this
// scale. If Phase 5 shows otherwise, the finer-grained version goes behind
// the same interface.
//
// RAII: detaches on destruction. Whoever created the segment does not
// remove it -- other processes may still be attached, and there is no way
// to know. Removal is Step 7's problem, along with the message queues.
// ============================================================
class ShmTable {
public:
    ShmTable() = default;
    ~ShmTable();

    ShmTable(const ShmTable&) = delete;
    ShmTable& operator=(const ShmTable&) = delete;

    bool open();
    bool is_open() const { return rows_ != nullptr; }

    // Claims the row for term_no, or the first free one. Called by termd at
    // startup: the terminal is registered before it has ever connected.
    // Returns false when the table is full.
    bool register_terminal(uint32_t term_no);

    // Called when the terminal's socket comes up or goes down.
    bool set_state(uint32_t term_no, uint8_t state, uint32_t capabilities);

    // Refreshes last_seen. Phase 5 uses this for timeout detection.
    bool touch(uint32_t term_no);

    // Copies one row out. Readers get a snapshot, not a pointer into shared
    // memory -- holding such a pointer would mean reading a row while it is
    // being written.
    bool find(uint32_t term_no, TermEntry& out) const;

    // Copies every occupied row. For a future terminal-list message and for
    // diagnostics.
    std::size_t snapshot(TermEntry* out, std::size_t cap) const;

private:
    bool lock() const;
    bool unlock() const;

    int        shm_id_ = -1;
    int        sem_id_ = -1;
    TermEntry* rows_   = nullptr;
};

}  // namespace fleetcore