/* test_node.cpp — §8 items N1–N14 of motion_streaming_tdd.md.
 *
 * One simulated node with four axes, a two-segment queue, a receive
 * ring, and the playback clock.  No hardware and no operating-system
 * clock are used.
 */

#include "assert.h"
#include "motion_core.h"
#include "node_sim.h"
#include "test_list.h"

#include <stdio.h>
#include <stdlib.h>

static mc_segment_t const_speed(uint16_t p0_code, int16_t v0_code) {
    mc_segment_t s = {p0_code, v0_code, 0, 0};
    return s;
}

static int32_t abs_i32(int32_t v) {
    return v < 0 ? -v : v;
}

/* N1 — active and next segment; evaluation follows the sync timeline, not
 * the arrival order. */
static int test_n1_timeline_not_arrival(void) {
    NodeSim n;
    n.set_board(0);
    n.sync(0);

    mc_segment_t a = const_speed(mc_um_to_code(1000000), 15000);
    ASSERT(n.push(0, 0, 0, 30000, &a) == 0);

    /* The next segment arrives early, but must not play before its start. */
    int32_t end_a = mc_segment_eval_um(&a, 30000);
    mc_segment_t b = const_speed((uint16_t)mc_um_to_code(end_a), 15000);
    ASSERT(n.push(0, 0, 30000, 30000, &b) == 0);

    ASSERT(n.eval(0, 15000) == mc_segment_eval_um(&a, 15000));
    ASSERT(n.eval(0, 29999) == mc_segment_eval_um(&a, 29999));
    ASSERT(n.eval(0, 30000) == mc_segment_eval_um(&b, 0));
    ASSERT(n.eval(0, 45000) == mc_segment_eval_um(&b, 15000));
    return 0;
}

/* N2 — a missing segment decelerates to rest and holds; it does not jump. */
static int test_n2_missing_segment(void) {
    NodeSim n;
    n.set_board(0);
    n.sync(0);

    mc_segment_t a = const_speed(mc_um_to_code(1000000), 30000); /* 1.2 m/s */
    ASSERT(n.push(0, 0, 0, 30000, &a) == 0);
    int32_t end_a = mc_segment_eval_um(&a, 30000);

    int32_t prev = n.eval(0, 0);
    for (int64_t t = 2000; t <= 1200000; t += 2000) {
        int32_t p = n.eval(0, t);
        ASSERT(p >= 0 && p <= 2000000);
        ASSERT(abs_i32(p - prev) < 5000);   /* no 36 mm jump */
        prev = p;
    }
    ASSERT(n.eval(0, 30000) == end_a);
    ASSERT(n.holding(0));
    ASSERT((n.fault(0) & MC_STATUS_FAULT_SAMPLE_TIMEOUT) != 0);
    ASSERT(abs_i32(n.speed(0, 1200000)) < 1000);
    return 0;
}

/* N3 — the next absolute segment after a gap becomes the new anchor. */
static int test_n3_reanchor(void) {
    NodeSim n;
    n.set_board(0);
    n.sync(0);

    mc_segment_t a = const_speed(mc_um_to_code(1000000), 15000);
    ASSERT(n.push(0, 0, 0, 30000, &a) == 0);

    /* Leave a 470 ms hole, then send the absolute position at that time. */
    const int64_t resume_us = 500000;
    (void)n.eval(0, resume_us);
    mc_segment_t b = const_speed(mc_um_to_code(1300000), 15000);
    ASSERT(n.push(0, 0, resume_us, 30000, &b) == 0);
    ASSERT(n.eval(0, resume_us) == mc_code_to_um(b.p0));
    ASSERT(n.eval(0, resume_us + 10000) ==
           mc_segment_eval_um(&b, 10000));
    return 0;
}

/* N4 — if the next segment is still absent when the queue empties, the
 * axis holds and raises a fault. */
