/* test_fleet_core.cpp — firmware_release_tdd.md phase R4 (F1–F16). */

#include "assert.h"
#include "fleet_core.h"
#include "release_core.h"
#include "test_list.h"

#include <string.h>
#include <vector>

static void make_uuid(int i, uint8_t u[6]) {
    u[0] = (uint8_t)(i + 1);
    u[1] = (uint8_t)(i * 7 + 3);
    u[2] = (uint8_t)(i * 13 + 5);
    u[3] = 0;
    u[4] = (uint8_t)i;
    u[5] = (uint8_t)(0xA0 + i);
}

static fl_node_t make_node(int board, int build) {
    fl_node_t n;
    memset(&n, 0, sizeof(n));
    make_uuid(board, n.uuid);
    n.board_id = board;
    n.online = true;
    n.version_valid = true;
    n.version_major = 1;
    n.version_minor = 0;
    n.version_patch = 0;
    n.current_build = (uint32_t)build;
    n.in_config = true;
    n.last_seen_ms = 1000;
    return n;
}

static std::vector<uint8_t> make_data(int seed, int n) {
    std::vector<uint8_t> d((size_t)n);
    for (int i = 0; i < n; ++i)
        d[(size_t)i] = (uint8_t)(seed * 31 + i * 7);
    return d;
}

/* Allocate the data inside the caller and point fl_image_t at it. */
static void make_image(fl_image_t *img, const std::vector<uint8_t> &data,
                       uint32_t build) {
    memset(img, 0, sizeof(*img));
    img->build = build;
    img->version_major = 1;
    img->version_minor = 4;
    img->version_patch = 2;
    strcpy(img->target, RC_TARGET_FLY);
    strcpy(img->kind, "app");
    img->data = data.data();
    img->size = (uint32_t)data.size();
    rc_sha256_hex(img->data, img->size, img->sha256);
}

static int find_tile(const fl_queue_t *q, int board) {
    for (int i = 0; i < q->set.node_count; ++i)
        if (q->set.nodes[i].board_id == board)
            return i;
    return -1;
}

static int drive_to_writing(fl_queue_t *q) {
    if (q->current < 0)
        return 1;
    const uint8_t *uuid = q->set.nodes[q->current].uuid;
    if (fl_queue_enter_result(q, 1) != 0)
        return 1;
    if (fl_queue_connect_result(q, 1, FL_APP_START, FL_BLOCK_SIZE, 1) != 0)
        return 1;
    if (fl_queue_canbus_id(q, uuid) != 0)
        return 1;
    return 0;
}

static void drive_blocks(fl_queue_t *q) {
    while (q->current >= 0 &&
           q->set.states[q->current] == FL_WRITING) {
        uint32_t addr = q->block_address;
        fl_queue_block_ack(q, addr, 1);
        if (q->status != FL_QUEUE_RUNNING)
            break;
    }
}

static int drive_one(fl_queue_t *q) {
    if (drive_to_writing(q) != 0)
        return 1;
    drive_blocks(q);
    if (fl_queue_eof_result(q, 1) != 0 ||
        fl_queue_readback_result(q, 1) != 0 ||
        fl_queue_complete_result(q, 1) != 0)
        return 1;
    fl_queue_version_result(q, q->image.build, 1);
    return 0;
}

/* F1 — image build 0, dirty, bad SHA, wrong target, factory: no command. */
static int test_f1_refuse_image(void) {
    std::vector<uint8_t> data = make_data(1, 200);
    fl_node_t nodes[1] = {make_node(0, 100)};
    fl_image_t img;
    const fl_image_t *list[1] = {&img};
    fl_queue_t q;

    make_image(&img, data, 0);
    ASSERT(fl_queue_start(&q, nodes, 1, list, 1, 0, 1000) == -1);
    ASSERT(q.log_count == 0);

    make_image(&img, data, 200);
    img.dirty = true;
    ASSERT(fl_queue_start(&q, nodes, 1, list, 1, 0, 1000) == -1);
    ASSERT(q.log_count == 0);

    make_image(&img, data, 200);
    img.sha256[0] = (img.sha256[0] == 'a') ? 'b' : 'a';
    ASSERT(fl_queue_start(&q, nodes, 1, list, 1, 0, 1000) == -1);
    ASSERT(q.log_count == 0);

    make_image(&img, data, 200);
    strcpy(img.target, RC_TARGET_PANEL);
    ASSERT(fl_queue_start(&q, nodes, 1, list, 1, 0, 1000) == -1);
    ASSERT(q.log_count == 0);

    make_image(&img, data, 200);
    strcpy(img.kind, "factory");
    ASSERT(fl_queue_start(&q, nodes, 1, list, 1, 0, 1000) == -1);
    ASSERT(q.log_count == 0);
    return 0;
}

