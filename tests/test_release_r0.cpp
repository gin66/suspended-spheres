/* test_release_r0.cpp — firmware_release_tdd.md phase R0 (U1–U3) and R3
 * (C1–C3).  Identity and the enter-bootloader command. */

#include "assert.h"
#include "motion_core.h"
#include "test_list.h"

/* U1 — fasthash64 over the 8 bytes 00…07 with seed 0xA16231A7 yields the
 * little-endian low six bytes 65 ef aa 4c f6 5d, even when the source is
 * unaligned. */
static int test_r0_u1_uuid_hash(void) {
    const uint8_t raw[8] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
    const uint8_t expect[MC_UUID_LEN] = {0x65, 0xEF, 0xAA, 0x4C, 0xF6, 0x5D};
    uint8_t uuid[MC_UUID_LEN];
    uint8_t shifted[9] = {0};

    ASSERT(mc_uuid_from_flash_id(raw, uuid) == 0);
    for (int i = 0; i < MC_UUID_LEN; ++i)
        ASSERT(uuid[i] == expect[i]);

    for (int i = 0; i < 8; ++i)
        shifted[i + 1] = raw[i];
    ASSERT(mc_uuid_from_flash_id(shifted + 1, uuid) == 0);
    for (int i = 0; i < MC_UUID_LEN; ++i)
        ASSERT(uuid[i] == expect[i]);
    return 0;
}

/* U2 — UUID bytes are compared in full. */
static int test_r0_u2_uuid_compare(void) {
    const uint8_t a[MC_UUID_LEN] = {1, 2, 3, 4, 5, 6};
    uint8_t b[MC_UUID_LEN] = {1, 2, 3, 4, 5, 6};
    ASSERT(mc_uuid_equal(a, b) == 1);
    for (int bit = 0; bit < MC_UUID_LEN * 8; ++bit) {
        for (int i = 0; i < MC_UUID_LEN; ++i)
            b[i] = a[i];
        b[bit / 8] ^= (uint8_t)(1u << (bit % 8));
        ASSERT(mc_uuid_equal(a, b) == 0);
    }
    return 0;
}

/* U3 — the motor field is raw = board × 4 + axis with field 63 skipped.
 * Board 0 owns 0–3, board 15 owns 60, 61, 62 and 64, field 63 is never a
 * motor, and board 29 owns 117–120. */
static int test_r0_u3_motor_field_skip(void) {
    for (int board = 0; board < MC_BOARD_COUNT; ++board) {
        int seen[4];
        for (int axis = 0; axis < MC_AXES_PER_NODE; ++axis) {
            int raw = board * MC_AXES_PER_NODE + axis;
            int field = mc_raw_to_motor_field(raw);
            ASSERT(field >= 0);
            ASSERT(field != MC_MOTOR_FIELD_SKIP);
            ASSERT(mc_motor_field_valid(field) == 1);
            ASSERT(mc_board_owns_motor(board, field) == 1);
            seen[axis] = field;
        }
        for (int i = 0; i < 4; ++i)
            for (int j = i + 1; j < 4; ++j)
                ASSERT(seen[i] != seen[j]);
    }
    ASSERT(mc_raw_to_motor_field(0) == 0);
    ASSERT(mc_raw_to_motor_field(3) == 3);
    ASSERT(mc_raw_to_motor_field(60) == 60);
    ASSERT(mc_raw_to_motor_field(61) == 61);
    ASSERT(mc_raw_to_motor_field(62) == 62);
    ASSERT(mc_raw_to_motor_field(63) == 64);
    ASSERT(mc_raw_to_motor_field(116) == 117);
    ASSERT(mc_raw_to_motor_field(119) == 120);
    ASSERT(mc_raw_to_motor_field(120) == -1);
    ASSERT(mc_motor_field_valid(63) == 0);
    ASSERT(mc_motor_field_valid(121) == 0);
    ASSERT(mc_id_make(63, 0) == -1);
    ASSERT(mc_board_owns_motor(15, 60) == 1);
    ASSERT(mc_board_owns_motor(15, 64) == 1);
    ASSERT(mc_board_owns_motor(15, 63) == 0);
    ASSERT(mc_board_first_motor(15) == 60);
    ASSERT(mc_board_first_motor(29) == 117);
    return 0;
}

