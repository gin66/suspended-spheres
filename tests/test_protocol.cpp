/* test_protocol.cpp — §4 items P4–P8 of motion_streaming_tdd.md.
 *
 * Contract: motion_streaming_whitepaper.md §4.5 and
 * Fly_Micro4_addressing.md §3.  All multi-byte fields are little-endian.
 */

#include "assert.h"
#include "motion_core.h"
#include "test_list.h"

/* P4 — sync frame byte table and t_us_low wrap recovery. */
static int test_p4_sync_frame(void) {
    mc_sync_t in = {0x01020304u, 11000u, 3, 250};
    mc_sync_t out = {0, 0, 0, 0};
    uint8_t buf[8];

    ASSERT(mc_pack_sync(&in, buf) == 0);
    const uint8_t expect[8] = {
        0x04, 0x03, 0x02, 0x01,   /* t_us_low */
        0xF8, 0x2A,               /* 11 000 µs */
        0x03, 0xFA,               /* n = 3, seq = 250 */
    };
    for (int i = 0; i < 8; ++i)
        ASSERT(buf[i] == expect[i]);

    ASSERT(mc_unpack_sync(buf, &out) == 0);
    ASSERT(out.t_us_low == in.t_us_low);
    ASSERT(out.dt_us == in.dt_us);
    ASSERT(out.n == in.n);
    ASSERT(out.seq == in.seq);

    /* A sync id is field 0, type 2. */
    ASSERT(mc_id_make(0, 2) == 2);
    ASSERT(mc_id_make(1, 2) == -1);

    /* After t_us_low wraps, the int64 timeline gains 2^32 µs. */
    mc_sync_time_t t;
    mc_sync_time_reset(&t);
    ASSERT(mc_sync_time_unwrap(&t, 0xFFFFFF00u) == 0xFFFFFF00LL);
    ASSERT(mc_sync_time_unwrap(&t, 0x00000100u) == 0x100000100LL);
    ASSERT(mc_sync_time_unwrap(&t, 0x00000200u) == 0x100000200LL);
    return 0;
}

/* P5 — status frame byte table; no position or velocity fields. */
static int test_p5_status_frame(void) {
    mc_status_t in = {
        MC_STATUS_PLAYING,
        MC_STATUS_FAULT_OUT_OF_RANGE | MC_STATUS_FAULT_FOLLOWING,
        {0, 1, 254, 255},
        7,
        42
    };
    mc_status_t out = {0, 0, {0, 0, 0, 0}, 0, 0};
    uint8_t buf[8];

    ASSERT(mc_pack_status(&in, buf) == 0);
    ASSERT(buf[0] == MC_STATUS_PLAYING);
    ASSERT(buf[1] == (MC_STATUS_FAULT_OUT_OF_RANGE |
                      MC_STATUS_FAULT_FOLLOWING));
    ASSERT(buf[2] == 0 && buf[3] == 1 && buf[4] == 254 && buf[5] == 255);
    ASSERT(buf[6] == 7);
    ASSERT(buf[7] == 42);

    ASSERT(mc_unpack_status(buf, &out) == 0);
    ASSERT(out.state == in.state);
    ASSERT(out.fault_flags == in.fault_flags);
    for (int i = 0; i < 4; ++i)
        ASSERT(out.following_error[i] == in.following_error[i]);
    ASSERT(out.buffer_depth == in.buffer_depth);
    ASSERT(out.last_seq == in.last_seq);
    return 0;
}

/* P6 — command, home and parameter frames (types 4, 5, 6) are not motion
 * payloads; the motion decoder rejects them. */
static int test_p6_non_motion_types(void) {
    mc_segment_t seg = {1, 2, 3, 4};
    mc_segment_t out = {0, 0, 0, 0};
    uint8_t buf[8];

    for (int type = 4; type <= 6; ++type) {
        ASSERT(mc_pack_frame(type, &seg, buf) == -1);
        ASSERT(mc_unpack_frame(buf, 8, type, &out) == -1);
    }
    ASSERT(mc_pack_frame(0, &seg, buf) == 0);
    ASSERT(mc_unpack_frame(buf, 8, 4, &out) == -1);
    ASSERT(mc_unpack_frame(buf, 8, 5, &out) == -1);
    ASSERT(mc_unpack_frame(buf, 8, 6, &out) == -1);
    return 0;
}

/* P7 — DISC and ASSIGN carry the 6-byte UUID followed by two zero bytes;
 * ASK is eight zero bytes. */
static int test_p7_discovery_payloads(void) {
    const uint8_t uuid[MC_UUID_LEN] = {0x10, 0x23, 0x45, 0x67, 0x89, 0xAB};
    uint8_t buf[8];
    uint8_t out[MC_UUID_LEN] = {0};

    ASSERT(mc_pack_uuid(uuid, buf) == 0);
    for (int i = 0; i < MC_UUID_LEN; ++i)
        ASSERT(buf[i] == uuid[i]);
    ASSERT(buf[6] == 0 && buf[7] == 0);
    ASSERT(mc_unpack_uuid(buf, out) == 0);
    ASSERT(mc_uuid_equal(out, uuid) == 1);

    for (int i = 0; i < 8; ++i)
        buf[i] = 0xAA;
    mc_pack_ask(buf);
    for (int i = 0; i < 8; ++i)
        ASSERT(buf[i] == 0);

    ASSERT(mc_id_make(0, 8) == 8);
    ASSERT(mc_id_make(0, 11) == 11);
    return 0;
}

/* P8 — ASSIGN is one frame: the first six payload bytes match the board's
 * own UUID and bytes 6–7 are zero; the field is the new board ID. */
static int test_p8_assign(void) {
    const uint8_t own[MC_UUID_LEN] = {1, 2, 3, 4, 5, 6};
    uint8_t payload[8] = {1, 2, 3, 4, 5, 6, 0, 0};
    const uint8_t other[8] = {1, 2, 3, 4, 5, 7, 0, 0};
    const uint8_t nonzero_tail[8] = {1, 2, 3, 4, 5, 6, 1, 0};
    int board_id = -1;

    ASSERT(mc_id_make(7, 10) == ((7 << 4) | 10));
    ASSERT(mc_assign_board(&board_id, 7, payload, own) == 0);
    ASSERT(board_id == 7);

    ASSERT(mc_assign_board(&board_id, 8, other, own) == -1);
    ASSERT(board_id == 7);
    ASSERT(mc_assign_board(&board_id, 8, nonzero_tail, own) == -1);
    ASSERT(board_id == 7);

    ASSERT(mc_assign_board(&board_id, 30, payload, own) == -1);
    ASSERT(board_id == 7);
    return 0;
}

static int reg_p4 = add_test("P4", test_p4_sync_frame);
static int reg_p5 = add_test("P5", test_p5_status_frame);
static int reg_p6 = add_test("P6", test_p6_non_motion_types);
static int reg_p7 = add_test("P7", test_p7_discovery_payloads);
static int reg_p8 = add_test("P8", test_p8_assign);
