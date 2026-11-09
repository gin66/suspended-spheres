/* test_payloads.cpp — §4 items P1, P2, P3, P10 of motion_streaming_tdd.md.
 *
 * Contract: motion_streaming_whitepaper.md §4.2 and
 * Fly_Micro4_addressing.md §2.  One motion frame per motor is one cubic
 * segment, exactly 8 bytes, little-endian:
 *
 *   [ p0 : u16 ][ v0 : s16 ][ a0 : s16 ][ j : s16 ]
 *
 * Type 0 is motion; type 1 is reserved and rejected.  P4–P8 are in
 * test_protocol.cpp.
 */

#include "assert.h"
#include "motion_core.h"
#include "test_list.h"

/* P1 — the type-0 motion payload is exactly 8 bytes: an unsigned 16-bit
 * position and three signed 16-bit coefficients, little-endian. Pack and
 * unpack are inverses, including negative values. */
static int test_p1_segment_roundtrip(void) {
    const mc_segment_t seg = {0x0102, -2, 300, (int16_t)0x8000};
    uint8_t buf[8];
    mc_segment_t out = {0, 0, 0, 0};

    ASSERT(mc_pack_frame(0, &seg, buf) == 0);

    const uint8_t expect[8] = {
        0x02, 0x01,   /* p0 = 0x0102 */
        0xFE, 0xFF,   /* v0 = −2     */
        0x2C, 0x01,   /* a0 = 300    */
        0x00, 0x80,   /* j  = −32768 */
    };
    for (int i = 0; i < 8; ++i)
        ASSERT(buf[i] == expect[i]);

    ASSERT(mc_unpack_frame(buf, 8, 0, &out) == 0);
    ASSERT(out.p0 == seg.p0);
    ASSERT(out.v0 == seg.v0);
    ASSERT(out.a0 == seg.a0);
    ASSERT(out.j == seg.j);
    return 0;
}

/* P2 — type 1 is reserved: neither pack nor unpack accepts it. The retired
 * three/four-position sample layouts are not motion frames. */
static int test_p2_type1_reserved(void) {
    const mc_segment_t seg = {1, 2, 3, 4};
    mc_segment_t out = {0, 0, 0, 0};
    uint8_t buf[8];

    ASSERT(mc_pack_frame(0, &seg, buf) == 0);
    ASSERT(mc_pack_frame(1, &seg, buf) == -1);
    ASSERT(mc_pack_frame(2, &seg, buf) == -1);
    ASSERT(mc_unpack_frame(buf, 8, 1, &out) == -1);
    ASSERT(mc_unpack_frame(buf, 8, 2, &out) == -1);
    return 0;
}

/* P3 — a payload shorter or longer than 8 bytes is rejected. */
static int test_p3_length_rejected(void) {
    const mc_segment_t seg = {1, 2, 3, 4};
    mc_segment_t out = {0, 0, 0, 0};
    uint8_t buf[16];

    ASSERT(mc_pack_frame(0, &seg, buf) == 0);
    ASSERT(mc_unpack_frame(buf, 7, 0, &out) == -1);
    ASSERT(mc_unpack_frame(buf, 9, 0, &out) == -1);
    ASSERT(mc_unpack_frame(buf, 0, 0, &out) == -1);

    /* null arguments are refused */
    ASSERT(mc_pack_frame(0, 0, buf) == -1);
    ASSERT(mc_pack_frame(0, &seg, 0) == -1);
    ASSERT(mc_unpack_frame(0, 8, 0, &out) == -1);
    ASSERT(mc_unpack_frame(buf, 8, 0, 0) == -1);
    return 0;
}

/* P9 — the per-node alternate packs four 16-bit positions into one
 * frame; unpacking marks it as not the per-motor cubic baseline. */
static int test_p9_per_node_frame(void) {
    mc_node_positions_t frame;
    frame.pos[0] = 0;
    frame.pos[1] = 1;
    frame.pos[2] = 0xFFFE;
    frame.pos[3] = 0xFFFF;
    frame.per_node = 0;

    uint8_t buf[8];
    ASSERT(mc_pack_node_frame(&frame, buf) == 0);
    const uint8_t expect[8] = {
        0x00, 0x00, 0x01, 0x00, 0xFE, 0xFF, 0xFF, 0xFF,
    };
    for (int i = 0; i < 8; ++i)
        ASSERT(buf[i] == expect[i]);

    mc_node_positions_t out;
    out.per_node = 0;
    ASSERT(mc_unpack_node_frame(buf, 8, &out) == 0);
    for (int i = 0; i < 4; ++i)
        ASSERT(out.pos[i] == frame.pos[i]);
    ASSERT(out.per_node == 1);
    ASSERT(out.per_node != 0);   /* not the per-motor baseline */

    ASSERT(mc_unpack_node_frame(buf, 7, &out) == -1);
    ASSERT(mc_unpack_node_frame(buf, 9, &out) == -1);
    ASSERT(mc_pack_node_frame(0, buf) == -1);
    ASSERT(mc_pack_node_frame(&frame, 0) == -1);
    ASSERT(mc_unpack_node_frame(0, 8, &out) == -1);
    ASSERT(mc_unpack_node_frame(buf, 8, 0) == -1);
    return 0;
}

/* P10 — mc_pack_frame / mc_unpack_frame follow P1 and P2 across the full
 * coefficient domain. */