/* C1 — command action 4 round-trips with zero distance, zero speed and a
 * sequence byte. */
static int test_r3_c1_enter_round_trip(void) {
    const mc_command_t in = {MC_CMD_ENTER_BOOTLOADER, 0, 0, 0, 0x42};
    mc_command_t out;
    uint8_t buf[8];
    ASSERT(mc_pack_command(&in, buf) == 0);
    ASSERT(buf[0] == 4);
    ASSERT(mc_unpack_command(buf, 8, &out) == 0);
    ASSERT(out.action == MC_CMD_ENTER_BOOTLOADER);
    ASSERT(out.axis_mask == 0);
    ASSERT(out.distance_mm == 0);
    ASSERT(out.speed_mm_s == 0);
    ASSERT(out.seq == 0x42);
    return 0;
}

/* C2 — field rules used by both ends. Action 4 accepts board 0…29 only.
 * Actions 1, 2, 3 and 0x7F keep the old rules. */
static int test_r3_c2_command_fields(void) {
    for (int field = 0; field <= 127; ++field) {
        int enter_ok = (field <= 29) ? 1 : 0;
        int other_ok = (field <= 29 || field == 127) ? 1 : 0;
        ASSERT(mc_command_field_ok(MC_CMD_ENTER_BOOTLOADER, field) == enter_ok);
        ASSERT(mc_command_field_ok(MC_CMD_UP, field) == other_ok);
        ASSERT(mc_command_field_ok(MC_CMD_DOWN, field) == other_ok);
        ASSERT(mc_command_field_ok(MC_CMD_STOP, field) == other_ok);
        ASSERT(mc_command_field_ok(MC_CMD_REIDENTIFY, field) == other_ok);
    }
    ASSERT(mc_command_field_ok(MC_CMD_ENTER_BOOTLOADER, 127) == 0);
    ASSERT(mc_command_field_ok(MC_CMD_ENTER_BOOTLOADER, 0) == 1);
    ASSERT(mc_command_field_ok(MC_CMD_ENTER_BOOTLOADER, 29) == 1);
    ASSERT(mc_command_field_ok(MC_CMD_ENTER_BOOTLOADER, 30) == 0);
    ASSERT(mc_command_field_ok(99, 0) == 0);
    return 0;
}

/* C3 — pure Katapult request check.  The host test never resets. */
static int test_r3_c3_katapult_request(void) {
    ASSERT(MC_CANBOOT_SIGNATURE == 0x21746f6f426e6143ULL);
    ASSERT(MC_CANBOOT_REQUEST == 0x5984E3FA6CA1589BULL);
    ASSERT(mc_katapult_request_ok(MC_CANBOOT_SIGNATURE, 1, 1) == 1);
    ASSERT(mc_katapult_request_ok(MC_CANBOOT_SIGNATURE, 0, 1) == 0);
    ASSERT(mc_katapult_request_ok(MC_CANBOOT_SIGNATURE, 1, 0) == 0);
    ASSERT(mc_katapult_request_ok(MC_CANBOOT_SIGNATURE ^ 1ULL, 1, 1) == 0);
    ASSERT(mc_katapult_request_ok(0, 1, 1) == 0);
    /* RP2040 Katapult: reset vector 0x10000169 -> signature at 0x10000160. */
    ASSERT(MC_CANBOOT_SIG_OFFSET == 9u);
    ASSERT(mc_katapult_signature_address(0x10000169u) == 0x10000160u);
    ASSERT((mc_katapult_signature_address(0x10000169u) & 7u) == 0);
    return 0;
}

static int reg_r0_u1 = add_test("R0-U1", test_r0_u1_uuid_hash);
static int reg_r0_u2 = add_test("R0-U2", test_r0_u2_uuid_compare);
static int reg_r0_u3 = add_test("R0-U3", test_r0_u3_motor_field_skip);
static int reg_r3_c1 = add_test("R3-C1", test_r3_c1_enter_round_trip);
static int reg_r3_c2 = add_test("R3-C2", test_r3_c2_command_fields);
static int reg_r3_c3 = add_test("R3-C3", test_r3_c3_katapult_request);