/* F2 — dirty flag selects at a lower, equal and higher build. */
static int test_f2_dirty_selected(void) {
    std::vector<uint8_t> data = make_data(2, 128);
    fl_image_t img;
    make_image(&img, data, 200);
    const fl_image_t *list[1] = {&img};
    for (uint32_t b : {100u, 200u, 300u}) {
        fl_node_t n = make_node(4, (int)b);
        n.version_flags = 1;
        fl_set_t set;
        fl_build_set(&set, &n, 1, &img, 1000);
        ASSERT(set.selected_count == 1);
        ASSERT(set.states[0] == FL_QUEUED);
        ASSERT(set.colors[0] == FL_COLOR_RED);
    }
    return 0;
}

/* F3 — clean lower is outdated, equal is current, higher is skipped. */
static int test_f3_clean_comparison(void) {
    std::vector<uint8_t> data = make_data(3, 128);
    fl_image_t img;
    make_image(&img, data, 200);
    fl_node_t n = make_node(1, 100);
    fl_set_t set;
    fl_build_set(&set, &n, 1, &img, 1000);
    ASSERT(set.selected_count == 1);
    n.current_build = 200;
    fl_build_set(&set, &n, 1, &img, 1000);
    ASSERT(set.selected_count == 0);
    ASSERT(set.reasons[0] == FL_REASON_CURRENT);
    n.current_build = 300;
    fl_build_set(&set, &n, 1, &img, 1000);
    ASSERT(set.selected_count == 0);
    ASSERT(set.reasons[0] == FL_REASON_NEWER);
    return 0;
}

/* F4 — build 0, missing version, offline and unassigned are visible and
 * excluded, each with a reason. */
static int test_f4_exclusions(void) {
    std::vector<uint8_t> data = make_data(4, 128);
    fl_image_t img;
    make_image(&img, data, 200);
    fl_node_t n[4];
    n[0] = make_node(0, 0);
    n[1] = make_node(1, 100);
    n[1].version_valid = false;
    n[2] = make_node(2, 100);
    n[2].online = false;
    n[3] = make_node(3, 100);
    n[3].board_id = -1;
    fl_set_t set;
    fl_build_set(&set, n, 4, &img, 1000);
    ASSERT(set.node_count == 4);
    ASSERT(set.tile_count == 4);
    ASSERT(set.selected_count == 0);
    ASSERT(set.reasons[0] == FL_REASON_BUILD_ZERO);
    ASSERT(set.reasons[1] == FL_REASON_VERSION_UNKNOWN);
    ASSERT(set.reasons[2] == FL_REASON_OFFLINE);
    ASSERT(set.reasons[3] == FL_REASON_UNASSIGNED);
    for (int i = 0; i < 4; ++i) {
        ASSERT(set.states[i] == FL_SKIPPED);
        ASSERT(set.colors[i] == FL_COLOR_DIM);
    }
    return 0;
}

/* F5 — selection is ascending board id; a 31st discovery is not stored. */
static int test_f5_table(void) {
    std::vector<uint8_t> data = make_data(5, 128);
    fl_image_t img;
    make_image(&img, data, 200);
    fl_node_t n[31];
    for (int i = 0; i < 30; ++i)
        n[i] = make_node(29 - i, 100);      /* descending discovery order */
    n[30] = make_node(5, 100);
    fl_set_t set;
    fl_build_set(&set, n, 31, &img, 1000);
    ASSERT(set.table_full == true);
    ASSERT(set.node_count == 30);
    ASSERT(set.selected_count == 30);
    for (int i = 0; i < 30; ++i)
        ASSERT(set.nodes[set.selected[i]].board_id == i);
    return 0;
}

/* F6 — two manifests, same build, different SHA-256 refuse the queue. */
static int test_f6_conflict(void) {
    std::vector<uint8_t> d1 = make_data(6, 128);
    std::vector<uint8_t> d2 = make_data(7, 128);
    fl_image_t a, b;
    make_image(&a, d1, 200);
    make_image(&b, d2, 200);
    const fl_image_t *list[2] = {&a, &b};
    fl_node_t n = make_node(0, 100);
    fl_queue_t q;
    ASSERT(fl_queue_start(&q, &n, 1, list, 2, 0, 1000) == -1);
    ASSERT(q.log_count == 0);
    return 0;
}

/* F7 — one ENTER_BOOTLOADER for the current board, never field 127, and no
 * next-board command until this board is terminal. */
