/* test_homing_core.cpp — pure homing decision logic.
 *
 * Regression: a queued FastAccelStepper move can report isRunning() == false
 * before it starts. The core therefore has no running input and faults only
 * when commanded travel reaches the cap or stops advancing entirely.
 */

#include "assert.h"
#include "homing_core.h"
#include "test_list.h"

using homing_core::Action;

static homing_core::Params params() {
    homing_core::Params p;
    p.min_travel_steps = 4000;
    p.max_travel_steps = 51200;
    p.timeout_ms = 90000;
    p.stall_hold_ms = 40;
    p.immediate_hold_ms = 500;
    p.min_free_ms = 1000;
    p.progress_timeout_ms = 2000;
    p.decel_zone_steps = 1200;
    return p;
}

static Action step(homing_core::State* s, uint32_t t, int32_t traveled,
                   bool diag, uint16_t ma = 100, uint16_t run_ma = 300) {
    return homing_core::step(params(), s, t, t, traveled, diag, ma, run_ma);
}

static int test_hc1_free_run_continues(void) {
    homing_core::State s;
    homing_core::reset(&s, 0);
    for (uint32_t t = 1; t <= 2000; ++t) {
        ASSERT(step(&s, t, (int32_t)(t * 4), false) == Action::Continue);
    }
    return 0;
}

static int test_hc2_no_progress_faults_late_not_early(void) {
    /* A move that has not started is not "ended" after 250 ms. It faults only
       after the progress timeout, and the finite move still bounds travel. */
    homing_core::State s;
    homing_core::reset(&s, 0);
    for (uint32_t t = 1; t <= 500; ++t) {
        ASSERT(step(&s, t, 0, false) == Action::Continue);
    }
    ASSERT(step(&s, 2001, 0, false) == Action::FaultNoProgress);
    return 0;
}

static int test_hc3_progress_then_stop(void) {
    homing_core::State s;
    homing_core::reset(&s, 0);
    for (uint32_t t = 1; t <= 100; ++t) {
        ASSERT(step(&s, t, (int32_t)t, false) == Action::Continue);
    }
    for (uint32_t t = 101; t <= 2100; ++t) {
        ASSERT(step(&s, t, 100, false) == Action::Continue);
    }
    ASSERT(step(&s, 2101, 100, false) == Action::FaultNoProgress);
    return 0;
}

static int test_hc4_travel_limit(void) {
    homing_core::State s;
    homing_core::reset(&s, 0);
    ASSERT(step(&s, 10, 51200, false) == Action::FaultTravel);
    return 0;
}

static int test_hc5_timeout(void) {
    homing_core::State s;
    homing_core::reset(&s, 0);
    homing_core::Params p = params();
    ASSERT(homing_core::step(p, &s, 90001, 90001, 1000, false, 100, 300) ==
           Action::FaultTimeout);
    return 0;
}

static int test_hc6_accept_after_free_and_travel(void) {
    homing_core::State s;
    homing_core::reset(&s, 0);
    for (uint32_t t = 1; t <= 1200; ++t) {
        ASSERT(step(&s, t, (int32_t)(t * 4), false) == Action::Continue);
    }
    Action a = Action::Continue;
    for (uint32_t t = 1201; t <= 1250; ++t) {
        a = step(&s, t, 5000, true);
    }
    ASSERT(a == Action::Accept);
    return 0;
}

static int test_hc7_short_diag_ignored(void) {
    homing_core::State s;
    homing_core::reset(&s, 0);
    for (uint32_t t = 1; t <= 1200; ++t) {
        ASSERT(step(&s, t, 5000, false) == Action::Continue);
    }
    for (uint32_t t = 1201; t <= 1220; ++t) {
        ASSERT(step(&s, t, 5000, true) == Action::Continue);
    }
    ASSERT(step(&s, 1221, 5000, false) == Action::Continue);
    return 0;
}