static int test_n4_queue_empty_fault(void) {
    NodeSim n;
    n.set_board(0);
    n.sync(0);

    mc_segment_t a = const_speed(mc_um_to_code(1000000), 15000);
    ASSERT(n.push(0, 0, 0, 30000, &a) == 0);

    for (int64_t t = 2000; t <= 600000; t += 2000)
        (void)n.eval(0, t);

    ASSERT(n.holding(0));
    ASSERT((n.fault(0) & MC_STATUS_FAULT_SAMPLE_TIMEOUT) != 0);
    ASSERT(abs_i32(n.speed(0, 600000)) < 1000);
    return 0;
}

/* N5 — an out-of-range segment is rejected; the stored position is
 * unchanged and a fault is raised. */
static int test_n5_out_of_range(void) {
    NodeSim n;
    n.set_board(0);
    n.sync(0);

    mc_segment_t a = const_speed(mc_um_to_code(1000000), 15000);
    ASSERT(n.push(0, 0, 0, 30000, &a) == 0);
    int32_t held = n.held_um(0);

    mc_segment_t bad = const_speed(mc_um_to_code(1999000), 32767);
    ASSERT(n.push(0, 0, 30000, 1000, &bad) == -1);
    ASSERT((n.fault(0) & MC_STATUS_FAULT_OUT_OF_RANGE) != 0);
    ASSERT(n.held_um(0) == held);
    ASSERT(n.holding(0));
    return 0;
}

/* N6 — a 35 ppm clock corrected by 20 ms syncs stays under 1 ms and does
 * not add a position step. */
static int test_n6_clock_in_node(void) {
    NodeSim n;
    n.set_board(0);
    n.clock.set_crystal_ppm(35);
    n.sync(0);

    mc_segment_t a = const_speed(mc_um_to_code(500000), 15000);
    ASSERT(n.push(0, 0, 0, 2000000, &a) == 0);

    int32_t prev = n.eval(0, 0);
    for (int i = 0; i < 100; ++i) {
        n.clock.advance_us(20000);
        n.sync(n.clock.master_us());
        int64_t t = n.clock.eval_us();
        int32_t p = n.eval(0, t);
        ASSERT(p >= prev);
        ASSERT(p - prev < 20000);       /* 0.6 m/s over 20 ms = 12 mm */
        ASSERT(abs_i32((int32_t)(t - n.clock.master_us())) < 1000);
        prev = p;
    }
    return 0;
}

/* N7 — after the sync timeout the node holds. */
static int test_n7_sync_timeout(void) {
    NodeSim n;
    n.set_board(0);
    n.sync(0);

    mc_segment_t a = const_speed(mc_um_to_code(1000000), 15000);
    ASSERT(n.push(0, 0, 0, 1000000, &a) == 0);

    n.clock.advance_us(100001);
    int16_t steps[4] = {0, 0, 0, 0};
    uint16_t ticks = 0;
    ASSERT(n.step(2000, steps, &ticks) == -1);
    ASSERT(n.holding(0));
    ASSERT((n.fault(0) & MC_STATUS_FAULT_SYNC_LOST) != 0);
    return 0;
}

/* N8 — CAN fault or segment timeout decelerates to rest, holds and
 * reports; a type-7 CAN frame does not clear the hardwired E-stop. */
static int test_n8_fault_paths(void) {
    NodeSim n;
    n.set_board(0);
    n.sync(0);
    n.sync_timeout_us = 10000000;

    mc_segment_t a = const_speed(mc_um_to_code(1000000), 15000);
    ASSERT(n.push(0, 0, 0, 1000000, &a) == 0);

    n.clock.advance_us(10000);
    n.request_stop(MC_STATUS_FAULT_SAMPLE_TIMEOUT);

    int16_t steps[4];
    uint16_t ticks = 0;
    for (int i = 0; i < 500; ++i) {
        n.clock.advance_us(2000);
        (void)n.step(2000, steps, &ticks);
    }
    ASSERT(n.holding(0));
    ASSERT((n.fault(0) & MC_STATUS_FAULT_SAMPLE_TIMEOUT) != 0);
    ASSERT(abs_i32(n.speed(0, n.clock.eval_us())) < 1000);

    /* The hardwired E-stop is independent of any CAN stop. */
    n.estop = true;
    n.can_fault = true;
    n.clock.advance_us(2000);
    ASSERT(n.step(2000, steps, &ticks) == -1);
    ASSERT(n.estop == true);
    return 0;
}

