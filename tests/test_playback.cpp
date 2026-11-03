/* test_playback.cpp — §6 items C1–C10 of motion_streaming_tdd.md.
 *
 * Contract: motion_streaming_whitepaper.md §4.2 and §4.7.  The node
 * evaluates one cubic polynomial per message:
 *
 *   p(τ) = p0 + v0·τ + a0·τ²/2 + j·τ³/6
 *
 * with τ in microseconds and integer Horner arithmetic.  No float is used
 * in the core or in the reference implementation below.
 */

#include "assert.h"
#include "motion_core.h"
#include "test_list.h"

#include <stdio.h>

/* Independent integer Horner reference (C3, C10). */
static int64_t ref_eval_um(const mc_segment_t *s, int64_t tau_us) {
    int64_t p = mc_code_to_um(s->p0);
    int64_t v = (int64_t)s->v0 * 40;
    int64_t a = (int64_t)s->a0 * 1000;
    int64_t j = (int64_t)s->j * 1000000;

    int64_t x = j * tau_us / 6 / 1000000 + a / 2;
    x = x * tau_us / 1000000 + v;
    x = x * tau_us / 1000000 + p;
    return x;
}

/* C1 — p0, v0=0, a0=0, j=0 returns p0 at every τ inside the segment. */
static int test_c1_constant_position(void) {
    mc_core_reset();
    mc_segment_t s = {mc_um_to_code(1234567), 0, 0, 0};
    int32_t p0 = mc_code_to_um(s.p0);
    const int64_t t0 = 100000;

    ASSERT(mc_trajectory_push(0, 0, t0, 30000, &s) == 0);
    ASSERT(mc_eval(0, t0) == p0);
    ASSERT(mc_eval(0, t0 + 1) == p0);
    ASSERT(mc_eval(0, t0 + 15000) == p0);
    ASSERT(mc_eval(0, t0 + 30000) == p0);
    return 0;
}

/* C2 — constant speed: p(τ) = p0 + v0·τ.  One speed LSB (40 µm/s) over
 * 30 ms is 1.2 µm, truncated to 1 µm by the integer form. */
static int test_c2_constant_speed(void) {
    mc_core_reset();
    mc_segment_t s = {mc_um_to_code(500000), 1, 0, 0};
    int32_t p0 = mc_code_to_um(s.p0);

    ASSERT(mc_trajectory_push(0, 0, 0, 1000000, &s) == 0);
    ASSERT(mc_eval(0, 0) == p0);
    ASSERT(mc_eval(0, 30000) == p0 + 1);
    ASSERT(mc_eval(0, 1000000) == p0 + 40);
    return 0;
}

/* C3 — constant acceleration and jerk match the cubic at τ = 0, 2 ms and
 * Tseg, using the same divide-before-multiply order in int64_t. */
static int test_c3_polynomial(void) {
    mc_core_reset();
    mc_segment_t s = {mc_um_to_code(1000000), 123, 45, 7};
    const int64_t tseg = 30000;

    ASSERT(mc_trajectory_push(0, 0, 0, tseg, &s) == 0);

    const int64_t taus[3] = {0, 2000, tseg};
    for (int i = 0; i < 3; ++i) {
        int64_t expect = ref_eval_um(&s, taus[i]);
        ASSERT(mc_eval(0, taus[i]) == (int32_t)expect);
    }
    return 0;
}

/* C4 — evaluation is a pure function of the timeline.  A step-counter
 * change does not alter the result. */
static int test_c4_pure_evaluation(void) {
    mc_core_reset();
    mc_segment_t s = {mc_um_to_code(700000), 500, -20, 3};
    int16_t steps[4] = {0, 0, 0, 0};
    uint16_t ticks = 0;

    ASSERT(mc_trajectory_push(0, 0, 0, 30000, &s) == 0);
    int32_t first = mc_eval(0, 10000);
    ASSERT(mc_eval(0, 10000) == first);

    ASSERT(mc_step_chunk(steps, &ticks, 2000, 2000) == 0);
    ASSERT(ticks == 32000);
    ASSERT(mc_eval(0, 10000) == first);
    return 0;
}

