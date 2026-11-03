/* test_identifiers.cpp — §3 of motion_streaming_tdd.md (I1–I10).
 *
 * Contract: Fly_Micro4_addressing.md §2 and §4.
 *   id = (field << 4) | type, 11-bit standard identifier only.
 *   Motor fields 0–119, board IDs 0–29, field 127 addresses all boards.
 */

#include "assert.h"
#include "motion_core.h"
#include "test_list.h"

#include <initializer_list>

/* I1 — id construction and the 0…0x7FF domain. */
static int test_i1_make_and_range(void) {
    /* Field 0 is legal for motor 0 and for the system/broadcast types. */
    for (int type : {0, 2, 3, 4, 5, 6, 7, 8, 10, 11})
        ASSERT(mc_id_make(0, type) == type);
    /* reserved types and motion fields above the motor range are rejected */
    for (int type : {1, 9, 12, 13, 14, 15})
        ASSERT(mc_id_make(0, type) == -1);

    ASSERT(mc_id_make(5, 0) == 80);          /* (5<<4)|0 */
    ASSERT(mc_id_make(119, 0) == 119 * 16);
    ASSERT(mc_id_make(29, 3) == 29 * 16 + 3);
    ASSERT(mc_id_make(127, 7) == 127 * 16 + 7);

    /* every constructed id stays in the 0…0x7FF 11-bit domain */
    for (int field = 0; field <= 127; field += 7)
        for (int type = 0; type <= 15; ++type)
            if (mc_id_make(field, type) >= 0)
                ASSERT(mc_id_make(field, type) <= 0x7FF);

    ASSERT(mc_id_valid(0x800) == 0);
    ASSERT(mc_id_valid(0xFFFFF) == 0);
    return 0;
}

/* I2 — type 0 carries a motor field 0…119; motor 0 is legal; type 1 is
 * reserved; 120…127 are rejected. */
static int test_i2_motion_field_rules(void) {
    ASSERT(mc_id_make(0, 0) == 0x00);
    ASSERT(mc_id_valid(0x00) == 1);
    ASSERT(mc_id_make(1, 0) == 0x10);
    ASSERT(mc_id_valid(mc_id_make(119, 0)) == 1);
    ASSERT(mc_id_make(120, 0) == -1);
    ASSERT(mc_id_make(127, 0) == -1);
    ASSERT(mc_id_make(0, 1) == -1);
    ASSERT(mc_id_make(5, 1) == -1);
    ASSERT(mc_id_valid(0x11) == 0);          /* motor 1, reserved type */
    return 0;
}

/* I3 — sync (type 2) requires field 0. */
static int test_i3_sync_field_zero(void) {
    ASSERT(mc_id_make(0, 2) == 2);
    ASSERT(mc_id_valid(2) == 1);
    ASSERT(mc_id_make(1, 2) == -1);
    ASSERT(mc_id_make(5, 2) == -1);
    return 0;
}

/* I4 — types 3–7 carry board 0…29 or 127 for every board. */
static int test_i4_board_types(void) {
    for (int type = 3; type <= 7; ++type) {
        ASSERT(mc_id_valid(mc_id_make(0, type)) == 1);     /* board 0 */
        ASSERT(mc_id_valid(mc_id_make(29, type)) == 1);    /* board 29 */
        ASSERT(mc_id_valid(mc_id_make(127, type)) == 1);   /* all boards */
        ASSERT(mc_id_make(30, type) == -1);
        ASSERT(mc_id_make(126, type) == -1);
    }
    return 0;
}

/* I5 — DISC/ASK require field 0; ASSIGN carries board 0…29; type 9 is
 * reserved. */
static int test_i5_commissioning_types(void) {
    ASSERT(mc_id_make(0, 8) == 8);
    ASSERT(mc_id_make(0, 11) == 11);
    ASSERT(mc_id_make(1, 8) == -1);
    ASSERT(mc_id_make(1, 11) == -1);

    ASSERT(mc_id_make(0, 10) == 10);
    ASSERT(mc_id_valid(mc_id_make(29, 10)) == 1);
    ASSERT(mc_id_make(30, 10) == -1);
    ASSERT(mc_id_make(127, 10) == -1);

    ASSERT(mc_id_make(0, 9) == -1);
    ASSERT(mc_id_make(5, 9) == -1);
    return 0;
}

/* I6 — types 12–15 are rejected. */
static int test_i6_reserved_types(void) {
    for (int type = 12; type <= 15; ++type) {
        ASSERT(mc_id_make(0, type) == -1);
        ASSERT(mc_id_make(5, type) == -1);
    }
    return 0;
}

/* I7 — motor 5/type 0 and board 5/type 3 are different identifiers; the
 * decoder reports the right entity kind. */
