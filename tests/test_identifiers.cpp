/* test_identifiers.cpp — §3 of motion_streaming_tdd.md (I1–I10).
 *
 * Contract: Fly_Micro4_addressing.md §2 and §4.
 *   id = (field << 4) | type, 11-bit standard identifier only.
 */

#include "assert.h"
#include "motion_core.h"
#include "test_list.h"

#include <initializer_list>

/* I1 — id construction and the 0…0x7FF domain. */
static int test_i1_make_and_range(void) {
    for (int type = 0; type <= 15; ++type) {
        int id = mc_id_make(0, type);
        if (type >= 2 && type <= 11 && type != 9) {
            /* field 0 is legal for the system/broadcast types (I3, I4, I5) */
            ASSERT(id >= 0 && id <= 0x7FF);
        } else {
            /* motion types need a motor, HELLO needs a node, 12–15 reserved */
            ASSERT(id == -1);
        }
    }
    ASSERT(mc_id_make(5, 0) == 80);          /* (5<<4)|0 */
    ASSERT(mc_id_make(0, 2) == 2);
    ASSERT(mc_id_make(120, 1) == 120 * 16 + 1);
    ASSERT(mc_id_make(30, 7) == 30 * 16 + 7);
    /* every constructed id stays in the 0…0x7FF 11-bit domain */
    for (int field = 0; field <= 127; field += 7)
        for (int type = 0; type <= 11; ++type)
            if (mc_id_make(field, type) >= 0)
                ASSERT(mc_id_make(field, type) <= 0x7FF);
    /* Decoder must reject anything above 0x7FF (no 29-bit encoder). */
    ASSERT(mc_id_valid(0x800) == 0);
    ASSERT(mc_id_valid(0xFFFFF) == 0);
    return 0;
}

/* I2 — motion types 0/1 carry motor ID 1…120; 121…127 rejected; field 0
 * rejected for motion types. */