/* N9 — the receive ring stores bytes and a timestamp; parsing happens in
 * the motion task, not in the producer. */
static int test_n9_receive_ring(void) {
    NodeSim n;
    n.set_board(0);
    n.sync(0);

    mc_segment_t seg = const_speed(mc_um_to_code(1000000), 15000);
    uint8_t data[8];
    ASSERT(mc_pack_frame(0, &seg, data) == 0);

    int before = n.parse_count;
    ASSERT(n.rx_push(mc_id_make(0, 0), data, 1000, 0, 30000) == 1);
    ASSERT(n.parse_count == before);
    ASSERT(n.rx_head != n.rx_tail);

    ASSERT(n.drain() == 1);
    ASSERT(n.parse_count == before + 1);
    ASSERT(n.first_p0_code(0) == seg.p0);
    return 0;
}

/* N10 — a frame for another board's motor never updates this board. */
static int test_n10_foreign_motor(void) {
    NodeSim n;
    n.set_board(3);
    n.sync(0);

    mc_segment_t mine = const_speed(mc_um_to_code(1000000), 15000);
    uint8_t data[8];
    ASSERT(mc_pack_frame(0, &mine, data) == 0);
    ASSERT(n.apply_frame(mc_id_make(12, 0), data, 0, 30000) == 0);
    int32_t p0 = n.first_p0_code(0);
    ASSERT(p0 == mine.p0);

    mc_segment_t other = const_speed(mc_um_to_code(1200000), 15000);
    ASSERT(mc_pack_frame(0, &other, data) == 0);
    ASSERT(n.apply_frame(mc_id_make(16, 0), data, 0, 30000) == -1);
    ASSERT(n.first_p0_code(0) == p0);
    return 0;
}

/* N11 — an injected following error raises a fault and leaves the
 * evaluated curve unchanged. */
static int test_n11_following_error(void) {
    NodeSim n;
    n.set_board(0);
    n.sync(0);

    mc_segment_t a = const_speed(mc_um_to_code(1000000), 15000);
    ASSERT(n.push(0, 0, 0, 30000, &a) == 0);
    int32_t before = n.eval(0, 10000);

    n.inject_following_error(0, 5);
    ASSERT((n.fault(0) & MC_STATUS_FAULT_FOLLOWING) != 0);
    ASSERT(n.eval(0, 10000) == before);
    return 0;
}

/* N12 — one node at fault does not change a second node. */
static int test_n12_isolation(void) {
    NodeSim a;
    NodeSim b;
    a.set_board(0);
    b.set_board(1);
    a.sync(0);
    b.sync(0);

    mc_segment_t s = const_speed(mc_um_to_code(1000000), 15000);
    ASSERT(a.push(0, 0, 0, 30000, &s) == 0);
    ASSERT(b.push(0, 0, 0, 30000, &s) == 0);
    int32_t b_before = b.eval(0, 10000);

    a.inject_following_error(0, 10);
    a.request_stop(MC_STATUS_FAULT_SAMPLE_TIMEOUT);
    ASSERT(b.eval(0, 10000) == b_before);
    ASSERT((b.fault(0) & MC_STATUS_FAULT_FOLLOWING) == 0);
    return 0;
}

/* N13 — master stops: both nodes decelerate and keep their step position;
 * a reset requires a new segment before motion. */