static int test_f7_sequence(void) {
    std::vector<uint8_t> data = make_data(8, 200);
    fl_image_t img;
    make_image(&img, data, 200);
    const fl_image_t *list[1] = {&img};
    fl_node_t n[2] = {make_node(0, 100), make_node(1, 100)};
    fl_queue_t q;
    ASSERT(fl_queue_start(&q, n, 2, list, 1, 0, 1000) == 1);
    ASSERT(q.log_count == 1);
    ASSERT(q.log[0].kind == FL_CMD_ENTER_BOOTLOADER);
    ASSERT(q.log[0].board_id == 0);
    ASSERT(fl_queue_count_cmd_for(&q, FL_CMD_ENTER_BOOTLOADER, 0) == 1);
    ASSERT(fl_queue_count_cmd_for(&q, FL_CMD_ENTER_BOOTLOADER, 1) == 0);
    ASSERT(fl_queue_count_cmd_for(&q, FL_CMD_ENTER_BOOTLOADER, 127) == 0);
    ASSERT(fl_queue_count_cmd(&q, FL_CMD_QUERY_ALL) == 0);

    ASSERT(drive_to_writing(&q) == 0);
    ASSERT(fl_queue_count_cmd_for(&q, FL_CMD_ENTER_BOOTLOADER, 1) == 0);
    drive_blocks(&q);
    ASSERT(fl_queue_eof_result(&q, 1) == 0);
    ASSERT(fl_queue_readback_result(&q, 1) == 0);
    ASSERT(fl_queue_complete_result(&q, 1) == 0);
    ASSERT(fl_queue_count_cmd_for(&q, FL_CMD_ENTER_BOOTLOADER, 1) == 0);
    ASSERT(fl_queue_version_result(&q, 200, 1) == 1);
    ASSERT(fl_queue_count_cmd_for(&q, FL_CMD_ENTER_BOOTLOADER, 1) == 1);
    return 0;
}

/* F8 — GET_CANBUS_ID differs from the row UUID: no SEND_BLOCK. */
static int test_f8_canbus_id(void) {
    std::vector<uint8_t> data = make_data(9, 200);
    fl_image_t img;
    make_image(&img, data, 200);
    const fl_image_t *list[1] = {&img};
    fl_node_t n = make_node(0, 100);
    fl_queue_t q;
    ASSERT(fl_queue_start(&q, &n, 1, list, 1, 0, 1000) == 1);
    ASSERT(fl_queue_enter_result(&q, 1) == 0);
    ASSERT(fl_queue_connect_result(&q, 1, FL_APP_START, FL_BLOCK_SIZE, 1) == 0);
    uint8_t wrong[6] = {9, 9, 9, 9, 9, 9};
    ASSERT(fl_queue_canbus_id(&q, wrong) == 0);
    ASSERT(q.status == FL_QUEUE_FAILED);
    ASSERT(fl_queue_count_cmd(&q, FL_CMD_SEND_BLOCK) == 0);
    ASSERT(q.set.colors[0] == FL_COLOR_FAILED);
    return 0;
}

/* F9 — read-back mismatch schedules the previous image and does not mark
 * the new image complete. */
static int test_f9_readback(void) {
    std::vector<uint8_t> data = make_data(10, 200);
    std::vector<uint8_t> prev = make_data(11, 200);
    fl_image_t img;
    make_image(&img, data, 200);
    img.previous = prev.data();
    img.previous_size = (uint32_t)prev.size();
    const fl_image_t *list[1] = {&img};
    fl_node_t n = make_node(0, 100);
    fl_queue_t q;
    ASSERT(fl_queue_start(&q, &n, 1, list, 1, 0, 1000) == 1);
    ASSERT(drive_to_writing(&q) == 0);
    drive_blocks(&q);
    int blocks_new = fl_queue_count_cmd(&q, FL_CMD_SEND_BLOCK);
    ASSERT(blocks_new == (int)((data.size() + 63) / 64));
    ASSERT(fl_queue_eof_result(&q, 1) == 0);
    ASSERT(fl_queue_readback_result(&q, 0) == 0);
    ASSERT(fl_queue_count_cmd(&q, FL_CMD_COMPLETE) == 0);
    ASSERT(fl_queue_count_cmd(&q, FL_CMD_WRITE_PREVIOUS) == 1);
    ASSERT(q.set.states[0] == FL_WRITING);
    ASSERT(q.write_previous == 1);
    /* the previous image is written and verified, then COMPLETE is allowed */
    drive_blocks(&q);
    ASSERT(fl_queue_eof_result(&q, 1) == 0);
    ASSERT(fl_queue_readback_result(&q, 1) == 0);
    ASSERT(fl_queue_count_cmd(&q, FL_CMD_COMPLETE) == 1);
    return 0;
}