static int test_i2_motion_field_rules(void) {
    ASSERT(mc_id_valid(0x00) == 0);          /* field 0, type 0 → invalid */
    ASSERT(mc_id_make(0, 0) == -1);
    ASSERT(mc_id_make(0, 1) == -1);
    ASSERT(mc_id_valid(mc_id_make(1, 0)) == 1);
    ASSERT(mc_id_valid(mc_id_make(120, 1)) == 1);
    ASSERT(mc_id_valid(mc_id_make(121, 0)) == 0);
    ASSERT(mc_id_make(121, 0) == -1);
    ASSERT(mc_id_make(127, 1) == -1);
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

/* I4 — types 3–7 carry node 1…30 or 0 broadcast; 31 rejected. */
static int test_i4_node_types(void) {
    for (int type = 3; type <= 7; ++type) {
        ASSERT(mc_id_valid(mc_id_make(0, type)) == 1);   /* broadcast */
        ASSERT(mc_id_valid(mc_id_make(30, type)) == 1);  /* node 30 */
        ASSERT(mc_id_make(31, type) == -1);              /* rejected */
        ASSERT(mc_id_make(-1, type) == -1);
    }
    return 0;
}

/* I5 — DISC (8), SET_ID A (10), SET_ID B (11) require field 0.
 * HELLO (9) carries the short node ID 1…30. */
static int test_i5_commissioning_types(void) {
    for (int type : {8, 10, 11}) {
        ASSERT(mc_id_make(0, type) == (type));
        ASSERT(mc_id_valid(type) == 1);
        ASSERT(mc_id_make(1, type) == -1);
        ASSERT(mc_id_make(30, type) == -1);
    }
    ASSERT(mc_id_make(0, 9) == -1);      /* HELLO needs a real short ID */
    ASSERT(mc_id_make(1, 9) == 0x10 | 9);
    ASSERT(mc_id_valid(mc_id_make(30, 9)) == 1);
    ASSERT(mc_id_make(31, 9) == -1);
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

/* I7 — motor 5/type 0 and node 5/type 3 are different identifiers, and the
 * decoder reports the right entity kind for each type class. */
static int test_i7_decoder_entities(void) {
    int motor_id = mc_id_make(5, 0);
    int node_id = mc_id_make(5, 3);
    ASSERT(motor_id != node_id);
    ASSERT(motor_id >= 0 && node_id >= 0);

    /* motion types 0–1 report a motor */
    ASSERT(mc_id_motor(mc_id_make(5, 0)) == 5);
    ASSERT(mc_id_motor(mc_id_make(120, 1)) == 120);
    ASSERT(mc_id_motor(mc_id_make(5, 3)) == -1);

    /* node types 3–7 and 9 report a node */
    ASSERT(mc_id_node(mc_id_make(5, 3)) == 5);
    ASSERT(mc_id_node(mc_id_make(30, 7)) == 30);
    ASSERT(mc_id_node(mc_id_make(0, 4)) == 0);   /* broadcast */
    ASSERT(mc_id_node(mc_id_make(7, 9)) == 7);
    ASSERT(mc_id_node(mc_id_make(5, 0)) == -1);
    return 0;
}

/* I8 — node n owns motors 4(n−1)+1 … 4(n−1)+4. Nodes 0 and 31 own nothing. */
static int test_i8_ownership_table(void) {
    for (int node = 1; node <= 30; ++node) {
        int first = 4 * (node - 1) + 1;
        ASSERT(mc_node_first_motor(node) == first);
        for (int m = 1; m <= 120; ++m) {
            int expect = (m >= first && m <= first + 3) ? 1 : 0;
            ASSERT(mc_node_owns_motor(node, m) == expect);
        }
    }
    ASSERT(mc_node_owns_motor(1, 1) == 1);
    ASSERT(mc_node_owns_motor(1, 4) == 1);
    ASSERT(mc_node_owns_motor(1, 5) == 0);
    ASSERT(mc_node_owns_motor(30, 117) == 1);
    ASSERT(mc_node_owns_motor(30, 120) == 1);
    /* the unassigned short IDs own nothing */
    for (int m = 0; m <= 120; ++m) {
        ASSERT(mc_node_owns_motor(0, m) == 0);
        ASSERT(mc_node_owns_motor(31, m) == 0);
    }
    return 0;
}

/* I9 — a node accepts motion for its four motors, frames addressed to its
 * own node ID, and field 0. Every other motion frame is discarded without
 * parsing the payload. */
static int test_i9_acceptance(void) {
    const int node = 3;
    const int first = 4 * 2 + 1;              /* 9, 10, 11, 12 */

    for (int type : {0, 1}) {
        /* own motors accepted */
        for (int m = first; m < first + 4; ++m)
            ASSERT(mc_node_accepts_id(node, mc_id_make(m, type)) == 1);
        /* a motion frame for any other motor (node 7's motors) discarded */
        for (int m = 1; m <= 120; ++m) {
            int expect = (m >= first && m < first + 4) ? 1 : 0;
            ASSERT(mc_node_accepts_id(node, mc_id_make(m, type)) == expect);
        }
    }

    for (int type = 3; type <= 7; ++type) {
        ASSERT(mc_node_accepts_id(node, mc_id_make(node, type)) == 1);
        ASSERT(mc_node_accepts_id(node, mc_id_make(0, type)) == 1);  /* field 0 */
        ASSERT(mc_node_accepts_id(node, mc_id_make(node + 1, type)) == 0);
        ASSERT(mc_node_accepts_id(node, mc_id_make(node - 1, type)) == 0);
    }

    /* broadcast/system types with field 0 reach every node */
    ASSERT(mc_node_accepts_id(node, mc_id_make(0, 2)) == 1);
    ASSERT(mc_node_accepts_id(node, mc_id_make(0, 8)) == 1);
    ASSERT(mc_node_accepts_id(node, mc_id_make(0, 10)) == 1);
    ASSERT(mc_node_accepts_id(node, mc_id_make(0, 11)) == 1);

    /* invalid and out-of-range identifiers are never accepted */
    ASSERT(mc_node_accepts_id(node, 0x800) == 0);
    ASSERT(mc_node_accepts_id(node, mc_id_make(0, 0)) == 0);
    ASSERT(mc_node_accepts_id(node, mc_id_make(31, 4)) == 0);
    return 0;
}

/* I10 — property: for every motor 1…120 and both motion types exactly one
 * node in 1…30 accepts the frame. */
static int test_i10_unique_acceptor_property(void) {
    for (int motor = 1; motor <= 120; ++motor) {
        for (int type : {0, 1}) {
            int id = mc_id_make(motor, type);
            int acceptors = 0;
            for (int node = 1; node <= 30; ++node)
                if (mc_node_accepts_id(node, id))
                    ++acceptors;
            ASSERT(acceptors == 1);
        }
    }
    return 0;
}

static int reg_i1 = add_test("I1", test_i1_make_and_range);
static int reg_i2 = add_test("I2", test_i2_motion_field_rules);
static int reg_i3 = add_test("I3", test_i3_sync_field_zero);
static int reg_i4 = add_test("I4", test_i4_node_types);
static int reg_i5 = add_test("I5", test_i5_commissioning_types);
static int reg_i6 = add_test("I6", test_i6_reserved_types);
static int reg_i7 = add_test("I7", test_i7_decoder_entities);
static int reg_i8 = add_test("I8", test_i8_ownership_table);
static int reg_i9 = add_test("I9", test_i9_acceptance);
static int reg_i10 = add_test("I10", test_i10_unique_acceptor_property);