/* C5 — one message is a complete segment; no priming message is needed.
 * Type 1 is reserved and rejected. */
static int test_c5_one_message_segment(void) {
    mc_core_reset();
    mc_segment_t s = {mc_um_to_code(100000), 100, 0, 0};

    ASSERT(mc_trajectory_push(0, 1, 0, 30000, &s) == -1);
    ASSERT(mc_axis_state(0) == MC_AXIS_IDLE);

    ASSERT(mc_trajectory_push(0, 0, 0, 30000, &s) == 0);
    ASSERT(mc_axis_state(0) == MC_AXIS_PLAYING);
    ASSERT(mc_eval(0, 0) == mc_code_to_um(s.p0));
    return 0;
}

/* C6 — while playing, the next p0 and v0 must match the previous segment's
 * end position and speed within one LSB.  A larger gap holds and faults. */
static int test_c6_join_match(void) {
    mc_core_reset();
    /* End = p0 + 40 000 µm/s × 10 ms = p0 + 400 µm. */
    uint16_t code_a = mc_um_to_code(1000000);
    int32_t p0_a = mc_code_to_um(code_a);
    int32_t end_a = p0_a + 400;
    mc_segment_t a = {code_a, 1000, 0, 0};
    ASSERT(mc_trajectory_push(0, 0, 0, 10000, &a) == 0);
    ASSERT(mc_eval(0, 10000) == end_a);

    uint16_t code_b = mc_um_to_code(end_a);
    int32_t end_b = mc_code_to_um(code_b) + 400;
    mc_segment_t b = {code_b, 1000, 0, 0};
    ASSERT(mc_trajectory_push(0, 0, 10000, 10000, &b) == 0);
    ASSERT(mc_axis_state(0) == MC_AXIS_PLAYING);

    mc_segment_t c = {(uint16_t)(mc_um_to_code(end_b) + 2), 1000, 0, 0};
    ASSERT(mc_trajectory_push(0, 0, 20000, 10000, &c) == -1);
    ASSERT(mc_axis_state(0) == MC_AXIS_HOLD);
    ASSERT((mc_axis_fault_flags(0) & MC_FAULT_JOIN) != 0);
    ASSERT(mc_axis_held_um(0) == end_b);
    return 0;
}

/* C7 — after a hold, a segment whose p0 matches the held position within
 * one position code is accepted. */
static int test_c7_rejoin_after_hold(void) {
    mc_core_reset();
    mc_segment_t a = {mc_um_to_code(1000000), 1000, 0, 0};
    ASSERT(mc_trajectory_push(0, 0, 0, 10000, &a) == 0);

    mc_segment_t gap = {mc_um_to_code(1000800), 1000, 0, 0};
    ASSERT(mc_trajectory_push(0, 0, 10000, 10000, &gap) == -1);
    ASSERT(mc_axis_state(0) == MC_AXIS_HOLD);

    int32_t held = mc_axis_held_um(0);
    uint16_t held_code = mc_um_to_code(held);
    mc_segment_t resume = {held_code, 0, 0, 0};
    ASSERT(mc_trajectory_push(0, 0, 20000, 10000, &resume) == 0);
    ASSERT(mc_axis_state(0) == MC_AXIS_PLAYING);
    ASSERT(mc_eval(0, 20000) == mc_code_to_um(held_code));
    return 0;
}

/* C8 — a segment with acceleration below −9.81 m/s² at either endpoint is
 * rejected; the stored position is unchanged. */
