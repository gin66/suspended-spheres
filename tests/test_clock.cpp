/* test_clock.cpp — PlaybackClock rules (§0.1, whitepaper §4.9). */

#include "assert.h"
#include "playback_clock.h"
#include "test_list.h"

static int test_clock_crystal_accumulator(void) {
    PlaybackClock c;
    c.set_crystal_ppm(35);
    c.advance_us(100000);
    ASSERT(c.master_us() == 100000);
    ASSERT(c.local_us() == 100003);

    /* five 20 ms advances equal one 100 ms advance, including the
     * fractional microsecond left in the accumulator */
    PlaybackClock d;
    d.set_crystal_ppm(35);
    for (int i = 0; i < 5; ++i)
        d.advance_us(20000);
    ASSERT(d.master_us() == c.master_us());
    ASSERT(d.local_us() == c.local_us());

    /* one second at +35 ppm adds exactly 35 µs */
    PlaybackClock e;
    e.set_crystal_ppm(35);
    e.advance_us(1000000);
    ASSERT(e.local_us() == 1000035);
    return 0;
}

static int test_clock_sync_and_correction(void) {
    PlaybackClock c;
    ASSERT(c.sync_lost(20000) == true);

    c.set_crystal_ppm(35);
    c.advance_us(100000);
    c.on_sync(100000);
    ASSERT(c.sync_lost(20000) == false);
    ASSERT(c.correction_ppm() == 0);
    ASSERT(c.eval_us() == 100000);

    c.advance_us(1000000);
    ASSERT(c.master_us() == 1100000);
    ASSERT(c.local_us() == 1100038);

    c.on_sync(1100000);
    ASSERT(c.correction_ppm() == 35);
    ASSERT(c.eval_us() == 1100035);

    int64_t before = c.eval_us();
    c.advance_us(1000000);
    ASSERT(c.master_us() == 2100000);
    ASSERT(c.eval_us() - before == 999999);
    return 0;
}

static int test_clock_sync_timeout(void) {
    PlaybackClock c;
    c.on_sync(0);
    ASSERT(c.sync_lost(20000) == false);
    c.advance_us(20000);
    ASSERT(c.sync_lost(20000) == false);
    c.advance_us(1);
    ASSERT(c.sync_lost(20000) == true);
    c.on_sync(c.master_us());
    ASSERT(c.sync_lost(20000) == false);
    return 0;
}

static int test_clock_drift_under_1ms(void) {
    /* 35 ppm over 100 ms is 3.5 µs; the requirement is 1 ms. */
    PlaybackClock c;
    c.set_crystal_ppm(35);
    c.on_sync(0);
    for (int i = 0; i < 100; ++i) {
        c.advance_us(100000);
        int64_t drift = c.eval_us() - c.master_us();
        ASSERT(drift < 1000 && drift > -1000);
        c.on_sync(c.master_us());
    }
    ASSERT(c.correction_ppm() >= 30 && c.correction_ppm() <= 40);
    return 0;
}

static int reg_clock1 = add_test("N6a", test_clock_crystal_accumulator);
static int reg_clock2 = add_test("N6b", test_clock_sync_and_correction);
static int reg_clock3 = add_test("N6c", test_clock_sync_timeout);
static int reg_clock4 = add_test("N6d", test_clock_drift_under_1ms);