static int test_i7_decoder_entities(void) {
    int motor_id = mc_id_make(5, 0);
    int board_id = mc_id_make(5, 3);
    ASSERT(motor_id != board_id);

    ASSERT(mc_id_motor(motor_id) == 5);
    ASSERT(mc_id_motor(mc_id_make(0, 0)) == 0);
    ASSERT(mc_id_motor(mc_id_make(119, 0)) == 119);
    ASSERT(mc_id_motor(board_id) == -1);

    ASSERT(mc_id_board(board_id) == 5);
    ASSERT(mc_id_board(mc_id_make(29, 7)) == 29);
    ASSERT(mc_id_board(mc_id_make(127, 4)) == 127);
    ASSERT(mc_id_board(mc_id_make(5, 10)) == 5);
    ASSERT(mc_id_board(motor_id) == -1);
    ASSERT(mc_id_board(mc_id_make(5, 9)) == -1);
    ASSERT(mc_id_node(board_id) == 5);
    return 0;
}

/* I8 — board b owns motors 4b … 4b+3. Boards outside 0–29 own nothing. */
static int test_i8_ownership_table(void) {
    for (int board = 0; board < 30; ++board) {
        int first = 4 * board;
        ASSERT(mc_board_first_motor(board) == first);
        for (int m = 0; m <= 119; ++m) {
            int expect = (m >= first && m <= first + 3) ? 1 : 0;
            ASSERT(mc_board_owns_motor(board, m) == expect);
        }
    }
    ASSERT(mc_board_first_motor(-1) == -1);
    ASSERT(mc_board_first_motor(30) == -1);
    ASSERT(mc_board_owns_motor(30, 0) == 0);
    ASSERT(mc_board_first_motor(0) == 0);
    ASSERT(mc_board_first_motor(29) == 116);
    ASSERT(mc_node_first_motor(0) == 0);
    ASSERT(mc_node_owns_motor(29, 119) == 1);
    return 0;
}

/* I9 — board b accepts motion for its four motors, frames addressed to
 * board b, field 127 on types 3–7, and field 0 on types 2, 8 and 11. */
static int test_i9_acceptance(void) {
    const int board = 3;
    const int first = 4 * board;             /* 12, 13, 14, 15 */

    for (int m = 0; m <= 119; ++m) {
        int expect = (m >= first && m < first + 4) ? 1 : 0;
        ASSERT(mc_board_accepts_id(board, mc_id_make(m, 0)) == expect);
    }
    ASSERT(mc_board_accepts_id(board, mc_id_make(12, 1)) == 0);

    for (int type = 3; type <= 7; ++type) {
        ASSERT(mc_board_accepts_id(board, mc_id_make(board, type)) == 1);
        ASSERT(mc_board_accepts_id(board, mc_id_make(127, type)) == 1);
        ASSERT(mc_board_accepts_id(board, mc_id_make(board + 1, type)) == 0);
        ASSERT(mc_board_accepts_id(board, mc_id_make(0, type)) ==
               (board == 0 ? 1 : 0));
    }

    for (int type : {2, 8, 11})
        ASSERT(mc_board_accepts_id(board, mc_id_make(0, type)) == 1);
    ASSERT(mc_board_accepts_id(board, mc_id_make(1, 2)) == 0);

    ASSERT(mc_board_accepts_id(board, mc_id_make(board, 10)) == 1);
    ASSERT(mc_board_accepts_id(board, mc_id_make(board + 1, 10)) == 0);

    ASSERT(mc_board_accepts_id(board, 0x800) == 0);
    ASSERT(mc_board_accepts_id(board, mc_id_make(5, 1)) == 0);
    ASSERT(mc_board_accepts_id(board, mc_id_make(30, 4)) == 0);
    ASSERT(mc_node_accepts_id(board, mc_id_make(first, 0)) == 1);
    return 0;
}

/* I10 — property: for every motor 0…119 (type 0) exactly one board
 * accepts the frame. */
static int test_i10_unique_acceptor_property(void) {
    for (int motor = 0; motor <= 119; ++motor) {
        int id = mc_id_make(motor, 0);
        int acceptors = 0;
        for (int board = 0; board < 30; ++board)
            if (mc_board_accepts_id(board, id))
                ++acceptors;
        ASSERT(acceptors == 1);
    }
    return 0;
}

static int reg_i1 = add_test("I1", test_i1_make_and_range);
static int reg_i2 = add_test("I2", test_i2_motion_field_rules);
static int reg_i3 = add_test("I3", test_i3_sync_field_zero);
static int reg_i4 = add_test("I4", test_i4_board_types);
static int reg_i5 = add_test("I5", test_i5_commissioning_types);
static int reg_i6 = add_test("I6", test_i6_reserved_types);
static int reg_i7 = add_test("I7", test_i7_decoder_entities);
static int reg_i8 = add_test("I8", test_i8_ownership_table);
static int reg_i9 = add_test("I9", test_i9_acceptance);
static int reg_i10 = add_test("I10", test_i10_unique_acceptor_property);