static int test_n13_master_stop_and_reset(void) {
    NodeSim a;
    NodeSim b;
    a.set_board(0);
    b.set_board(1);
    a.sync(0);
    b.sync(0);
    a.sync_timeout_us = 10000000;
    b.sync_timeout_us = 10000000;

    mc_segment_t s = const_speed(mc_um_to_code(1000000), 15000);
    ASSERT(a.push(0, 0, 0, 30000, &s) == 0);
    ASSERT(b.push(0, 0, 0, 30000, &s) == 0);

    int16_t steps_a[4];
    int16_t steps_b[4];
    uint16_t ticks = 0;
    for (int i = 0; i < 300; ++i) {
        a.clock.advance_us(2000);
        b.clock.advance_us(2000);
        (void)a.step(2000, steps_a, &ticks);
        (void)b.step(2000, steps_b, &ticks);
    }
    ASSERT(a.holding(0) && b.holding(0));
    ASSERT(a.step_pos(0) >= 0 && b.step_pos(0) >= 0);

    /* The held step position does not move while no segment arrives. */
    int32_t pa = a.step_pos(0);
    int32_t pb = b.step_pos(0);
    a.clock.advance_us(2000);
    b.clock.advance_us(2000);
    (void)a.step(2000, steps_a, &ticks);
    (void)b.step(2000, steps_b, &ticks);
    ASSERT(a.step_pos(0) == pa);
    ASSERT(b.step_pos(0) == pb);

    a.reset();
    ASSERT(a.state(0) == MC_STATUS_IDLE);
    ASSERT(a.eval(0, 0) == -1);
    mc_segment_t resume = const_speed(mc_um_to_code(1200000), 0);
    ASSERT(a.push(0, 0, 0, 30000, &resume) == 0);
    ASSERT(a.state(0) != MC_STATUS_IDLE);
    return 0;
}

/* N14 — property: random dropouts keep position continuous and inside
 * travel; the next good segment restores the absolute p0. */
static uint32_t g_xs_state;

static uint32_t xorshift32(void) {
    uint32_t x = g_xs_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g_xs_state = x;
    return x;
}

static int test_n14_dropout_property(void) {
    const uint32_t seed = 0x12345678u;
    g_xs_state = seed;

    for (int k = 0; k < 1000; ++k) {
        NodeSim n;
        n.set_board(0);
        n.sync(0);

        mc_segment_t a = const_speed(mc_um_to_code(1000000), 15000);
        ASSERT(n.push(0, 0, 0, 30000, &a) == 0);

        int gap_chunks = (int)(xorshift32() % 21);   /* 0…40 ms */
        int64_t t_end = 30000 + (int64_t)gap_chunks * 2000;

        int32_t prev = n.eval(0, 0);
        for (int64_t t = 2000; t <= t_end; t += 2000) {
            int32_t p = n.eval(0, t);
            if (p < 0 || p > 2000000 || abs_i32(p - prev) >= 5000) {
                fprintf(stderr,
                        "N14 seed=0x%08X case=%d t=%lld p=%d prev=%d\n",
                        seed, k, (long long)t, (int)p, (int)prev);
                return 1;
            }
            prev = p;
        }

        int32_t p_now = n.eval(0, t_end);
        mc_segment_t b = const_speed((uint16_t)mc_um_to_code(p_now), 15000);
        ASSERT(n.push(0, 0, t_end, 30000, &b) == 0);
        ASSERT(n.eval(0, t_end) == mc_code_to_um(b.p0));
    }
    return 0;
}

static int reg_n1 = add_test("N1", test_n1_timeline_not_arrival);
static int reg_n2 = add_test("N2", test_n2_missing_segment);
static int reg_n3 = add_test("N3", test_n3_reanchor);
static int reg_n4 = add_test("N4", test_n4_queue_empty_fault);
static int reg_n5 = add_test("N5", test_n5_out_of_range);
static int reg_n6 = add_test("N6", test_n6_clock_in_node);
static int reg_n7 = add_test("N7", test_n7_sync_timeout);
static int reg_n8 = add_test("N8", test_n8_fault_paths);
static int reg_n9 = add_test("N9", test_n9_receive_ring);
static int reg_n10 = add_test("N10", test_n10_foreign_motor);
static int reg_n11 = add_test("N11", test_n11_following_error);
static int reg_n12 = add_test("N12", test_n12_isolation);
static int reg_n13 = add_test("N13", test_n13_master_stop_and_reset);
static int reg_n14 = add_test("N14", test_n14_dropout_property);