/* F10 — a post-update build that differs fails the board and stops. */
static int test_f10_version_mismatch(void) {
    std::vector<uint8_t> data = make_data(12, 200);
    fl_image_t img;
    make_image(&img, data, 200);
    const fl_image_t *list[1] = {&img};
    fl_node_t n[2] = {make_node(0, 100), make_node(1, 100)};
    fl_queue_t q;
    ASSERT(fl_queue_start(&q, n, 2, list, 1, 0, 1000) == 1);
    ASSERT(drive_to_writing(&q) == 0);
    drive_blocks(&q);
    ASSERT(fl_queue_eof_result(&q, 1) == 0);
    ASSERT(fl_queue_readback_result(&q, 1) == 0);
    ASSERT(fl_queue_complete_result(&q, 1) == 0);
    ASSERT(fl_queue_version_result(&q, 199, 1) == 0);
    ASSERT(q.status == FL_QUEUE_FAILED);
    ASSERT(q.set.colors[find_tile(&q, 0)] == FL_COLOR_FAILED);
    ASSERT(fl_queue_count_cmd_for(&q, FL_CMD_ENTER_BOOTLOADER, 1) == 0);
    return 0;
}

/* F11 — STOP during a block lets that board finish and does not dequeue the
 * next board. */
static int test_f11_stop(void) {
    std::vector<uint8_t> data = make_data(13, 200);
    fl_image_t img;
    make_image(&img, data, 200);
    const fl_image_t *list[1] = {&img};
    fl_node_t n[2] = {make_node(0, 100), make_node(1, 100)};
    fl_queue_t q;
    ASSERT(fl_queue_start(&q, n, 2, list, 1, 0, 1000) == 1);
    ASSERT(drive_to_writing(&q) == 0);
    ASSERT(fl_queue_stop(&q) == 0);
    drive_blocks(&q);
    ASSERT(fl_queue_eof_result(&q, 1) == 0);
    ASSERT(fl_queue_readback_result(&q, 1) == 0);
    ASSERT(fl_queue_complete_result(&q, 1) == 0);
    ASSERT(fl_queue_version_result(&q, 200, 1) == 0);
    ASSERT(q.status == FL_QUEUE_STOPPED);
    ASSERT(fl_queue_count_cmd_for(&q, FL_CMD_ENTER_BOOTLOADER, 1) == 0);
    ASSERT(q.set.colors[find_tile(&q, 0)] == FL_COLOR_GREEN);
    ASSERT(q.set.colors[find_tile(&q, 1)] == FL_COLOR_RED);
    return 0;
}

/* F12 — a failed board stops the queue; later boards stay not-started. */
static int test_f12_failure_stops(void) {
    std::vector<uint8_t> data = make_data(14, 200);
    fl_image_t img;
    make_image(&img, data, 200);
    const fl_image_t *list[1] = {&img};
    fl_node_t n[3] = {make_node(0, 100), make_node(1, 100),
                      make_node(2, 100)};
    fl_queue_t q;
    ASSERT(fl_queue_start(&q, n, 3, list, 1, 0, 1000) == 1);
    ASSERT(fl_queue_enter_result(&q, 0) == 0);
    ASSERT(q.status == FL_QUEUE_FAILED);
    ASSERT(q.set.states[0] == FL_FAILED);
    ASSERT(q.set.states[1] == FL_QUEUED);
    ASSERT(q.set.states[2] == FL_QUEUED);
    ASSERT(fl_queue_count_cmd(&q, FL_CMD_ENTER_BOOTLOADER) == 1);
    return 0;
}

/* F13 — rebuilding after a reset does not resume a stored queue; a board at
 * the image build drops out and a board with no version is excluded. */
static int test_f13_rebuild(void) {
    std::vector<uint8_t> data = make_data(15, 200);
    fl_image_t img;
    make_image(&img, data, 200);
    fl_node_t n[2] = {make_node(0, 100), make_node(1, 200)};
    fl_set_t before;
    fl_build_set(&before, n, 2, &img, 1000);
    ASSERT(before.selected_count == 1);
    n[0].current_build = 200;              /* board 0 is now current */
    n[1].version_valid = false;            /* board 1 has no version */
    fl_queue_t q;
    memset(&q, 0, sizeof(q));
    q.image = img;
    fl_queue_rebuild(&q, n, 2, 2000);
    ASSERT(q.status == FL_QUEUE_IDLE);
    ASSERT(q.set.selected_count == 0);
    ASSERT(q.set.reasons[find_tile(&q, 0)] == FL_REASON_CURRENT);
    ASSERT(q.set.reasons[find_tile(&q, 1)] == FL_REASON_VERSION_UNKNOWN);
    ASSERT(q.log_count == 0);
    return 0;
}