static int test_c8_acceleration_floor(void) {
    mc_core_reset();
    mc_segment_t good = {mc_um_to_code(1000000), 0, 0, 0};
    ASSERT(mc_trajectory_push(0, 0, 0, 10000, &good) == 0);
    int32_t held = mc_axis_held_um(0);

    /* −9 811 mm/s² = −9.811 m/s² */
    mc_segment_t bad_start = {(uint16_t)mc_um_to_code(held), 0, -9811, 0};
    ASSERT(mc_trajectory_push(0, 0, 10000, 10000, &bad_start) == -1);
    ASSERT((mc_axis_fault_flags(0) & MC_FAULT_ACCEL) != 0);
    ASSERT(mc_axis_held_um(0) == held);

    /* Endpoint acceleration: a0=0, j<0 over 30 ms drives a(Tseg) below g. */
    mc_segment_t bad_end = {(uint16_t)mc_um_to_code(held), 0, 0, -32767};
    ASSERT(mc_trajectory_push(0, 0, 10000, 30000, &bad_end) == -1);
    ASSERT((mc_axis_fault_flags(0) & MC_FAULT_ACCEL) != 0);
    ASSERT(mc_axis_held_um(0) == held);

    /* −9.810 m/s² exactly is allowed after a hold. */
    mc_segment_t edge = {(uint16_t)mc_um_to_code(held), 0, -9810, 0};
    ASSERT(mc_trajectory_push(0, 0, 10000, 1000, &edge) == 0);
    return 0;
}

/* C9 — p(Tseg) outside 0–2 000 000 µm is rejected. */
static int test_c9_end_out_of_range(void) {
    mc_core_reset();
    mc_segment_t high = {mc_um_to_code(1999000), 32767, 0, 0};
    ASSERT(mc_trajectory_push(0, 0, 0, 1000, &high) == -1);
    ASSERT((mc_axis_fault_flags(0) & MC_FAULT_RANGE) != 0);

    mc_core_reset();
    mc_segment_t low = {mc_um_to_code(1000), -32767, 0, 0};
    ASSERT(mc_trajectory_push(0, 0, 0, 1000, &low) == -1);
    ASSERT((mc_axis_fault_flags(0) & MC_FAULT_RANGE) != 0);
    return 0;
}

/* C10 — property: 100 random int16 coefficient sets, seed 0x12345678.
 * Every accepted segment matches the independent Horner reference at 20
 * times.  No float in either implementation. */
static uint32_t g_xs_state;

static uint32_t xorshift32(void) {
    uint32_t x = g_xs_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g_xs_state = x;
    return x;
}

static int test_c10_property(void) {
    const uint32_t seed = 0x12345678u;
    const int64_t tseg = 20000;
    int accepted = 0;

    g_xs_state = seed;

    for (int k = 0; k < 100; ++k) {
        mc_segment_t s;
        s.p0 = (uint16_t)(xorshift32() & 0xFFFFu);
        s.v0 = (int16_t)xorshift32();
        s.a0 = (int16_t)xorshift32();
        s.j  = (int16_t)xorshift32();

        mc_core_reset();
        if (mc_trajectory_push(0, 0, 0, tseg, &s) != 0)
            continue;
        ++accepted;

        for (int q = 0; q < 20; ++q) {
            int64_t tau = tseg * q / 19;
            int32_t got = mc_eval(0, tau);
            int64_t expect = ref_eval_um(&s, tau);
            if (got != (int32_t)expect) {
                fprintf(stderr,
                        "C10 seed=0x%08X case=%d tau=%lld "
                        "p0=%u v0=%d a0=%d j=%d got=%d expect=%lld\n",
                        seed, k, (long long)tau, (unsigned)s.p0,
                        (int)s.v0, (int)s.a0, (int)s.j, (int)got,
                        (long long)expect);
                return 1;
            }
        }
    }

    ASSERT(accepted > 0);
    return 0;
}

static int reg_c1 = add_test("C1", test_c1_constant_position);
static int reg_c2 = add_test("C2", test_c2_constant_speed);
static int reg_c3 = add_test("C3", test_c3_polynomial);
static int reg_c4 = add_test("C4", test_c4_pure_evaluation);
static int reg_c5 = add_test("C5", test_c5_one_message_segment);
static int reg_c6 = add_test("C6", test_c6_join_match);
static int reg_c7 = add_test("C7", test_c7_rejoin_after_hold);
static int reg_c8 = add_test("C8", test_c8_acceleration_floor);
static int reg_c9 = add_test("C9", test_c9_end_out_of_range);
static int reg_c10 = add_test("C10", test_c10_property);
