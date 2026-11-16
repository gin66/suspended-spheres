/* test_panel_core.cpp — firmware_release_tdd.md R6 (G1–G2). */

#include "assert.h"
#include "panel_core.h"
#include "test_list.h"

#include <string.h>

/* G1 — version 2 packs and unpacks; a version 1 blob does not pass and
 * does not yield a board build. */
static int test_g1_config_v2(void) {
    pc_config_v2_t cfg, out;
    pc_config_v2_defaults(&cfg);
    const uint8_t u0[PC_UUID_LEN] = {1, 2, 3, 4, 5, 6};
    const uint8_t u1[PC_UUID_LEN] = {7, 8, 9, 10, 11, 12};
    cfg.grid_cols = 10;
    cfg.grid_rows = 12;
    cfg.positions[3].assigned = true;
    memcpy(cfg.positions[3].uuid, u0, PC_UUID_LEN);
    cfg.positions[3].axis = 2;
    ASSERT(pc_config_v2_set_board(&cfg, u0, 4, 1794839525u, 0, "build 1794839525"));
    ASSERT(pc_config_v2_set_board(&cfg, u1, 5, 100u, 1, "dirty"));

    uint8_t blob[4096];
    ASSERT(sizeof(pc_config_v2_t) <= sizeof(blob));
    ASSERT(pc_config_v2_pack(&cfg, blob, sizeof(blob)) == 0);
    ASSERT(pc_config_v2_unpack(blob, sizeof(pc_config_v2_t), &out) == 0);
    ASSERT(out.grid_cols == 10 && out.grid_rows == 12);
    ASSERT(pc_config_v2_find_position(&out, u0, 2) == 3);
    ASSERT(pc_config_v2_get_board_id(&out, u0) == 4);
    ASSERT(pc_config_v2_get_board_id(&out, u1) == 5);
    ASSERT(out.boards[0].last_build == 1794839525u ||
           out.boards[1].last_build == 1794839525u);
    ASSERT(strcmp(out.boards[0].last_result, "dirty") == 0 ||
           strcmp(out.boards[1].last_result, "dirty") == 0);

    /* a version-1 file must not pass the version-2 check */
    pc_config_v2_t v1 = cfg;
    v1.version = 1;
    uint8_t v1blob[sizeof(pc_config_v2_t)];
    memcpy(v1blob, &v1, sizeof(v1blob));
    pc_config_v2_t bad;
    memset(&bad, 0xFF, sizeof(bad));
    ASSERT(pc_config_v2_unpack(v1blob, sizeof(v1blob), &bad) < 0);
    ASSERT(bad.boards[0].last_build == 0xFFFFFFFFu);  /* untouched */
    return 0;
}

/* G2 — QUERY ALL clears version_valid, waits and retries only the missing
 * ids, three times.  Time is supplied, never slept. */
static int test_g2_version_query(void) {
    const int ids[3] = {0, 1, 2};
    pc_vquery_t q;
    pc_vquery_start(&q, ids, 3, 1000);
    ASSERT(q.missing_count == 3);
    ASSERT(q.attempt == 1);
    ASSERT(q.deadline_ms == 1000 + PC_QUERY_WAIT_MS);

    pc_vquery_response(&q, 0);
    ASSERT(q.missing_count == 2);
    ASSERT(!pc_vquery_is_missing(&q, 0));
    ASSERT(pc_vquery_is_missing(&q, 1));

    ASSERT(pc_vquery_poll(&q, 1000 + PC_QUERY_WAIT_MS - 1) == 0);
    ASSERT(pc_vquery_poll(&q, 1000 + PC_QUERY_WAIT_MS) == 1);
    ASSERT(q.attempt == 2);
    ASSERT(q.missing_count == 2);

    pc_vquery_response(&q, 1);
    ASSERT(q.missing_count == 1);
    ASSERT(pc_vquery_is_missing(&q, 2));
    ASSERT(pc_vquery_poll(&q, 1000 + 2 * PC_QUERY_WAIT_MS) == 1);
    ASSERT(q.attempt == 3);
    ASSERT(pc_vquery_poll(&q, 1000 + 3 * PC_QUERY_WAIT_MS) == -1);
    ASSERT(q.done);
    ASSERT(q.missing_count == 1);
    ASSERT(pc_vquery_is_missing(&q, 2));
    return 0;
}

static int reg_g1 = add_test("R6-G1", test_g1_config_v2);
static int reg_g2 = add_test("R6-G2", test_g2_version_query);
