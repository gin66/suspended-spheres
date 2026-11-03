/* test_payloads.cpp — §4 items P1, P2, P3, P10 of motion_streaming_tdd.md.
 *
 * Contract: motion_streaming_whitepaper.md §4.2, §4.3.
 *   N=3 payload: [p1:16][p2:16][p3:16][v3:s16], 8 B, little-endian.
 *   N=4 payload: [p1:16][p2:16][p3:16][p4:16], 8 B, little-endian.
 * P4–P8 and P9 arrive later (§13 step 4).
 */

#include "assert.h"
#include "motion_core.h"
#include "test_list.h"

#include <string.h>

/* P1 — N=3: exactly 8 bytes, three unsigned 16-bit positions and one signed
 * 16-bit speed, little-endian; pack/unpack are inverses including a
 * negative speed. */
static int test_p1_n3_roundtrip(void) {
    const uint16_t pos[4] = {0x0102, 0x0304, 0x0506, 0xFFFF};
    const int16_t speed = (int16_t)0x8000;   /* −32 768, the negative rail */
    const int16_t speed2 = 30000;
    const int16_t speed3 = -1;
    uint8_t buf[8];
    int n = 0;
    int16_t out_speed = 0;
    uint16_t out_pos[4] = {0, 0, 0, 0};

    ASSERT(mc_pack_frame(3, pos, speed, buf) == 0);
    const uint8_t expect_sp8000[8] = {
        0x02, 0x01, 0x04, 0x03, 0x06, 0x05, 0x00, 0x80
    };
    for (int i = 0; i < 8; ++i)
        ASSERT(buf[i] == expect_sp8000[i]);

    out_speed = 999;
    ASSERT(mc_unpack_frame(buf, 8, 3, out_pos, &out_speed) == 0);
    ASSERT(out_pos[0] == pos[0]);
    ASSERT(out_pos[1] == pos[1]);
    ASSERT(out_pos[2] == pos[2]);
    ASSERT(out_speed == speed);

    ASSERT(mc_pack_frame(3, pos, speed2, buf) == 0);
    ASSERT(mc_unpack_frame(buf, 8, 3, out_pos, &out_speed) == 0);
    ASSERT(out_speed == speed2 && out_pos[2] == 0x0506);

    ASSERT(mc_pack_frame(3, pos, speed3, buf) == 0);
    ASSERT(mc_unpack_frame(buf, 8, 3, out_pos, &out_speed) == 0);
    ASSERT(out_speed == speed3);
    return 0;
}

/* P2 — N=4: exactly 8 bytes, four unsigned 16-bit positions; pack/unpack
 * are inverses. */
static int test_p2_n4_roundtrip(void) {
    const uint16_t pos[4] = {1, 300, 60000, 65535};
    uint8_t buf[8];
    int n = 0;
    int16_t out_speed = 0;
    uint16_t out_pos[4] = {0, 0, 0, 0};

    ASSERT(mc_pack_frame(4, pos, 0, buf) == 0);

    uint16_t p1 = buf[0] | (uint16_t)(buf[1] << 8);
    uint16_t p4 = buf[6] | (uint16_t)(buf[7] << 8);
    ASSERT(p1 == 1 && p4 == 65535);

    ASSERT(mc_unpack_frame(buf, 8, 4, out_pos, &out_speed) == 0);
    for (int k = 0; k < 4; ++k)
        ASSERT(out_pos[k] == pos[k]);
    return 0;
}

/* P3 — a payload shorter or longer than 8 bytes is rejected. */
static int test_p3_length_rejected(void) {
    const uint16_t pos[4] = {1, 2, 3, 4};
    uint8_t buf[16];
    int n = 0;
    int16_t speed = 0;
    uint16_t out_pos[4] = {0, 0, 0, 0};

    ASSERT(mc_pack_frame(3, pos, 5, buf) == 0);
    ASSERT(mc_pack_frame(4, pos, 0, buf + 8) == 0);

    ASSERT(mc_unpack_frame(buf, 7, 3, out_pos, &speed) == -1);
    ASSERT(mc_unpack_frame(buf, 9, 3, out_pos, &speed) == -1);
    ASSERT(mc_unpack_frame(buf, 0, 3, out_pos, &speed) == -1);
    ASSERT(mc_unpack_frame(buf + 8, 7, 4, out_pos, &speed) == -1);
    ASSERT(mc_unpack_frame(buf + 8, 9, 4, out_pos, &speed) == -1);

    /* the variant is caller knowledge (§6.3): bytes alone cannot detect
     * it, so only a nonsensical expect_n is rejected */
    ASSERT(mc_unpack_frame(buf, 8, 5, out_pos, &speed) == -1);
    /* 0-terminated arguments are refused */
    ASSERT(mc_pack_frame(3, 0, 5, buf) == -1);
    ASSERT(mc_pack_frame(3, pos, 5, 0) == -1);
    ASSERT(mc_unpack_frame(0, 8, 3, out_pos, &speed) == -1);
    return 0;
}

/* P10 — mc_pack_frame / mc_unpack_frame follow P1 and P2 for the §6.3
 * variants, exercised through the whole round trip for both variants. */
static int test_p10_variants_through_abi(void) {
    /* forward and boundary codes for both variants */
    const uint16_t a[4] = {0, 32767, 65534, 65535};
    const uint16_t b[4] = {65535, 1, 2, 0};
    uint8_t buf[8];
    int n = 0;
    int16_t speed = 0;
    uint16_t out_pos[4];

    ASSERT(mc_pack_frame(3, a, -32768, buf) == 0);
    ASSERT(mc_unpack_frame(buf, 8, 3, out_pos, &speed) == 0);
    ASSERT(out_pos[0] == 0 && out_pos[1] == 32767 && out_pos[2] == 65534);
    ASSERT(speed == -32768);

    ASSERT(mc_pack_frame(4, b, 0, buf) == 0);
    ASSERT(mc_unpack_frame(buf, 8, 4, out_pos, &speed) == 0);
    ASSERT(out_pos[0] == 65535 && out_pos[1] == 1 && out_pos[2] == 2 &&
           out_pos[3] == 0);

    /* unsupported variant */
    ASSERT(mc_pack_frame(5, a, 0, buf) == -1);
    ASSERT(mc_pack_frame(2, a, 0, buf) == -1);

    /* inverse property over a sample of codes for both variants */
    for (uint32_t c = 0; c <= 65535; c += 17) {
        uint16_t q[4] = {(uint16_t)c, (uint16_t)(65535 - c),
                         (uint16_t)(c ^ 0x5555), (uint16_t)(c ^ 0xAAAA)};
        ASSERT(mc_pack_frame(4, q, 0, buf) == 0);
        ASSERT(mc_unpack_frame(buf, 8, 4, out_pos, &speed) == 0);
        for (int k = 0; k < 4; ++k)
            ASSERT(out_pos[k] == q[k]);
        ASSERT(mc_pack_frame(3, q, (int16_t)(0x8000 - c), buf) == 0);
        ASSERT(mc_unpack_frame(buf, 8, 3, out_pos, &speed) == 0);
        ASSERT(out_pos[0] == q[0] && out_pos[1] == q[1] &&
               out_pos[2] == q[2]);
        ASSERT(speed == (int16_t)(0x8000 - c));
    }
    return 0;
}

static int reg_p1 = add_test("P1", test_p1_n3_roundtrip);
static int reg_p2 = add_test("P2", test_p2_n4_roundtrip);
static int reg_p3 = add_test("P3", test_p3_length_rejected);
static int reg_p10 = add_test("P10", test_p10_variants_through_abi);
