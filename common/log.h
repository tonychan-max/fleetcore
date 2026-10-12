#pragma once

namespace fleetcore {

// Monotonic timestamp shared by every process and thread, so logs from
// different programs can be merged into one timeline.
//
// CLOCK_MONOTONIC rather than wall clock: it never jumps backwards when the
// system clock is adjusted. The absolute value is meaningless; what counts
// is that two processes' numbers are comparable.
void log_timestamp();

}  // namespace fleetcore