/* F14 — the show-running flag refuses the first ENTER_BOOTLOADER. */
static int test_f14_show_running(void) {
    std::vector<uint8_t> data = make_data(16, 200);
    fl_image_t img;
    make_image(&img, data, 200);
    const fl_image_t *list[1] = {&img};
    fl_node_t n = make_node(0, 100);
    fl_queue_t q;
    ASSERT(fl_queue_start(&q, &n, 1, list, 1, 1, 1000) == -1);
    ASSERT(q.log_count == 0);
    ASSERT(fl_queue_count_cmd(&q, FL_CMD_ENTER_BOOTLOADER) == 0);
    return 0;
}

/* F15 — tile colour comes from the queue state. */
static int test_f15_tiles(void) {
    std::vector<uint8_t> data = make_data(17, 200);
    fl_image_t img;
    make_image(&img, data, 200);
    const fl_image_t *list[1] = {&img};
    fl_node_t n[30];
    for (int i = 0; i < 30; ++i)
        n[i] = make_node(i, 100);
    n[25].version_valid = false;
    fl_queue_t q;
    ASSERT(fl_queue_start(&q, n, 30, list, 1, 0, 1000) == 1);
    ASSERT(q.set.tile_count == 30);
    ASSERT(q.set.colors[25] == FL_COLOR_DIM);
    ASSERT(drive_one(&q) == 0);                          /* board 0 green */
    ASSERT(q.set.colors[find_tile(&q, 0)] == FL_COLOR_GREEN);
    ASSERT(drive_to_writing(&q) == 0);                   /* board 1 yellow */
    ASSERT(q.set.colors[find_tile(&q, 1)] == FL_COLOR_YELLOW);
    ASSERT(q.set.colors[find_tile(&q, 2)] == FL_COLOR_RED);
    ASSERT(fl_queue_yellow_count(&q) == 1);
    return 0;
}

/* F16 — exactly one yellow while running; none before start or after the
 * last board verifies. */
static int test_f16_one_yellow(void) {
    std::vector<uint8_t> data = make_data(18, 200);
    fl_image_t img;
    make_image(&img, data, 200);
    const fl_image_t *list[1] = {&img};
    fl_node_t n[3] = {make_node(0, 100), make_node(1, 100),
                      make_node(2, 100)};
    fl_set_t prior;
    fl_build_set(&prior, n, 3, &img, 1000);
    for (int i = 0; i < prior.tile_count; ++i) {
        ASSERT(prior.colors[i] == FL_COLOR_RED);
    }
    fl_queue_t q;
    ASSERT(fl_queue_start(&q, n, 3, list, 1, 0, 1000) == 1);
    ASSERT(fl_queue_yellow_count(&q) == 1);
    ASSERT(drive_one(&q) == 0);
    ASSERT(q.status == FL_QUEUE_RUNNING);
    ASSERT(fl_queue_yellow_count(&q) == 1);
    ASSERT(drive_one(&q) == 0);
    ASSERT(fl_queue_yellow_count(&q) == 1);
    ASSERT(drive_one(&q) == 0);
    ASSERT(q.status == FL_QUEUE_DONE);
    ASSERT(fl_queue_yellow_count(&q) == 0);
    return 0;
}

static int reg_f1 = add_test("R4-F1", test_f1_refuse_image);
static int reg_f2 = add_test("R4-F2", test_f2_dirty_selected);
static int reg_f3 = add_test("R4-F3", test_f3_clean_comparison);
static int reg_f4 = add_test("R4-F4", test_f4_exclusions);
static int reg_f5 = add_test("R4-F5", test_f5_table);
static int reg_f6 = add_test("R4-F6", test_f6_conflict);
static int reg_f7 = add_test("R4-F7", test_f7_sequence);
static int reg_f8 = add_test("R4-F8", test_f8_canbus_id);
static int reg_f9 = add_test("R4-F9", test_f9_readback);
static int reg_f10 = add_test("R4-F10", test_f10_version_mismatch);
static int reg_f11 = add_test("R4-F11", test_f11_stop);
static int reg_f12 = add_test("R4-F12", test_f12_failure_stops);
static int reg_f13 = add_test("R4-F13", test_f13_rebuild);
static int reg_f14 = add_test("R4-F14", test_f14_show_running);
static int reg_f15 = add_test("R4-F15", test_f15_tiles);
static int reg_f16 = add_test("R4-F16", test_f16_one_yellow);
