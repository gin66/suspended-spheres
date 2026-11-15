#pragma once

#include <stdint.h>

// Pure decision logic for the StallGuard homing search. No Arduino, no
// FastAccelStepper: the firmware feeds it the commanded travel, the DIAG
// level and the timers; the host tests drive it directly.
//
// There is deliberately no `isRunning()` input. FastAccelStepper can report
// not-running for a just-queued move, and treating that as "the move ended"
// put the firmware into FaultWait while the finite move kept running. The
// search is bounded by commanded travel instead: reach the cap, or stop
// making progress at all.

namespace homing_core {

enum class Action : uint8_t {
    Continue,
    Accept,        // stop and record home
    RaiseCurrent,  // stop, raise the current and retry
    FaultTravel,   // commanded travel reached the cap
    FaultTimeout,  // search took too long
    FaultEarly,    // sustained DIAG before the minimum travel at run current
    FaultNoProgress,  // commanded travel stopped advancing before the cap
};

struct Params {
    int32_t min_travel_steps;
    int32_t max_travel_steps;
    uint32_t timeout_ms;
    uint32_t stall_hold_ms;
    uint32_t immediate_hold_ms;
    uint32_t min_free_ms;
    uint32_t progress_timeout_ms;
    int32_t decel_zone_steps;  // ignore DIAG here; the finite move is ending
};

struct State {
    uint32_t last_ms;
    uint32_t diag_since_ms;
    uint32_t free_ms;
    uint32_t last_progress_ms;
    int32_t last_traveled;
    bool diag_seen;
};

// Reset the timers at the start of one search (or one current-retry).
void reset(State* state, uint32_t now_ms);

// Evaluate the search once. `elapsed_ms` is now_ms minus the search start;
// `traveled` is the commanded travel from the start; `diag` is the active-high
// DIAG level.
Action step(const Params& params, State* state, uint32_t now_ms,
            uint32_t elapsed_ms, int32_t traveled, bool diag,
            uint16_t current_ma, uint16_t run_ma);

// Commanded travel of a trapezoidal FastAccelStepper move at `elapsed_ms`.
// Used instead of getCurrentPosition() in the search loop: that read drains the
// Pico PIO RX FIFO and can stall against the stepper task. The result is
// clamped to `cap`; the finite move itself is the hard bound.
int32_t travelAt(uint32_t elapsed_ms, uint32_t speed_hz, uint32_t accel_hz2,
                 int32_t cap);

}  // namespace homing_core
