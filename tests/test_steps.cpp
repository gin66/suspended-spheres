/* test_steps.cpp — §7 items S1–S10 of motion_streaming_tdd.md.
 *
 * Contract: motion_streaming_whitepaper.md §4.7, §5.3, §5.4 and
 * Appendix A.  Steps/mm is a Q16.16 integer; the fractional remainder is
 * kept per axis so a chunk sum does not drift.
 */

#include "assert.h"
#include "motion_core.h"
#include "test_list.h"

#include <stdlib.h>

static int32_t abs_i32(int32_t v) {
    return v < 0 ? -v : v;
}

static mc_segment_t speed_segment(uint16_t p0_code, int16_t v0_code) {
    mc_segment_t s = {p0_code, v0_code, 0, 0};
    return s;
}

/* S1 — the Q16.16 constant represents 3200 / (π × 40 mm) = 25.4648
 * steps/mm.  Full-scale 2 000 mm is within one step of round(...). */
static int test_s1_steps_per_mm(void) {
    double steps_per_mm = mc_steps_per_mm_q16() / 65536.0;
    ASSERT(steps_per_mm > 25.4647 && steps_per_mm < 25.4649);

    int32_t net = mc_um_to_steps(2000000);
    int32_t expect = 50930;   /* round(2 000 × 25.4648) */
    ASSERT(abs_i32(net - expect) <= 1);
    return 0;
}

/* S2 — the per-axis accumulator makes the chunk sum equal the net step
 * count across 2 000 mm.  Rounding each chunk on its own drifts further. */
static int test_s2_no_drift(void) {
    mc_core_reset();
    mc_segment_t s = speed_segment(0, 30000);   /* 1.2 m/s */
    const int64_t tseg = 1666666;               /* ≈ 1.999999 m */
    ASSERT(mc_trajectory_push(0, 0, 0, tseg, &s) == 0);

    int16_t steps[4];
    uint16_t ticks = 0;
    int64_t sum = 0;
    int64_t control = 0;
    int64_t t = 0;

    while (t + 2000 <= tseg) {
        ASSERT(mc_step_chunk(steps, &ticks, t + 2000, 2000) == 0);
        ASSERT(ticks == 32000);
        sum += steps[0];
        control += mc_um_to_steps(2400);   /* independently rounded */
        t += 2000;
    }
    if (t < tseg) {
        int32_t left_us = (int32_t)(tseg - t);
        ASSERT(mc_step_chunk(steps, &ticks, tseg, left_us) == 0);
        sum += steps[0];
        control += mc_um_to_steps((int32_t)((1200000LL * left_us) / 1000000));
    }

    int32_t net = mc_um_to_steps((int32_t)((1200000LL * tseg) / 1000000));
    ASSERT(sum == net);
    ASSERT(abs_i32((int32_t)(sum - control)) > 1);
    return 0;
}

/* S3 — 2 ms chunks at 1.2 m/s and 0.6 m/s are 61 and 31 steps. */
static int test_s3_chunk_steps(void) {
    int16_t steps[4];
    uint16_t ticks = 0;

    mc_core_reset();
    mc_segment_t fast = speed_segment(0, 30000);
    ASSERT(mc_trajectory_push(0, 0, 0, 100000, &fast) == 0);
    ASSERT(mc_step_chunk(steps, &ticks, 2000, 2000) == 0);
    ASSERT(steps[0] == 61);
    ASSERT(abs_i32(steps[0]) <= 128);

    mc_core_reset();
    mc_segment_t half = speed_segment(0, 15000);
    ASSERT(mc_trajectory_push(0, 0, 0, 100000, &half) == 0);
    ASSERT(mc_step_chunk(steps, &ticks, 2000, 2000) == 0);
    ASSERT(steps[0] == 31);
    return 0;
}