static int test_p10_variants_through_abi(void) {
    const uint16_t p0_codes[6] = {0, 1, 32767, 32768, 65534, 65535};
    const int16_t coeffs[6] = {-32768, -1, 0, 1, 32000, 32767};
    uint8_t buf[8];

    for (int a = 0; a < 6; ++a) {
        for (int b = 0; b < 6; ++b) {
            mc_segment_t seg = {
                p0_codes[a], coeffs[b], coeffs[5 - b], coeffs[(a + b) % 6]
            };
            mc_segment_t out = {0, 0, 0, 0};
            ASSERT(mc_pack_frame(0, &seg, buf) == 0);
            ASSERT(mc_unpack_frame(buf, 8, 0, &out) == 0);
            ASSERT(out.p0 == seg.p0);
            ASSERT(out.v0 == seg.v0);
            ASSERT(out.a0 == seg.a0);
            ASSERT(out.j == seg.j);
        }
    }
    return 0;
}

/* P11 — version request is eight zeros; the response carries a semantic
 * version and a 32-bit build id, little-endian. */
static int test_p11_version_payloads(void) {
    uint8_t req[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    ASSERT(mc_pack_version_req(req) == 0);
    for (int i = 0; i < 8; ++i)
        ASSERT(req[i] == 0);

    const mc_version_t v = {1, 2, 3, MC_VERSION_FLAG_DIRTY, 0x12345678u};
    uint8_t buf[8];
    ASSERT(mc_pack_version_rsp(&v, buf) == 0);
    const uint8_t expect[8] = {
        1, 2, 3, 1, 0x78, 0x56, 0x34, 0x12,
    };
    for (int i = 0; i < 8; ++i)
        ASSERT(buf[i] == expect[i]);

    mc_version_t out = {0, 0, 0, 0, 0};
    ASSERT(mc_unpack_version_rsp(buf, &out) == 0);
    ASSERT(out.major == v.major);
    ASSERT(out.minor == v.minor);
    ASSERT(out.patch == v.patch);
    ASSERT(out.flags == v.flags);
    ASSERT(out.build == v.build);

    ASSERT(mc_pack_version_req(0) == -1);
    ASSERT(mc_pack_version_rsp(0, buf) == -1);
    ASSERT(mc_pack_version_rsp(&v, 0) == -1);
    ASSERT(mc_unpack_version_rsp(0, &out) == -1);
    ASSERT(mc_unpack_version_rsp(buf, 0) == -1);
    return 0;
}

static int reg_p1 = add_test("P1", test_p1_segment_roundtrip);
static int reg_p2 = add_test("P2", test_p2_type1_reserved);
static int reg_p3 = add_test("P3", test_p3_length_rejected);
static int reg_p9 = add_test("P9", test_p9_per_node_frame);
static int reg_p10 = add_test("P10", test_p10_variants_through_abi);
/* P12 — the type-14 health payload packs the MCU temperature, per-driver
 * OTPW/OT masks, DIAG mask, optional NTC temperatures, validity mask and
 * sequence counter into exactly 8 bytes. */
static int test_p12_health_payload(void) {
    const mc_health_t in = {
        -12,
        0x05,
        0x03,
        0x0A,
        25,
        -5,
        MC_HEALTH_VALID_MCU | MC_HEALTH_VALID_NTC_T0,
        77
    };
    const uint8_t expect[8] = {
        0xF4,  /* −12 °C */
        0x35,  /* otpw 0x05 | ot 0x03 << 4 */
        0x0A,  /* DIAG mask */
        0x19,  /* 25 °C */
        0xFB,  /* −5 °C */
        0x03,  /* valid mask */
        0x4D,  /* seq */
        0x00,  /* reserved */
    };
    uint8_t buf[8];
    mc_health_t out = {0, 0, 0, 0, 0, 0, 0, 0};

    ASSERT(mc_pack_health(&in, buf) == 0);
    for (int i = 0; i < 8; ++i)
        ASSERT(buf[i] == expect[i]);

    ASSERT(mc_unpack_health(buf, &out) == 0);
    ASSERT(out.mcu_temp_c == in.mcu_temp_c);
    ASSERT(out.otpw_mask == in.otpw_mask);
    ASSERT(out.ot_mask == in.ot_mask);
    ASSERT(out.diag_mask == in.diag_mask);
    ASSERT(out.ntc_t0_c == in.ntc_t0_c);
    ASSERT(out.ntc_bed_c == in.ntc_bed_c);
    ASSERT(out.valid_mask == in.valid_mask);
    ASSERT(out.seq == in.seq);

    ASSERT(mc_pack_health(0, buf) == -1);
    ASSERT(mc_pack_health(&in, 0) == -1);
    ASSERT(mc_unpack_health(0, &out) == -1);
    ASSERT(mc_unpack_health(buf, 0) == -1);

    /* The health type uses a board field; 127 is not a health broadcast. */
    ASSERT(mc_id_make(7, MC_TYPE_HEALTH) == ((7 << 4) | MC_TYPE_HEALTH));
    ASSERT(mc_id_make(127, MC_TYPE_HEALTH) == -1);
    return 0;
}

static int reg_p11 = add_test("P11", test_p11_version_payloads);
static int reg_p12 = add_test("P12", test_p12_health_payload);
