#pragma once

#include <stdint.h>

// StallGuard homing against the upper mechanical stop
// (doc/motion_streaming_whitepaper.md §5.7).
//
// The search uses the lowest current that reliably lifts the ball, StealthChop
// and a slow constant speed, and detects the stop from a sustained StallGuard
// assert after a minimum travel. On an immediate assert the current is raised
// in small steps up to the normal run current. The bench parameters come from
// doc/bench/tmc2209/2026-11-15-M0-SC-300-16-01: 100 mA lifts, 30 mA slips,
// and on that rig negative steps raise while positive steps drive into the
// bottom stop.
//
// One axis is homed at a time. A CAN HOME mask is queued and homed
// sequentially. Homing owns the axis: do not command move/run/mode/current on
// it while busy(). A STOP cancels the search and restores the normal current.

namespace homing {

enum class State : uint8_t {
    Idle,
    Search,
    Backoff,
    Fault,
};

struct Config {
    uint16_t start_ma = 300;      // lowest current with a usable top SG signal
    uint16_t step_ma = 25;        // escalation step on an immediate assert
    uint8_t  sgthrs = 15;         // threshold 30; lift SG >50, top press 2-24
    uint32_t speed_hz = 2000;     // 78 mm/s: SG amplitude and spot the top
    uint32_t accel_hz2 = 2000;    // reach 2000 step/s in 1 s
    int32_t  min_travel_steps = 16000;  // 5 turns; reject lift SG glitches
    int32_t  max_travel_steps = 70400;  // 22 turns / 2.76 m finite search cap
    int32_t  backoff_steps = 3200;      // one turn / 126 mm below the stop
    uint32_t settle_ms = 2500;          // ignore SG during the spin-up ramp
    uint32_t stall_hold_ms = 0;         // stop on the first accepted assert
    uint32_t min_free_ms = 1000;        // ignore a stall present from the start
    uint32_t immediate_hold_ms = 500;   // sustained early assert -> raise current
    uint32_t timeout_ms = 90000;        // search timeout
};

const Config& defaults();
void setDefaults(const Config& config);

// Queue mask (bit 0 = axis 0). Returns false if busy, the mask is empty, or an
// axis is not ready/running. `override` may be null.
bool start(uint8_t axis_mask, const Config* override_config = nullptr);

// Drive the state machine. Call from the main loop after tmc2209::poll().
void poll(uint32_t now_ms);

// Stop the search and restore the normal current/mode. Safe when idle.
void cancel();

bool busy();
bool faulted();
State state();
uint8_t activeAxis();

// Steps from the last ok home: 0 at the stop, +backoff after the backoff move.
int32_t homePositionSteps();

void printState();

}  // namespace homing