/* S4 — a 5 ms or 10 ms chunk at 1.2 m/s is rejected; 2 ms is accepted. */
static int test_s4_chunk_too_long(void) {
    int16_t steps[4];
    uint16_t ticks = 0;
    mc_segment_t fast = speed_segment(0, 30000);

    mc_core_reset();
    ASSERT(mc_trajectory_push(0, 0, 0, 100000, &fast) == 0);
    ASSERT(mc_step_chunk(steps, &ticks, 5000, 5000) == -1);
    ASSERT(mc_step_chunk(steps, &ticks, 10000, 10000) == -1);
    ASSERT(mc_step_chunk(steps, &ticks, 2000, 2000) == 0);
    ASSERT(abs_i32(steps[0]) <= 128);
    return 0;
}

/* S5 — 2 ms at 16 MHz is 32 000 ticks.  Ticks stay in [3 200, 65 535]; a
 * chunk shorter than 200 µs or longer than 4.096 ms is rejected. */
static int test_s5_tick_limits(void) {
    int16_t steps[4];
    uint16_t ticks = 0;
    mc_segment_t fast = speed_segment(0, 30000);

    mc_core_reset();
    ASSERT(mc_trajectory_push(0, 0, 0, 100000, &fast) == 0);
    ASSERT(mc_step_chunk(steps, &ticks, 2000, 2000) == 0);
    ASSERT(ticks == 32000);

    mc_core_reset();
    ASSERT(mc_trajectory_push(0, 0, 0, 100000, &fast) == 0);
    ASSERT(mc_step_chunk(steps, &ticks, 199, 199) == -1);

    mc_core_reset();
    ASSERT(mc_trajectory_push(0, 0, 0, 100000, &fast) == 0);
    ASSERT(mc_step_chunk(steps, &ticks, 4096, 4096) == -1);

    mc_core_reset();
    ASSERT(mc_trajectory_push(0, 0, 0, 100000, &fast) == 0);
    ASSERT(mc_step_chunk(steps, &ticks, 4095, 4095) == 0);
    ASSERT(ticks >= 3200 && ticks <= 65535);
    return 0;
}

/* S6 — Δt of 11 ms and 15 ms does not move the 2 ms chunk boundary; the
 * evaluator is queried at 2 ms, and the same tick returns the same steps
 * regardless of the segment length. */
static int test_s6_two_ms_grid(void) {
    int16_t a[4], b[4];
    uint16_t ticks = 0;
    mc_segment_t s = speed_segment(0, 15000);

    mc_core_reset();
    ASSERT(mc_trajectory_push(0, 0, 0, 33000, &s) == 0);   /* N=3, Δt=11 */
    ASSERT(mc_step_chunk(a, &ticks, 2000, 2000) == 0);

    mc_core_reset();
    ASSERT(mc_trajectory_push(0, 0, 0, 45000, &s) == 0);   /* N=3, Δt=15 */
    ASSERT(mc_step_chunk(b, &ticks, 2000, 2000) == 0);

    ASSERT(a[0] == b[0]);
    ASSERT(a[0] == 31);   /* 0.6 m/s at the first 2 ms boundary */
    return 0;
}

/* S7 — four axes share one tick count; axis 0 reversing does not change
 * the step delta of axes 1–3. */
static int test_s7_shared_ticks_independent_axes(void) {
    int16_t steps[4];
    uint16_t ticks = 0;

    mc_core_reset();
    uint16_t mid = mc_um_to_code(1000000);
    mc_segment_t down = speed_segment(mid, -30000);
    mc_segment_t up1 = speed_segment(mid, 30000);
    mc_segment_t up2 = speed_segment(mid, 15000);
    mc_segment_t up3 = speed_segment(mid, 10000);
    ASSERT(mc_trajectory_push(0, 0, 0, 100000, &down) == 0);
    ASSERT(mc_trajectory_push(1, 0, 0, 100000, &up1) == 0);
    ASSERT(mc_trajectory_push(2, 0, 0, 100000, &up2) == 0);
    ASSERT(mc_trajectory_push(3, 0, 0, 100000, &up3) == 0);

    ASSERT(mc_step_chunk(steps, &ticks, 2000, 2000) == 0);
    ASSERT(ticks == 32000);

    ASSERT(steps[0] == -61);
    ASSERT(steps[1] == 61);
    ASSERT(steps[2] == 31);
    ASSERT(steps[3] == 21);   /* 0.4 m/s, plus the p0 rounding phase */
    return 0;
}

