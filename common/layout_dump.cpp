// Layout measurement tool (ADR-0003)
//
// This is not a throwaway script. Every time a field is added or reordered,
// run this first, verify the measured layout against the field table in
// docs/ARCHITECTURE.md, and only then update the static_assert values.

#include "common/proto.h"

#include <cstddef>
#include <cstdio>

using namespace fleetcore;

namespace {

std::size_t g_prev_end = 0;

void begin(const char* name, std::size_t total) {
    std::printf("\n=== %s : sizeof = %zu ===\n", name, total);
    std::printf("%-12s %8s %8s %8s\n", "field", "offset", "size", "gap");
    std::printf("---------------------------------------------\n");
    g_prev_end = 0;
}

// gap = bytes between the end of the previous field and the start of this one.
// With #pragma pack(1) every gap must be 0. A non-zero gap means the compiler
// inserted padding, so the byte layout is not what the field list suggests.
void field(const char* name, std::size_t offset, std::size_t size) {
    const std::size_t gap = offset - g_prev_end;
    std::printf("%-12s %8zu %8zu %8zu%s\n",
                name, offset, size, gap,
                gap ? "   <-- PADDING" : "");
    g_prev_end = offset + size;
}

void end(std::size_t total) {
    const std::size_t tail = total - g_prev_end;
    if (tail) {
        std::printf("%-12s %8s %8s %8zu   <-- TRAILING PADDING\n",
                    "(tail)", "-", "-", tail);
    }
}

}  // namespace

// Stringify the field name so the table stays in sync with the struct.
#define DUMP_FIELD(S, F) field(#F, offsetof(S, F), sizeof(S::F))

int main() {
    std::printf("fleetcore layout dump\n");

    begin("Header", sizeof(Header));
    DUMP_FIELD(Header, magic);
    DUMP_FIELD(Header, version);
    DUMP_FIELD(Header, code);
    DUMP_FIELD(Header, length);
    DUMP_FIELD(Header, seq);
    DUMP_FIELD(Header, timestamp);
    DUMP_FIELD(Header, reserved);
    end(sizeof(Header));

    begin("LockRequest", sizeof(LockRequest));
    DUMP_FIELD(LockRequest, header);
    DUMP_FIELD(LockRequest, term_no);
    end(sizeof(LockRequest));

    begin("LockResponse", sizeof(LockResponse));
    DUMP_FIELD(LockResponse, header);
    DUMP_FIELD(LockResponse, term_no);
    DUMP_FIELD(LockResponse, result);
    DUMP_FIELD(LockResponse, reserved);
    end(sizeof(LockResponse));

    std::printf("\nA non-zero gap means padding was inserted; "
                "#pragma pack may not be in effect.\n");
    return 0;
}