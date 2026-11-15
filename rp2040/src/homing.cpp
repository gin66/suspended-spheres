#include "homing.h"

#include <Arduino.h>
#include <FastAccelStepper.h>

#include "board_pins.h"
#include "homing_core.h"
#include "stepper_axes.h"
#include "tmc2209.h"

namespace homing {
namespace {

// Measured 2026-11-15 (M0, NEMA-17 pancake, 40 mm spool): negative steps raise
// and positive steps drive into the bottom stop. Flip this for an axis wound
// the other way.
constexpr int8_t kHomeUpSign = -1;

// Search phases. FaultWait / CancelWait only wait for the axis to stop before
// restoring the normal current and chopper mode; the mode write is only legal
// with the axis stopped.
enum class Phase : uint8_t {
    Idle,
    Search,
    RaiseWait,
    StopWait,
    Backoff,
    FaultWait,
    CancelWait,
    Fault,
};

Config s_defaults;
Config s_config;
uint8_t s_pending_mask = 0;
uint8_t s_axis = 0xFF;
Phase s_phase = Phase::Idle;
const char* s_reason = "";

bool s_saved = false;
uint16_t s_normal_run_ma = 0;
uint16_t s_normal_hold_ma = 0;
uint8_t s_normal_sgthrs = 0;
uint32_t s_normal_tcoolthrs = 0;

uint16_t s_current_ma = 0;
int32_t s_search_cap = 0;
int32_t s_home_steps = 0;
uint32_t s_start_ms = 0;
uint32_t s_next_log_ms = 0;
uint32_t s_last_sg_ms = 0;
bool s_sg_stall = false;
uint32_t s_detect_ms = 0;
int32_t s_detect_travel = 0;
bool s_detect_pin = false;
bool s_detected = false;
homing_core::State s_core;

bool validAxis(uint8_t axis) {
    return axis < tmc2209::kDriverCount;
}

bool stopped(uint8_t axis) {
    FastAccelStepper* stepper = stepper_axes::axis(axis);
    return stepper != nullptr && !stepper->isRunning();
}

void printPrefix(uint8_t axis) {
    Serial.print("home: tmc");
    Serial.print(axis);
    Serial.print("@");
    Serial.print(axis);
    Serial.print(" ");
}

bool setCurrentsRetry(uint8_t axis, uint16_t run_ma, uint16_t hold_ma) {
    for (uint8_t i = 0; i < 10; ++i) {
        if (tmc2209::setCurrents(axis, run_ma, hold_ma)) {
            return true;
        }
    }
    return false;
}

bool setSpreadCycleRetry(uint8_t axis, bool enable) {
    for (uint8_t i = 0; i < 3; ++i) {
        if (tmc2209::setSpreadCycle(axis, enable)) {
            return true;
        }
    }
    return false;
}

bool setStallGuardRetry(uint8_t axis, uint8_t sgthrs) {
    for (uint8_t i = 0; i < 3; ++i) {
        if (tmc2209::setStallGuard(axis, sgthrs)) {
            return true;
        }
    }
    return false;
}

bool setTcoolthrsRetry(uint8_t axis, uint32_t value) {
    for (uint8_t i = 0; i < 3; ++i) {
        if (tmc2209::setTcoolthrs(axis, value)) {
            return true;
        }
    }
    return false;
}

bool restoreNormal() {
    if (!s_saved || !validAxis(s_axis)) {
        return false;
    }
    bool ok = setCurrentsRetry(s_axis, s_normal_run_ma, s_normal_hold_ma);
    ok = setStallGuardRetry(s_axis, s_normal_sgthrs) && ok;
    ok = setTcoolthrsRetry(s_axis, s_normal_tcoolthrs) && ok;
    // Motion runs in SpreadCycle; homing is the only StealthChop user.
    ok = setSpreadCycleRetry(s_axis, true) && ok;
    s_saved = false;
    return ok;
}

void fault(const char* reason) {
    s_reason = reason;
    s_pending_mask = 0;
    if (validAxis(s_axis)) {
        stepper_axes::stop(s_axis);
        s_phase = Phase::FaultWait;
    } else {
        s_phase = Phase::Fault;
    }
}

void finishOk() {
    printPrefix(s_axis);
    Serial.print("ok home_steps=");
    Serial.println(s_home_steps);
    s_axis = 0xFF;
    s_saved = false;
    s_phase = Phase::Idle;
}

void beginAxis(uint8_t axis) {
    s_axis = axis;
    s_reason = "";
    s_current_ma = 0;
    if (!tmc2209::ready(axis) || stepper_axes::axis(axis) == nullptr ||
        stepper_axes::isRunning(axis)) {
        fault("axis not ready or running");
        return;
    }

    const tmc2209::Config& cfg = tmc2209::config(axis);
    s_normal_run_ma = cfg.run_ma;
    s_normal_hold_ma = cfg.hold_ma;
    s_normal_sgthrs = cfg.sgthrs;
    s_normal_tcoolthrs = cfg.tcoolthrs;
    s_saved = true;

    s_current_ma = s_config.start_ma;
    if (s_current_ma > s_normal_run_ma) {
        s_current_ma = s_normal_run_ma;
    }

    // StealthChop is required for StallGuard. Stop before changing the mode.
    if (!setSpreadCycleRetry(axis, false)) {
        fault("mode");
        return;
    }
    if (!setCurrentsRetry(axis, s_current_ma, s_current_ma)) {
        fault("current");
        return;
    }
    if (!setStallGuardRetry(axis, s_config.sgthrs)) {
        fault("sgthrs");
        return;
    }
    // Arm StallGuard only at the search speed and above: TSTEP scales as
    // 1/speed. 800000/hz is the measured 1000 step/s TSTEP (762) with margin.
    // This keeps the low-speed ramp and the final deceleration from asserting.
    uint32_t tcoolthrs = 800000u / s_config.speed_hz;
    if (tcoolthrs == 0) {
        tcoolthrs = 1;
    }
    if (tcoolthrs > 0xFFFFFu) {
        tcoolthrs = 0xFFFFFu;
    }
    if (!setTcoolthrsRetry(axis, tcoolthrs)) {
        fault("tcoolthrs");
        return;
    }

    stepper_axes::setAxisEnabled(axis, true);
    stepper_axes::setSpeed(axis, s_config.speed_hz);
    stepper_axes::setAcceleration(axis, s_config.accel_hz2);

    // One refresh with the axis stopped, to clear a stale stall state. No
    // UART reads are performed once the finite search move starts.
    tmc2209::refresh(axis);
    tmc2209::clearDiagEvents();

    s_search_cap = s_config.max_travel_steps;
    s_start_ms = millis();
    s_next_log_ms = s_start_ms + 500;
    s_last_sg_ms = s_start_ms;
    s_sg_stall = false;
    s_detected = false;
    s_detect_ms = 0;
    s_detect_travel = 0;
    homing_core::reset(&s_core, s_start_ms);
    s_phase = Phase::Search;

    printPrefix(axis);
    Serial.print("start ma=");
    Serial.print(s_current_ma);
    Serial.print(" hz=");
    Serial.print(s_config.speed_hz);
    Serial.print(" sgthrs=");
    Serial.print(s_config.sgthrs);
    Serial.print(" cap=");
    Serial.println(s_config.max_travel_steps);

    // A finite move, not a continuous run: the PIO stops at the travel cap
    // even if the CPU wedges. Detection normally stops it much earlier.
    if (!stepper_axes::move(axis, kHomeUpSign * s_search_cap)) {
        fault("search start");
    }
}

void startNext() {
    if (s_pending_mask == 0) {
        s_phase = Phase::Idle;
        s_axis = 0xFF;
        return;
    }
    uint8_t axis = 0;
    while (axis < tmc2209::kDriverCount &&
           (s_pending_mask & (uint8_t)(1u << axis)) == 0) {
        ++axis;
    }
    s_pending_mask &= (uint8_t)~(1u << axis);
    beginAxis(axis);
}


homing_core::Params params() {
    homing_core::Params p;
    p.min_travel_steps = s_config.min_travel_steps;
    p.max_travel_steps = s_search_cap;
    p.timeout_ms = s_config.timeout_ms;
    p.stall_hold_ms = s_config.stall_hold_ms;
    p.immediate_hold_ms = s_config.immediate_hold_ms;
    p.min_free_ms = s_config.min_free_ms;
    p.progress_timeout_ms = 2000;
    // Ignore DIAG in the finite move's deceleration tail.
    const int32_t decel_steps =
        (int32_t)((uint64_t)s_config.speed_hz * s_config.speed_hz /
                  (2ull * s_config.accel_hz2));
    p.decel_zone_steps = decel_steps + 1000;
    return p;
}

void pollSearch(uint32_t now_ms) {
    const uint32_t elapsed_ms =
        (now_ms >= s_start_ms) ? (uint32_t)(now_ms - s_start_ms) : 0;
    const int32_t traveled =
        homing_core::travelAt(elapsed_ms, s_config.speed_hz,
                              s_config.accel_hz2, s_search_cap);
    const bool diag_pin = digitalRead(board::kDrives[s_axis].diag) != 0;
    if ((uint32_t)(now_ms - s_last_sg_ms) >= 20) {
        s_last_sg_ms = now_ms;
        uint32_t sg = 0;
        if (tmc2209::readRegister(s_axis, 0x41, &sg)) {  // SG_RESULT
            s_sg_stall = (uint16_t)(sg & 0x3FFu) <=
                         (uint16_t)(2u * s_config.sgthrs);
        }
    }
    const bool diag = diag_pin || s_sg_stall;

    if ((int32_t)(now_ms - s_next_log_ms) >= 0) {
        printPrefix(s_axis);
        Serial.print("t=");
        Serial.print((uint32_t)(now_ms - s_start_ms));
        Serial.print(" travel=");
        Serial.print(traveled);
        Serial.print(" ma=");
        Serial.print(s_current_ma);
        Serial.print(" diag=");
        Serial.println(diag ? 1 : 0);
        s_next_log_ms += 500;
    }

    const homing_core::Action action =
        homing_core::step(params(), &s_core, now_ms, elapsed_ms, traveled,
                          diag, s_current_ma, s_normal_run_ma);
    switch (action) {
        case homing_core::Action::Continue:
            return;
        case homing_core::Action::Accept:
            s_detected = true;
            s_detect_ms = now_ms;
            s_detect_travel = traveled;
            s_detect_pin = diag_pin;
            printPrefix(s_axis);
            Serial.print("accept t=");
            Serial.print((uint32_t)(now_ms - s_start_ms));
            Serial.print(" travel=");
            Serial.print(traveled);
            Serial.print(" pin=");
            Serial.println(diag_pin ? 1 : 0);
            // Immediate stop; the caller owns the position from here.
            stepper_axes::forceStopAndSetPosition(s_axis, 0);
            s_phase = Phase::StopWait;
            return;
        case homing_core::Action::RaiseCurrent:
            stepper_axes::stop(s_axis);
            s_phase = Phase::RaiseWait;
            return;
        case homing_core::Action::FaultTravel:
            fault("travel limit before stop");
            return;
        case homing_core::Action::FaultTimeout:
            fault("timeout");
            return;
        case homing_core::Action::FaultEarly:
            fault("stall before minimum travel at run current");
            return;
        case homing_core::Action::FaultNoProgress:
            fault("no travel progress");
            return;
    }
}

}  // namespace

const Config& defaults() {
    return s_defaults;
}

void setDefaults(const Config& config) {
    s_defaults = config;
}

bool start(uint8_t axis_mask, const Config* override_config) {
    if (s_phase != Phase::Idle && s_phase != Phase::Fault) {
        return false;
    }
    if (axis_mask == 0 ||
        (axis_mask & (uint8_t)~((1u << tmc2209::kDriverCount) - 1u)) != 0) {
        return false;
    }
    for (uint8_t i = 0; i < tmc2209::kDriverCount; ++i) {
        if ((axis_mask & (uint8_t)(1u << i)) == 0) {
            continue;
        }
        if (!tmc2209::ready(i) || stepper_axes::axis(i) == nullptr ||
            stepper_axes::isRunning(i)) {
            return false;
        }
    }

    s_config = override_config != nullptr ? *override_config : s_defaults;
    s_pending_mask = axis_mask;
    s_reason = "";
    s_phase = Phase::Idle;
    startNext();
    return true;
}

void poll(uint32_t now_ms) {
    if (s_phase == Phase::Idle || s_phase == Phase::Fault) {
        return;
    }
    if (!validAxis(s_axis)) {
        s_phase = Phase::Idle;
        return;
    }
    FastAccelStepper* stepper = stepper_axes::axis(s_axis);
    if (stepper == nullptr) {
        fault("axis missing");
        return;
    }


    // A driver fault bit is never a homing reference. The bits are refreshed
    // by tmc2209::poll() on a DIAG edge, so this is a memory read.
    const tmc2209::DriverStatus& status = tmc2209::status(s_axis);
    if (s_phase != Phase::FaultWait && s_phase != Phase::CancelWait &&
        (status.drv_status & 0xFu) != 0) {
        fault("driver fault");
        return;
    }

    switch (s_phase) {
        case Phase::Search:
            pollSearch(now_ms);
            break;

        case Phase::RaiseWait:
            if (stopped(s_axis)) {
                uint32_t next = (uint32_t)s_current_ma + s_config.step_ma;
                if (next > s_normal_run_ma) {
                    next = s_normal_run_ma;
                }
                if (!setCurrentsRetry(s_axis, (uint16_t)next,
                                      (uint16_t)next)) {
                    fault("current raise");
                    return;
                }
                s_current_ma = (uint16_t)next;
                printPrefix(s_axis);
                Serial.print("raise ma=");
                Serial.println(s_current_ma);
                tmc2209::clearDiagEvents();
                const uint32_t elapsed_ms =
                    (now_ms >= s_start_ms) ? (uint32_t)(now_ms - s_start_ms) : 0;
                s_search_cap -= homing_core::travelAt(
                    elapsed_ms, s_config.speed_hz, s_config.accel_hz2,
                    s_search_cap);
                if (s_search_cap <= 0) {
                    fault("travel limit before stop");
                    return;
                }
                s_start_ms = now_ms;
                s_next_log_ms = now_ms + 500;
                s_last_sg_ms = now_ms;
                s_sg_stall = false;
                s_detected = false;
                s_detect_ms = 0;
                s_detect_travel = 0;
                homing_core::reset(&s_core, now_ms);
                s_phase = Phase::Search;
                if (!stepper_axes::move(s_axis, kHomeUpSign * s_search_cap)) {
                    fault("search restart");
                    return;
                }
            }
            break;

        case Phase::StopWait:
            if (stopped(s_axis)) {
                // One UART read now that the axis is stopped: distinguish a
                // real driver fault from a StallGuard stop.
                tmc2209::refresh(s_axis);
                if ((tmc2209::status(s_axis).drv_status & 0xFu) != 0) {
                    fault("driver fault at the stop");
                    return;
                }
                // Assert the detection-to-stop interval: commanded travel
                // after the diag, and the elapsed time (1 ms = 2 steps at
                // 2000 step/s). The force stop drains the queue.
                if (s_detected) {
                    const uint32_t elapsed_ms =
                        (now_ms >= s_start_ms)
                            ? (uint32_t)(now_ms - s_start_ms)
                            : 0;
                    const int32_t stop_travel = homing_core::travelAt(
                        elapsed_ms, s_config.speed_hz, s_config.accel_hz2,
                        s_search_cap);
                    printPrefix(s_axis);
                    Serial.print("diag->stop ms=");
                    Serial.print((uint32_t)(now_ms - s_detect_ms));
                    Serial.print(" steps=");
                    Serial.print(stop_travel - s_detect_travel);
                    Serial.print(" pin=");
                    Serial.println(s_detect_pin ? 1 : 0);
                }
                // The force stop already set position 0 at the top.
                s_home_steps = 0;
                if (!restoreNormal()) {
                    fault("restore after home");
                    return;
                }
                stepper_axes::setSpeed(s_axis, s_config.speed_hz);
                stepper_axes::setAcceleration(s_axis, s_config.accel_hz2);
                if (!stepper_axes::move(s_axis,
                                        -kHomeUpSign * s_config.backoff_steps)) {
                    fault("backoff");
                    return;
                }
                s_phase = Phase::Backoff;
            }
            break;

        case Phase::Backoff:
            if (stopped(s_axis)) {
                s_home_steps = -kHomeUpSign * s_config.backoff_steps;
                finishOk();
                startNext();
            }
            break;

        case Phase::FaultWait:
            if (stopped(s_axis)) {
                restoreNormal();
                printPrefix(s_axis);
                Serial.print("FAULT: ");
                Serial.println(s_reason);
                s_axis = 0xFF;
                s_phase = Phase::Fault;
            }
            break;

        case Phase::CancelWait:
            if (stopped(s_axis)) {
                restoreNormal();
                printPrefix(s_axis);
                Serial.println("cancelled");
                s_axis = 0xFF;
                s_phase = Phase::Idle;
            }
            break;

        default:
            break;
    }
}

void cancel() {
    s_pending_mask = 0;
    if (s_phase == Phase::Idle) {
        return;
    }
    if (s_phase == Phase::Fault) {
        s_phase = Phase::Idle;
        return;
    }
    if (!validAxis(s_axis)) {
        s_phase = Phase::Idle;
        return;
    }
    stepper_axes::stop(s_axis);
    s_phase = Phase::CancelWait;
}

bool busy() {
    return s_phase == Phase::Search || s_phase == Phase::RaiseWait ||
           s_phase == Phase::StopWait || s_phase == Phase::Backoff ||
           s_phase == Phase::FaultWait || s_phase == Phase::CancelWait;
}

bool faulted() {
    return s_phase == Phase::Fault;
}

State state() {
    switch (s_phase) {
        case Phase::Idle:
        case Phase::CancelWait:
            return State::Idle;
        case Phase::Backoff:
            return State::Backoff;
        case Phase::Fault:
        case Phase::FaultWait:
            return State::Fault;
        default:
            return State::Search;
    }
}

uint8_t activeAxis() {
    return s_axis;
}

int32_t homePositionSteps() {
    return s_home_steps;
}

void printState() {
    Serial.print("home: state=");
    switch (state()) {
        case State::Idle:
            Serial.print("idle");
            break;
        case State::Search:
            Serial.print("search");
            break;
        case State::Backoff:
            Serial.print("backoff");
            break;
        case State::Fault:
            Serial.print("fault");
            break;
    }
    Serial.print(" axis=");
    if (validAxis(s_axis)) {
        Serial.print(s_axis);
    } else {
        Serial.print("-");
    }
    Serial.print(" ma=");
    Serial.print(s_current_ma);
    Serial.print(" speed_hz=");
    Serial.print(s_config.speed_hz);
    Serial.print(" sgthrs=");
    Serial.print(s_config.sgthrs);
    Serial.print(" min_steps=");
    Serial.print(s_config.min_travel_steps);
    Serial.print(" max_steps=");
    Serial.print(s_config.max_travel_steps);
    Serial.print(" home_steps=");
    Serial.print(s_home_steps);
    if (s_reason[0] != '\0') {
        Serial.print(" reason=");
        Serial.print(s_reason);
    }
    Serial.println();
}

}  // namespace homing