static int test_hc8_early_diag_raises_current(void) {
    homing_core::State s;
    homing_core::reset(&s, 0);
    Action a = Action::Continue;
    for (uint32_t t = 1; t <= 600; ++t) {
        a = step(&s, t, 0, true, 100, 300);
    }
    ASSERT(a == Action::RaiseCurrent);
    ASSERT(s.free_ms == 0);
    return 0;
}

static int test_hc9_early_diag_at_run_current_faults(void) {
    homing_core::State s;
    homing_core::reset(&s, 0);
    Action a = Action::Continue;
    for (uint32_t t = 1; t <= 600; ++t) {
        a = step(&s, t, 0, true, 300, 300);
    }
    ASSERT(a == Action::FaultEarly);
    return 0;
}

static int test_hc10_free_time_only_while_diag_low(void) {
    homing_core::State s;
    homing_core::reset(&s, 0);
    for (uint32_t t = 1; t <= 300; ++t) {
        ASSERT(step(&s, t, 5000, false) == Action::Continue);
    }
    ASSERT(s.free_ms >= 299);
    const uint32_t free_before = s.free_ms;
    for (uint32_t t = 301; t <= 340; ++t) {
        (void)step(&s, t, 5000, true);
    }
    ASSERT(s.free_ms == free_before);
    return 0;
}

static int test_hc11_brief_stop_is_not_a_fault(void) {
    homing_core::State s;
    homing_core::reset(&s, 0);
    ASSERT(step(&s, 1, 1000, false) == Action::Continue);
    ASSERT(step(&s, 1000, 1000, false) == Action::Continue);
    ASSERT(step(&s, 1500, 1001, false) == Action::Continue);
    return 0;
}

static int test_hc12_decel_zone_ignores_diag(void) {
    homing_core::State s;
    homing_core::reset(&s, 0);
    for (uint32_t t = 1; t <= 1200; ++t) {
        ASSERT(step(&s, t, (int32_t)(t * 4), false) == Action::Continue);
    }
    /* max 51200 - 1200 = 50000; 51000 is inside the deceleration tail. */
    Action a = Action::Continue;
    for (uint32_t t = 1201; t <= 1300; ++t) {
        a = step(&s, t, 51000, true);
    }
    ASSERT(a == Action::Continue);
    return 0;
}

static int reg_hc1 = add_test("HC1", test_hc1_free_run_continues);
static int reg_hc2 = add_test("HC2", test_hc2_no_progress_faults_late_not_early);
static int reg_hc3 = add_test("HC3", test_hc3_progress_then_stop);
static int reg_hc4 = add_test("HC4", test_hc4_travel_limit);
static int reg_hc5 = add_test("HC5", test_hc5_timeout);
static int reg_hc6 = add_test("HC6", test_hc6_accept_after_free_and_travel);
static int reg_hc7 = add_test("HC7", test_hc7_short_diag_ignored);
static int reg_hc8 = add_test("HC8", test_hc8_early_diag_raises_current);
static int reg_hc9 = add_test("HC9", test_hc9_early_diag_at_run_current_faults);
static int reg_hc10 = add_test("HC10", test_hc10_free_time_only_while_diag_low);
static int reg_hc11 = add_test("HC11", test_hc11_brief_stop_is_not_a_fault);
static int reg_hc12 = add_test("HC12", test_hc12_decel_zone_ignores_diag);

static int test_hc13_travel_at(void) {
    /* 1000 step/s, 500 step/s^2: 2 s to speed, 1000 steps during the ramp. */
    ASSERT(homing_core::travelAt(0, 1000, 500, 59200) == 0);
    ASSERT(homing_core::travelAt(1000, 1000, 500, 59200) == 250);
    ASSERT(homing_core::travelAt(2000, 1000, 500, 59200) == 1000);
    ASSERT(homing_core::travelAt(3000, 1000, 500, 59200) == 2000);
    ASSERT(homing_core::travelAt(600000, 1000, 500, 59200) == 59200);
    ASSERT(homing_core::travelAt(1000, 1000, 500, 100) == 100);
    ASSERT(homing_core::travelAt(1000, 0, 500, 59200) == 0);
    return 0;
}

static int reg_hc13 = add_test("HC13", test_hc13_travel_at);
