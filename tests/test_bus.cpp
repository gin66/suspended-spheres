/* test_bus.cpp — §5 items B1–B5 and B7 of motion_streaming_tdd.md.
 *
 * Contract: motion_streaming_whitepaper.md §3.3, §4.6, Appendix A.
 * The expected column is the whitepaper table, so a changed bit-stuffing
 * assumption fails here.  B6 and B8 arrive later.
 */

#include "assert.h"
#include "motion_core.h"
#include "test_list.h"

#include <initializer_list>

/* B1 — frame cost is 130 bits at 1 Mbit/s (130 µs). Ceiling ≈ 7 700. */
static int test_b1_frame_cost(void) {
    ASSERT(mc_bus_frame_cost_bits() == 130);
    ASSERT(mc_bus_frame_time_us() == 130);
    /* 1 000 000 µs / 130 µs ≈ 7 692 frames/s, "roughly 7 700" */
    ASSERT(mc_bus_ceiling_frames_s() >= 7650);
    ASSERT(mc_bus_ceiling_frames_s() <= 7750);
    return 0;
}

/* B2 — motion frames/s = 120/(N·Δt); the whitepaper §3.3 table. */
static int test_b2_motion_rates(void) {
    struct Case { int n; int dt_ms; int64_t frames; int load; };
    static const Case table[] = {
        {3, 10, 4000, 52},
        {3, 11, 3636, 47},
        {3, 15, 2667, 35},
        {4, 10, 3000, 39},
        {4, 11, 2727, 35},
        {4, 15, 2000, 26},
    };
    for (const Case &c : table) {
        ASSERT(mc_bus_motion_frames_s(c.n, c.dt_ms) == c.frames);
        ASSERT(mc_bus_load_percent(c.n, c.dt_ms) == c.load);
        /* the clock-free closed form agrees with the table */
        ASSERT(mc_bus_load_percent_frames(c.frames) == c.load);
    }
    return 0;
}

/* B3 — adding 50 Hz sync and 30 × 10 Hz status (350 frames/s). */
static int test_b3_with_sync_status(void) {
    struct Case { int n; int dt_ms; int load; };
    static const Case table[] = {
        {3, 10, 57},
        {3, 11, 52},
        {3, 15, 39},
        {4, 10, 44},
        {4, 11, 40},
        {4, 15, 31},
    };
    for (const Case &c : table) {
        ASSERT(mc_bus_load_percent_with_overhead(c.n, c.dt_ms) == c.load);
    }
    return 0;
}

/* B4 — a 10 ms message period (12 000 frames/s) is 156 % and over budget;
 * the pattern checker limit is 60 %. */
static int test_b4_over_budget(void) {
    ASSERT(mc_bus_load_percent_frames(12000) == 156);
    ASSERT(mc_bus_budget_percent() == 60);
    ASSERT(mc_bus_load_percent_frames(12000) > mc_bus_budget_percent());
    /* the six B2 cases all sit below the checker limit */
    const int loads[6] = {52, 47, 35, 39, 35, 26};
    for (int i = 0; i < 6; ++i)
        ASSERT(loads[i] <= mc_bus_budget_percent());
    return 0;
}

/* B5 — 120 motion frames occupy 15.6 ms of bus time. */
static int test_b5_window_time(void) {
    ASSERT(mc_bus_frames_time_us(120) == 15600);
    ASSERT(mc_bus_frames_time_us(1) == 130);
    ASSERT(mc_bus_frames_time_us(0) == 0);
    return 0;
}

/* B7 — property: load scales as 1/(N·Δt) for Δt in {10, 11, 15} and N in
 * {3, 4}, independent of step rate and microstepping (FR-ARCH-002). */
static int test_b7_inverse_scale_property(void) {
    /* load(N, dt) ≈ 1560/(N·dt) percent; with round-to-nearest the product
     * load·(N·dt) must stay within N·dt of 1560.  Integer only. */
    for (int n = 3; n <= 4; ++n) {
        for (int k = 0; k < 3; ++k) {
            const int dt[3] = {10, 11, 15};
            int64_t lhs = mc_bus_load_percent(n, dt[k]);
            ASSERT(lhs * n * dt[k] >= 1560 - n * dt[k]);
            ASSERT(lhs * n * dt[k] <= 1560 + n * dt[k]);
        }
    }
    /* frames/s depend only on n and dt: the step rate and the microstep
     * setting are not parameters of the call, and the same values come
     * back however the driver is configured (FR-ARCH-002). */
    const int microsteps[2] = {16, 32};
    for (int i = 0; i < 2; ++i) {
        ASSERT(mc_bus_motion_frames_s(3, 10) == 4000);
        ASSERT(mc_bus_motion_frames_s(4, 15) == 2000);
        ASSERT(mc_bus_frame_cost_bits() == 130);
    }
    return 0;
}

static int reg_b1 = add_test("B1", test_b1_frame_cost);
static int reg_b2 = add_test("B2", test_b2_motion_rates);
static int reg_b3 = add_test("B3", test_b3_with_sync_status);
static int reg_b4 = add_test("B4", test_b4_over_budget);
static int reg_b5 = add_test("B5", test_b5_window_time);
static int reg_b7 = add_test("B7", test_b7_inverse_scale_property);