/* S8 — mc_step_chunk returns per-axis steps and the tick count for a 2 ms
 * boundary; steps match S3 for constant-speed segments. */
static int test_s8_chunk_result(void) {
    int16_t steps[4] = {99, 99, 99, 99};
    uint16_t ticks = 0;

    mc_core_reset();
    mc_segment_t fast = speed_segment(0, 30000);
    mc_segment_t half = speed_segment(0, 15000);
    ASSERT(mc_trajectory_push(0, 0, 0, 100000, &fast) == 0);
    ASSERT(mc_trajectory_push(1, 0, 0, 100000, &half) == 0);

    ASSERT(mc_step_chunk(steps, &ticks, 2000, 2000) == 0);
    ASSERT(ticks == 32000);
    ASSERT(steps[0] == 61);
    ASSERT(steps[1] == 31);
    ASSERT(steps[2] == 0);
    ASSERT(steps[3] == 0);
    return 0;
}

/* S9/S10 feeder double: a queue of (steps, ticks) with horizon 8. */
struct ChunkFeeder {
    static const int HORIZON = 8;
    int count;
    bool underrun;
    int16_t steps[HORIZON];
    uint16_t ticks[HORIZON];

    int add(int16_t s, uint16_t t) {
        if (count >= HORIZON)
            return 0;
        steps[count] = s;
        ticks[count] = t;
        ++count;
        return 1;
    }

    int pump(void) {
        if (count <= 0)
            return 0;
        --count;
        return 1;
    }

    void mark_underrun(void) { underrun = true; }
    void clear_underrun(void) { underrun = false; }
    bool reported_fault(void) const { return underrun; }
};

/* S9 — an underrun, once set, stays set until an explicit clear and is
 * reported as a fault. */
static int test_s9_latched_underrun(void) {
    ChunkFeeder f = {0, false, {0}, {0}};
    ASSERT(f.reported_fault() == false);
    f.mark_underrun();
    ASSERT(f.underrun == true);
    ASSERT(f.reported_fault() == true);
    ASSERT(f.pump() == 0);              /* pumping does not clear it */
    ASSERT(f.reported_fault() == true);
    f.clear_underrun();
    ASSERT(f.reported_fault() == false);
    return 0;
}

/* S10 — add is rejected when the horizon is full; after pump frees a slot
 * add succeeds. */
static int test_s10_horizon(void) {
    ChunkFeeder f = {0, false, {0}, {0}};
    for (int i = 0; i < ChunkFeeder::HORIZON; ++i)
        ASSERT(f.add((int16_t)i, (uint16_t)(32000 + i)) == 1);
    ASSERT(f.count == ChunkFeeder::HORIZON);
    ASSERT(f.add(99, 32000) == 0);
    ASSERT(f.pump() == 1);
    ASSERT(f.count == ChunkFeeder::HORIZON - 1);
    ASSERT(f.add(99, 32000) == 1);
    ASSERT(f.count == ChunkFeeder::HORIZON);
    return 0;
}

static int reg_s1 = add_test("S1", test_s1_steps_per_mm);
static int reg_s2 = add_test("S2", test_s2_no_drift);
static int reg_s3 = add_test("S3", test_s3_chunk_steps);
static int reg_s4 = add_test("S4", test_s4_chunk_too_long);
static int reg_s5 = add_test("S5", test_s5_tick_limits);
static int reg_s6 = add_test("S6", test_s6_two_ms_grid);
static int reg_s7 = add_test("S7", test_s7_shared_ticks_independent_axes);
static int reg_s8 = add_test("S8", test_s8_chunk_result);
static int reg_s9 = add_test("S9", test_s9_latched_underrun);
static int reg_s10 = add_test("S10", test_s10_horizon);
