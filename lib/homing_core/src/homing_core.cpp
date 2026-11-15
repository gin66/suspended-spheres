#include "homing_core.h"

namespace homing_core {

void reset(State* state, uint32_t now_ms) {
    state->last_ms = now_ms;
    state->diag_since_ms = 0;
    state->free_ms = 0;
    state->last_progress_ms = now_ms;
    state->last_traveled = 0;
    state->diag_seen = false;
}

Action step(const Params& p, State* s, uint32_t now_ms, uint32_t elapsed_ms,
            int32_t traveled, bool diag, uint16_t current_ma, uint16_t run_ma) {
    if (traveled >= p.max_travel_steps) {
        return Action::FaultTravel;
    }
    if (elapsed_ms > p.timeout_ms) {
        return Action::FaultTimeout;
    }

    if (traveled > s->last_traveled) {
        s->last_traveled = traveled;
        s->last_progress_ms = now_ms;
    } else if ((uint32_t)(now_ms - s->last_progress_ms) >
               p.progress_timeout_ms) {
        return Action::FaultNoProgress;
    }

    const uint32_t dt_ms = now_ms - s->last_ms;
    s->last_ms = now_ms;

    // In the deceleration tail of the finite move the StallGuard signal is not
    // a valid reference; the move is about to stop on its own.
    const bool usable_diag =
        diag && traveled <= p.max_travel_steps - p.decel_zone_steps;
    if (!usable_diag) {
        if (s->free_ms < 10000u) {
            s->free_ms += dt_ms;
        }
        s->diag_seen = false;
        return Action::Continue;
    }
    if (!s->diag_seen) {
        s->diag_seen = true;
        s->diag_since_ms = now_ms;
    }

    if ((uint32_t)(now_ms - s->diag_since_ms) < p.stall_hold_ms) {
        return Action::Continue;
    }
    if (traveled >= p.min_travel_steps && s->free_ms >= p.min_free_ms) {
        return Action::Accept;
    }
    if ((uint32_t)(now_ms - s->diag_since_ms) < p.immediate_hold_ms) {
        return Action::Continue;
    }
    if (current_ma < run_ma) {
        return Action::RaiseCurrent;
    }
    return Action::FaultEarly;
}

int32_t travelAt(uint32_t elapsed_ms, uint32_t speed_hz, uint32_t accel_hz2,
                 int32_t cap) {
    if (cap <= 0 || speed_hz == 0 || accel_hz2 == 0) {
        return 0;
    }
    const uint32_t t_acc_ms =
        (uint32_t)((uint64_t)speed_hz * 1000u / accel_hz2);
    int32_t d;
    if (elapsed_ms < t_acc_ms) {
        d = (int32_t)((uint64_t)accel_hz2 * elapsed_ms * elapsed_ms /
                      2000000u);
    } else {
        const int32_t d_acc =
            (int32_t)((uint64_t)accel_hz2 * t_acc_ms * t_acc_ms / 2000000u);
        d = d_acc + (int32_t)((uint64_t)speed_hz *
                              (elapsed_ms - t_acc_ms) / 1000u);
    }
    if (d > cap) {
        d = cap;
    }
    return d;
}

}  // namespace homing_core
