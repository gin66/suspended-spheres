#include "panel_core.h"

#include <string.h>

uint32_t pc_crc32(const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= p[i];
        for (int b = 0; b < 8; ++b)
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
    }
    return ~crc;
}

void pc_config_v2_defaults(pc_config_v2_t *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->magic = PC_CONFIG_MAGIC;
    cfg->version = PC_CONFIG_VERSION;
    cfg->grid_cols = 10;
    cfg->grid_rows = 12;
    cfg->test_drop_mm = 100;
    cfg->test_speed_mm_s = 20;
    cfg->test_step_delay_ms = 300;
    cfg->test_serpentine = 1;
    cfg->brightness = 100;
    for (int i = 0; i < PC_MAX_BOARDS; ++i)
        cfg->boards[i].board_id = -1;
}

static uint32_t cfg_crc(const pc_config_v2_t *cfg) {
    return pc_crc32(cfg, offsetof(pc_config_v2_t, crc));
}

int pc_config_v2_pack(const pc_config_v2_t *cfg, uint8_t *out, size_t cap) {
    if (cfg == 0 || out == 0 || cap < sizeof(pc_config_v2_t))
        return -1;
    pc_config_v2_t tmp = *cfg;
    tmp.crc = cfg_crc(&tmp);
    memcpy(out, &tmp, sizeof(tmp));
    return 0;
}

int pc_config_v2_unpack(const uint8_t *blob, size_t len, pc_config_v2_t *out) {
    if (blob == 0 || out == 0 || len != sizeof(pc_config_v2_t))
        return -1;
    pc_config_v2_t tmp;
    memcpy(&tmp, blob, sizeof(tmp));
    if (tmp.magic != PC_CONFIG_MAGIC)
        return -1;
    if (tmp.version != PC_CONFIG_VERSION)
        return -1;                     /* version 1 is "version unknown" */
    if (tmp.grid_cols < 1 || tmp.grid_cols > PC_GRID_MAX_COLS)
        return -1;
    if (tmp.grid_rows < 1 || tmp.grid_rows > PC_GRID_MAX_ROWS)
        return -1;
    if (tmp.crc != cfg_crc(&tmp))
        return -1;
    for (int i = 0; i < PC_MAX_BOARDS; ++i) {
        if (tmp.boards[i].board_id < -1 ||
            tmp.boards[i].board_id >= PC_MAX_BOARDS)
            return -1;
    }
    *out = tmp;
    return 0;
}

int pc_config_v2_find_position(const pc_config_v2_t *cfg,
                               const uint8_t uuid[PC_UUID_LEN], uint8_t axis) {
    if (cfg == 0 || uuid == 0 || axis >= 4)
        return -1;
    int cap = (int)cfg->grid_cols * (int)cfg->grid_rows;
    if (cap > PC_MAX_POSITIONS)
        cap = PC_MAX_POSITIONS;
    for (int i = 0; i < cap; ++i) {
        const pc_position_t *p = &cfg->positions[i];
        if (p->assigned && p->axis == axis &&
            memcmp(p->uuid, uuid, PC_UUID_LEN) == 0)
            return i;
    }
    return -1;
}

int pc_config_v2_get_board_id(const pc_config_v2_t *cfg,
                              const uint8_t uuid[PC_UUID_LEN]) {
    if (cfg == 0 || uuid == 0)
        return -1;
    for (int i = 0; i < PC_MAX_BOARDS; ++i) {
        const pc_board_t *b = &cfg->boards[i];
        if (b->board_id >= 0 &&
            memcmp(b->uuid, uuid, PC_UUID_LEN) == 0)
            return b->board_id;
    }
    return -1;
}

bool pc_config_v2_set_board(pc_config_v2_t *cfg,
                            const uint8_t uuid[PC_UUID_LEN], int board_id,
                            uint32_t build, uint8_t flags,
                            const char *result) {
    if (cfg == 0 || uuid == 0 || board_id < -1 || board_id >= PC_MAX_BOARDS)
        return false;
    pc_board_t *slot = 0;
    for (int i = 0; i < PC_MAX_BOARDS; ++i)
        if (memcmp(cfg->boards[i].uuid, uuid, PC_UUID_LEN) == 0) {
            slot = &cfg->boards[i];
            break;
        }
    if (slot == 0) {
        for (int i = 0; i < PC_MAX_BOARDS; ++i)
            if (cfg->boards[i].board_id < 0 &&
                cfg->boards[i].uuid[0] == 0 &&
                cfg->boards[i].uuid[1] == 0) {
                slot = &cfg->boards[i];
                break;
            }
    }
    if (slot == 0)
        return false;
    memcpy(slot->uuid, uuid, PC_UUID_LEN);
    slot->board_id = (int8_t)board_id;
    slot->last_build = build;
    slot->version_flags = flags;
    if (result) {
        size_t n = strlen(result);
        if (n >= sizeof(slot->last_result))
            n = sizeof(slot->last_result) - 1;
        memcpy(slot->last_result, result, n);
        slot->last_result[n] = '\0';
    } else {
        slot->last_result[0] = '\0';
    }
    return true;
}

/* ------------------------------------------------------------------ */
/*  QUERY ALL retry                                                    */
/* ------------------------------------------------------------------ */

