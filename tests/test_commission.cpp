/* test_commission.cpp — §9 items M1–M7 of motion_streaming_tdd.md. */

#include "assert.h"
#include "commission_sim.h"
#include "motion_core.h"
#include "test_list.h"

static void make_flash(uint8_t out[8], int seed) {
    for (int i = 0; i < 8; ++i)
        out[i] = (uint8_t)(seed * 17 + i * 3 + 1);
}

/* M1 — the host table is (flash_id, axis) -> position, plus flash_id ->
 * board_id, exact for 30 entries.  The board has no position value. */
static int test_m1_host_table(void) {
    HostTable host;
    uint8_t flash[30][8];

    for (int i = 0; i < 30; ++i) {
        make_flash(flash[i], i);
        ASSERT(host.add(flash[i], i * 4) == i);
        ASSERT(host.board_for(flash[i]) == i);
        for (int axis = 0; axis < 4; ++axis)
            ASSERT(host.position_for(flash[i], axis) == i * 4 + axis);
    }
    ASSERT(host.count == 30);
    ASSERT(host.position_for(flash[0], 4) == -1);
    return 0;
}

/* M2 — a known flash ID keeps its board ID.  A second flash does not get
 * an occupied board ID. */
static int test_m2_no_duplicate_board(void) {
    HostTable host;
    uint8_t a[8], b[8];
    make_flash(a, 1);
    make_flash(b, 2);

    int board_a = host.add(a, 0);
    ASSERT(board_a == 0);
    ASSERT(host.add(a, 0) == board_a);      /* repeated advertisement */
    ASSERT(host.count == 1);

    int board_b = host.add(b, 4);
    ASSERT(board_b >= 0 && board_b != board_a);
    ASSERT(host.count == 2);
    ASSERT(host.board_for(a) == board_a);
    ASSERT(host.board_for(b) == board_b);
    return 0;
}

/* M3 — an unidentified board answers ASK with one DISC; an identified
 * board stays silent. */
static int test_m3_ask_disc(void) {
    uint8_t flash[8];
    make_flash(flash, 3);

    BoardSim b;
    b.set_flash(flash);
    b.on_ask(0);
    ASSERT(b.disc_due(0) == false);
    ASSERT(b.emit_disc(b.advertise_delay_us()) == mc_id_make(0, 8));
    ASSERT(b.disc_emitted == 1);

    ASSERT(b.on_assign(3, flash) == 0);
    ASSERT(b.board_id == 3);
    b.on_ask(1000000);
    ASSERT(b.disc_due(2000000) == false);
    ASSERT(b.emit_disc(2000000) == -1);
    ASSERT(b.disc_emitted == 1);
    return 0;
}

/* M4 — the advertise delay is 1 ms × (flash_id byte 0 mod 10), under
 * 10 ms, and deterministic.  DISC is field 0, type 8. */
static int test_m4_backoff(void) {
    uint8_t f1[8], f2[8], f3[8];
    make_flash(f1, 4);
    make_flash(f2, 5);
    f1[0] = 7;
    f2[0] = 7;
    f3[0] = 10;

    BoardSim a, b, c;
    a.set_flash(f1);
    b.set_flash(f2);
    c.set_flash(f3);
    ASSERT(a.advertise_delay_us() == 7000);
    ASSERT(b.advertise_delay_us() == 7000);
    ASSERT(c.advertise_delay_us() == 0);
    ASSERT(a.advertise_delay_us() < 10000);

    a.on_ask(0);
    int id = a.emit_disc(a.advertise_delay_us());
    ASSERT(id == mc_id_make(0, 8));
    ASSERT(mc_id_field(id) == 0);
    ASSERT(mc_id_type(id) == 8);
    return 0;
}

/* M5 — one ASSIGN frame: the field becomes the board ID only when the
 * payload matches the board's own flash ID; the next ASK is silent. */
static int test_m5_assign(void) {
    uint8_t own[8], other[8];
    make_flash(own, 6);
    make_flash(other, 7);

    BoardSim b;
    b.set_flash(own);
    ASSERT(b.on_assign(4, other) == -1);
    ASSERT(b.board_id == -1);
    ASSERT(b.on_assign(4, own) == 0);
    ASSERT(b.board_id == 4);

    b.on_ask(0);
    ASSERT(b.emit_disc(1000000) == -1);
    ASSERT(b.disc_emitted == 0);
    return 0;
}

/* M6 — clearing the RAM ID makes the board unidentified again; the host
 * reassigns the board ID stored for that flash ID and keeps its map. */
static int test_m6_reassign_after_clear(void) {
    uint8_t flash[8];
    make_flash(flash, 8);

    HostTable host;
    int board = host.add(flash, 20);
    ASSERT(board >= 0);
    int pos0 = host.position_for(flash, 0);
    int pos3 = host.position_for(flash, 3);

    BoardSim b;
    b.set_flash(flash);
    ASSERT(b.on_assign(board, flash) == 0);
    b.clear_ram_id();
    ASSERT(b.board_id == -1);

    b.on_ask(5000000);
    ASSERT(b.emit_disc(5000000 + b.advertise_delay_us()) == mc_id_make(0, 8));
    ASSERT(b.disc_emitted == 1);

    ASSERT(host.board_for(flash) == board);
    ASSERT(host.position_for(flash, 0) == pos0);
    ASSERT(host.position_for(flash, 3) == pos3);
    ASSERT(b.on_assign(host.board_for(flash), flash) == 0);
    ASSERT(b.board_id == board);
    return 0;
}

/* M7 — ASK runs every 5 s during a pattern; identified boards stay
 * silent, and an ASK with no DISC ends the startup loop. */
static int test_m7_periodic_ask(void) {
    uint8_t f1[8], f2[8];
    make_flash(f1, 9);
    make_flash(f2, 10);

    HostTable host;
    int b1 = host.add(f1, 0);
    int b2 = host.add(f2, 4);

    BoardSim boards[2];
    boards[0].set_flash(f1);
    boards[1].set_flash(f2);
    ASSERT(boards[0].on_assign(b1, f1) == 0);
    ASSERT(boards[1].on_assign(b2, f2) == 0);

    /* Three 5 s rounds while a pattern is playing: no DISC. */
    int64_t now = 0;
    for (int i = 0; i < 3; ++i) {
        now += 5000000;
        ASSERT(ask_round(boards, 2, now, 10000) == 0);
    }
    ASSERT(boards[0].disc_emitted == 0);
    ASSERT(boards[1].disc_emitted == 0);

    /* One board loses its RAM ID: it answers the next ASK. */
    boards[1].clear_ram_id();
    ASSERT(ask_round(boards, 2, now + 5000000, 10000) == 1);
    ASSERT(boards[1].disc_emitted == 1);

    /* The master reassigns the same board ID; the following ASK is the
     * end of the startup loop (no DISC). */
    ASSERT(boards[1].on_assign(host.board_for(f2), f2) == 0);
    ASSERT(ask_round(boards, 2, now + 10000000, 10000) == 0);
    return 0;
}

static int reg_m1 = add_test("M1", test_m1_host_table);
static int reg_m2 = add_test("M2", test_m2_no_duplicate_board);
static int reg_m3 = add_test("M3", test_m3_ask_disc);
static int reg_m4 = add_test("M4", test_m4_backoff);
static int reg_m5 = add_test("M5", test_m5_assign);
static int reg_m6 = add_test("M6", test_m6_reassign_after_clear);
static int reg_m7 = add_test("M7", test_m7_periodic_ask);
