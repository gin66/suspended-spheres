/* test_runner.cpp — the single motion_core_tests executable.
 *
 * Covers §1 Harness (H1–H4) and §2 Units (U1–U8) of
 * doc/motion_streaming_tdd.md.  Every host test here is clock-free and
 * hardware-free.  One main(); a failing ASSERT returns non-zero.
 */

#include "assert.h"
#include "motion_core.h"
#include "test_list.h"

/* H4 is verified by the C translation unit abi_c.c linked into this binary. */
extern "C" int mc_abi_probe(void);

/* ------------------------------------------------------------------ */
/*  §1 Harness                                                         */
/* ------------------------------------------------------------------ */

static int test_h1_exit_zero(void) {
    /* Reaching main() means the target built with no Pico SDK, no PIO
       headers and no CAN socket.  The CMake target adds no such deps. */
    return 0;
}

static int test_h2_compile_time_checks(void) {
    /* The core's static_asserts enforce these at build time; read them
       back here so a wrong value cannot compile silently. */
    ASSERT(mc_get_ticks_per_s() == 16000000);
    ASSERT(mc_get_min_cmd_ticks() == 3200);
    ASSERT(mc_get_max_steps_per_chunk() == 128);
    return 0;
}

static int test_h3_no_floating_point(void) {
    /* The real check is the source scan in CMakeLists.txt.  It sets this
       definition only after the scan passes, so a stale or skipped check
       cannot pass silently. */
#ifndef MC_H3_CHECK_RAN
#error "H3 source check did not run"
#endif
    return 0;
}

static int test_h4_c_abi(void) {
    ASSERT(mc_abi_probe() == 0);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  §2 Units and quantization                                          */
/* ------------------------------------------------------------------ */

static int test_u1_code_boundaries(void) {
    ASSERT(mc_code_to_um(0) == 0);
    ASSERT(mc_code_to_um(65535) == 2000000);
    return 0;
}

static int test_u2_integer_conversion(void) {
    /* floor(code * 2 000 000 / 65 535), not code * 0.0305 mm. */
    ASSERT(mc_code_to_um(32767) == 999984);
    ASSERT(mc_code_to_um(1) == 30);
    ASSERT(mc_code_to_um(65534) == 1999969);
    return 0;
}

static int test_u3_round_trip(void) {
    /* uint32_t loop counter: a uint16_t counter never exceeds 65 535 and
       would wrap forever. */
    for (uint32_t c = 0; c <= 65535; ++c) {
        uint16_t code = (uint16_t)c;
        int32_t um = mc_code_to_um(code);
        ASSERT(mc_um_to_code(um) == code);
    }
    return 0;
}

static int test_u4_code_step_size(void) {
    int32_t step = mc_code_to_um(1) - mc_code_to_um(0);
    ASSERT(step == 30);   /* 30.5 um truncated by the integer form */
    ASSERT(step < 50);    /* FR-MOT-002, 50 um visual limit */
    ASSERT(step < 39);    /* below the 39.3 um microstep */
    return 0;
}

static int test_u5_speed_encoding(void) {
    ASSERT(mc_speed_encode(1310680) == 32767);
    ASSERT(mc_speed_encode(-1310720) == -32768);
    ASSERT(mc_speed_encode(1200000) == 30000);
    ASSERT(mc_speed_decode(32767) == 1310680);
    ASSERT(mc_speed_decode(-32768) == -1310720);
    ASSERT(mc_speed_decode(0) == 0);
    ASSERT(mc_speed_encode(1310720) == -1);   /* rejected, not wrapped */
    ASSERT(mc_speed_encode(-1310721) == -1);
    return 0;
}

static int test_u6_lsb_check(void) {
    /* A 0.02 mm/s LSB cannot reach 1.2 m/s; 0.04 mm/s can. */
    ASSERT(32767 * 20 < 1200000);
    ASSERT(32767 * 40 >= 1200000);
    return 0;
}

static int test_u7_little_endian(void) {
    uint8_t buf[8] = {0};
    mc_segment_t seg = {0x0102, -2, 0x0304, 0x0506};
    const uint8_t expected[8] = {
        0x02, 0x01,  /* p0 */
        0xFE, 0xFF,  /* v0 = −2 */
        0x04, 0x03,  /* a0 */
        0x06, 0x05,  /* j */
    };
    ASSERT(mc_pack_frame(0, &seg, buf) == 0);
    for (int i = 0; i < 8; ++i) {
        ASSERT(buf[i] == expected[i]);
    }
    return 0;
}

static int test_u8_position_domain(void) {
    ASSERT(mc_um_in_range(0) == 1);
    ASSERT(mc_um_in_range(2000000) == 1);
    ASSERT(mc_um_in_range(1000000) == 1);
    ASSERT(mc_um_in_range(-1) == 0);
    ASSERT(mc_um_in_range(2000001) == 0);
    ASSERT(mc_um_in_range(2500000) == 0);  /* core domain, not wire range */
    return 0;
}

/* ------------------------------------------------------------------ */
/*  registration                                                       */
/* ------------------------------------------------------------------ */

static int reg_h1 = add_test("H1", test_h1_exit_zero);
static int reg_h2 = add_test("H2", test_h2_compile_time_checks);
static int reg_h3 = add_test("H3", test_h3_no_floating_point);
static int reg_h4 = add_test("H4", test_h4_c_abi);

static int reg_u1 = add_test("U1", test_u1_code_boundaries);
static int reg_u2 = add_test("U2", test_u2_integer_conversion);
static int reg_u3 = add_test("U3", test_u3_round_trip);
static int reg_u4 = add_test("U4", test_u4_code_step_size);
static int reg_u5 = add_test("U5", test_u5_speed_encoding);
static int reg_u6 = add_test("U6", test_u6_lsb_check);
static int reg_u7 = add_test("U7", test_u7_little_endian);
static int reg_u8 = add_test("U8", test_u8_position_domain);