void pc_vquery_start(pc_vquery_t *q, const int *board_ids, int count,
                     uint32_t now_ms) {
    memset(q, 0, sizeof(*q));
    if (count > PC_MAX_BOARDS)
        count = PC_MAX_BOARDS;
    for (int i = 0; i < count; ++i)
        if (board_ids[i] >= 0)
            q->missing[q->missing_count++] = board_ids[i];
    q->attempt = 1;
    q->deadline_ms = now_ms + PC_QUERY_WAIT_MS;
    q->done = q->missing_count == 0;
}

void pc_vquery_response(pc_vquery_t *q, int board_id) {
    if (q == 0)
        return;
    for (int i = 0; i < q->missing_count; ++i) {
        if (q->missing[i] == board_id) {
            for (int j = i; j + 1 < q->missing_count; ++j)
                q->missing[j] = q->missing[j + 1];
            --q->missing_count;
            if (q->missing_count == 0)
                q->done = true;
            return;
        }
    }
}

int pc_vquery_poll(pc_vquery_t *q, uint32_t now_ms) {
    if (q == 0 || q->done)
        return -1;
    if ((int32_t)(now_ms - q->deadline_ms) < 0)
        return 0;
    if (q->missing_count == 0) {
        q->done = true;
        return -1;
    }
    if (q->attempt >= PC_QUERY_ATTEMPTS) {
        q->done = true;
        return -1;
    }
    ++q->attempt;
    q->deadline_ms += PC_QUERY_WAIT_MS;
    return 1;
}

int pc_vquery_missing_count(const pc_vquery_t *q) {
    return q ? q->missing_count : 0;
}

bool pc_vquery_is_missing(const pc_vquery_t *q, int board_id) {
    if (q == 0)
        return false;
    for (int i = 0; i < q->missing_count; ++i)
        if (q->missing[i] == board_id)
            return true;
    return false;
}

/* ------------------------------------------------------------------ */
/*  SD listing and panel OTA policy                                    */
/* ------------------------------------------------------------------ */

int pc_image_list_add(pc_image_list_t *list, const rc_manifest_t *m,
                      const char *file_sha256) {
    if (list == 0 || m == 0 || file_sha256 == 0)
        return -1;
    if (list->count >= PC_MAX_IMAGES)
        return -1;
    if (strcmp(m->sha256, file_sha256) != 0)
        return -1;                     /* broken file in the listing */
    list->entries[list->count++] = *m;
    return 0;
}

const rc_manifest_t *pc_image_select_panel(const pc_image_list_t *list) {
    if (list == 0)
        return 0;
    const rc_manifest_t *ptrs[PC_MAX_IMAGES];
    for (int i = 0; i < list->count; ++i)
        ptrs[i] = &list->entries[i];
    const rc_manifest_t *out = 0;
    if (rc_select_highest(ptrs, list->count, 1, &out) != 1)
        return 0;
    return out;
}

const rc_manifest_t *pc_image_select_fly(const pc_image_list_t *list) {
    if (list == 0)
        return 0;
    const rc_manifest_t *ptrs[PC_MAX_IMAGES];
    for (int i = 0; i < list->count; ++i)
        ptrs[i] = &list->entries[i];
    const rc_manifest_t *out = 0;
    if (rc_select_highest(ptrs, list->count, 0, &out) != 1)
        return 0;
    return out;
}

const rc_manifest_t *pc_image_find_factory(const pc_image_list_t *list) {
    if (list == 0)
        return 0;
    for (int i = 0; i < list->count; ++i) {
        const rc_manifest_t *m = &list->entries[i];
        if (strcmp(m->target, RC_TARGET_PANEL) == 0 &&
            strcmp(m->kind, "factory") == 0)
            return m;
    }
    return 0;
}

pc_ota_result_t pc_ota_check(const pc_ota_request_t *req,
                             const rc_manifest_t *m, uint32_t body_size) {
    if (req == 0 || m == 0)
        return PC_OTA_BAD_MANIFEST;
    if (req->show_running || req->queue_active)
        return PC_OTA_BUSY;
    if (!rc_manifest_ota_ok(m))
        return PC_OTA_BAD_MANIFEST;
    if (req->max_size != 0 && body_size > req->max_size)
        return PC_OTA_TOO_LARGE;
    if (req->running_partition_sha256 != 0 &&
        strcmp(req->running_partition_sha256, m->partition_table_sha256) != 0)
        return PC_OTA_BAD_PARTITION;
    return PC_OTA_OK;
}

pc_ota_result_t pc_ota_run(const pc_ota_request_t *req,
                           const rc_manifest_t *m,
                           const uint8_t *data, size_t len, bool self_test_ok,
                           const pc_ota_callbacks_t *cb) {
    pc_ota_result_t r = pc_ota_check(req, m, (uint32_t)len);
    if (r != PC_OTA_OK)
        return r;
    if (cb == 0 || cb->write == 0)
        return PC_OTA_WRITE_FAILED;
    if (cb->write(cb->ctx, data, len) != 0)
        return PC_OTA_WRITE_FAILED;
    char sha[65];
    rc_sha256_hex(data, len, sha);
    if (strcmp(sha, m->sha256) != 0)
        return PC_OTA_HASH_MISMATCH;
    if (!self_test_ok)
        return PC_OTA_OK;              /* slot stays unmarked */
    if (cb->mark_valid != 0 && cb->mark_valid(cb->ctx) != 0)
        return PC_OTA_WRITE_FAILED;
    return PC_OTA_OK;
}
