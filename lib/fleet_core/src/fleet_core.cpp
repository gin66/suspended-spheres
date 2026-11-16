/*
 * fleet_core.cpp — outdated set, sequential Katapult queue, tile colour.
 * Pure state: time arrives as now_ms, results arrive as calls.
 */

#include "fleet_core.h"
#include "motion_core.h"

#include <string.h>

static void set_state(fl_queue_t *q, int idx, fl_state_t st,
                      fl_reason_t reason) {
    if (idx < 0 || idx >= q->set.node_count)
        return;
    q->set.states[idx] = st;
    q->set.reasons[idx] = reason;
    q->set.colors[idx] = fl_state_color(st, reason);
}

static void log_cmd(fl_queue_t *q, fl_cmd_kind_t kind, int board_id,
                    uint32_t address, int image) {
    if (q->log_count >= FL_MAX_LOG)
        return;
    fl_cmd_t *c = &q->log[q->log_count++];
    c->kind = kind;
    c->board_id = board_id;
    c->address = address;
    c->image = image;
}

const char *fl_reason_text(fl_reason_t r) {
    switch (r) {
    case FL_REASON_NONE: return "";
    case FL_REASON_SELECTED: return "outdated";
    case FL_REASON_BUILD_ZERO: return "build 0";
    case FL_REASON_VERSION_UNKNOWN: return "version unknown";
    case FL_REASON_OFFLINE: return "offline";
    case FL_REASON_UNASSIGNED: return "unassigned";
    case FL_REASON_NOT_IN_CONFIG: return "not in map";
    case FL_REASON_CURRENT: return "current";
    case FL_REASON_NEWER: return "newer than image";
    case FL_REASON_NOT_SELECTED: return "not selected";
    case FL_REASON_TABLE_FULL: return "table full";
    case FL_REASON_FAILED: return "failed";
    }
    return "";
}

const char *fl_state_text(fl_state_t s) {
    switch (s) {
    case FL_IDLE: return "idle";
    case FL_QUEUED: return "waiting";
    case FL_ENTERING: return "enter";
    case FL_CONNECTING: return "connect";
    case FL_WRITING: return "write";
    case FL_VERIFYING: return "verify";
    case FL_REBOOTING: return "reboot";
    case FL_DONE: return "done";
    case FL_FAILED: return "failed";
    case FL_SKIPPED: return "excluded";
    }
    return "";
}

const char *fl_color_text(fl_color_t c) {
    switch (c) {
    case FL_COLOR_RED: return "red";
    case FL_COLOR_YELLOW: return "yellow";
    case FL_COLOR_GREEN: return "green";
    case FL_COLOR_DIM: return "dim";
    case FL_COLOR_FAILED: return "failed";
    }
    return "";
}

fl_color_t fl_state_color(fl_state_t s, fl_reason_t reason) {
    (void)reason;
    switch (s) {
    case FL_QUEUED:
        return FL_COLOR_RED;
    case FL_ENTERING:
    case FL_CONNECTING:
    case FL_WRITING:
    case FL_VERIFYING:
    case FL_REBOOTING:
        return FL_COLOR_YELLOW;
    case FL_DONE:
        return FL_COLOR_GREEN;
    case FL_FAILED:
        return FL_COLOR_FAILED;
    case FL_SKIPPED:
    case FL_IDLE:
    default:
        return FL_COLOR_DIM;
    }
}

/* A board is outdated for this image.  The dirty flag wins over the build
 * comparison; a clean board newer than the image is skipped. */
static fl_reason_t classify(const fl_node_t *n, const fl_image_t *img,
                            uint32_t now_ms) {
    if (n->board_id < 0)
        return FL_REASON_UNASSIGNED;
    if (!n->online || (n->last_seen_ms != 0 &&
                       (uint32_t)(now_ms - n->last_seen_ms) > FL_ONLINE_MS))
        return FL_REASON_OFFLINE;
    if (!n->version_valid)
        return FL_REASON_VERSION_UNKNOWN;
    if (!n->in_config)
        return FL_REASON_NOT_IN_CONFIG;
    if (n->version_flags & 1u)
        return FL_REASON_SELECTED;         /* dirty: replace at any build */
    if (n->current_build == 0)
        return FL_REASON_BUILD_ZERO;
    if (n->current_build < img->build)
        return FL_REASON_SELECTED;
    if (n->current_build == img->build)
        return FL_REASON_CURRENT;
    return FL_REASON_NEWER;
}

void fl_build_set(fl_set_t *set, const fl_node_t *nodes, int count,
                  const fl_image_t *image, uint32_t now_ms) {
    memset(set, 0, sizeof(*set));
    set->image_build = image ? image->build : 0;
    if (count > FL_MAX_BOARDS) {
        set->table_full = true;
        count = FL_MAX_BOARDS;             /* the 31st is not stored */
    }
    if (count < 0)
        count = 0;
    set->node_count = count;
    set->tile_count = count;
    for (int i = 0; i < count; ++i) {
        set->nodes[i] = nodes[i];
        fl_reason_t r = classify(&nodes[i], image, now_ms);
        if (r == FL_REASON_SELECTED) {
            set->states[i] = FL_QUEUED;
            set->reasons[i] = r;
            set->colors[i] = FL_COLOR_RED;
            set->selected[set->selected_count++] = i;
        } else {
            set->states[i] = FL_SKIPPED;
            set->reasons[i] = r;
            set->colors[i] = FL_COLOR_DIM;
        }
    }
    /* ascending board ID */
    for (int i = 1; i < set->selected_count; ++i) {
        int v = set->selected[i];
        int j = i - 1;
        while (j >= 0 &&
               set->nodes[set->selected[j]].board_id >
                   set->nodes[v].board_id) {
            set->selected[j + 1] = set->selected[j];
            --j;
        }
        set->selected[j + 1] = v;
    }
}

static int image_valid(const fl_image_t *m) {
    if (m == 0)
        return 0;
    if (m->build == 0 || m->dirty)
        return 0;
    if (strcmp(m->target, RC_TARGET_FLY) != 0)
        return 0;
    if (strcmp(m->kind, "app") != 0)
        return 0;
    if (!rc_hex_is_sha256(m->sha256))
        return 0;
    if (m->data == 0 || m->size == 0)
        return 0;
    char sha[65];
    rc_sha256_hex(m->data, m->size, sha);
    return strcmp(sha, m->sha256) == 0;
}

int fl_queue_start(fl_queue_t *q, const fl_node_t *nodes, int count,
                   const fl_image_t *const *images, int image_count,
                   int show_running, uint32_t now_ms) {
    if (q == 0)
        return -1;
    memset(q, 0, sizeof(*q));
    q->show_running = show_running ? 1 : 0;
    q->status = FL_QUEUE_IDLE;

    int best = -1;
    for (int i = 0; i < image_count; ++i) {
        if (!image_valid(images[i]))
            continue;
        if (best < 0 || images[i]->build > images[best]->build)
            best = i;
    }
    if (best < 0)
        return -1;                         /* no usable manifest */
    uint32_t best_build = images[best]->build;
    for (int i = 0; i < image_count; ++i) {
        if (!image_valid(images[i]) || images[i]->build != best_build)
            continue;
        if (strcmp(images[i]->sha256, images[best]->sha256) != 0)
            return -1;                     /* two files claim one build */
    }
    q->image = *images[best];

    if (show_running)
        return -1;                         /* F14: no ENTER while running */

    fl_build_set(&q->set, nodes, count, &q->image, now_ms);
    if (q->set.selected_count == 0)
        return 0;
    q->status = FL_QUEUE_RUNNING;
    q->next_order = 0;
    q->current = q->set.selected[0];
    set_state(q, q->current, FL_ENTERING, FL_REASON_SELECTED);
    log_cmd(q, FL_CMD_ENTER_BOOTLOADER,
            q->set.nodes[q->current].board_id, 0, 0);
    return 1;
}

void fl_queue_rebuild(fl_queue_t *q, const fl_node_t *nodes, int count,
                      uint32_t now_ms) {
    fl_build_set(&q->set, nodes, count, &q->image, now_ms);
    q->current = -1;
    q->next_order = 0;
    q->log_count = 0;
    q->status = FL_QUEUE_IDLE;
    q->stop_requested = 0;
}

static void fail_board(fl_queue_t *q) {
    if (q->current >= 0)
        set_state(q, q->current, FL_FAILED, FL_REASON_FAILED);
    q->status = FL_QUEUE_FAILED;
}

int fl_queue_enter_result(fl_queue_t *q, int ok) {
    if (q == 0 || q->status != FL_QUEUE_RUNNING || q->current < 0)
        return -1;
    if (!ok) {
        fail_board(q);
        return 0;
    }
    log_cmd(q, FL_CMD_SET_NODEID, q->set.nodes[q->current].board_id, 0, 0);
    log_cmd(q, FL_CMD_CONNECT, q->set.nodes[q->current].board_id, 0, 0);
    set_state(q, q->current, FL_CONNECTING, FL_REASON_SELECTED);
    return 0;
}

int fl_queue_connect_result(fl_queue_t *q, int ok, uint32_t start_address,
                            uint32_t block_size, int mcu_ok) {
    if (q == 0 || q->status != FL_QUEUE_RUNNING || q->current < 0)
        return -1;
    if (!ok || start_address != FL_APP_START || block_size != FL_BLOCK_SIZE ||
        !mcu_ok) {
        fail_board(q);
        return 0;
    }
    log_cmd(q, FL_CMD_GET_CANBUS_ID, q->set.nodes[q->current].board_id, 0, 0);
    return 0;
}

int fl_queue_canbus_id(fl_queue_t *q, const uint8_t uuid[FL_UUID_LEN]) {
    if (q == 0 || q->status != FL_QUEUE_RUNNING || q->current < 0)
        return -1;
    if (!mc_uuid_equal(uuid, q->set.nodes[q->current].uuid)) {
        fail_board(q);                     /* F8: no block is sent */
        return 0;
    }
    q->write_previous = 0;
    const fl_image_t *img = &q->image;
    q->blocks_total = (img->size + FL_BLOCK_SIZE - 1) / FL_BLOCK_SIZE;
    q->blocks_done = 0;
    q->block_address = FL_APP_START;
    log_cmd(q, FL_CMD_SEND_BLOCK, q->set.nodes[q->current].board_id,
            q->block_address, 0);
    set_state(q, q->current, FL_WRITING, FL_REASON_SELECTED);
    return 0;
}

int fl_queue_block_ack(fl_queue_t *q, uint32_t address, int ok) {
    if (q == 0 || q->status != FL_QUEUE_RUNNING || q->current < 0)
        return -1;
    if (q->set.states[q->current] != FL_WRITING)
        return -1;
    if (!ok || address != q->block_address) {
        fail_board(q);
        return 0;
    }
    ++q->blocks_done;
    q->block_address += FL_BLOCK_SIZE;
    if (q->blocks_done < q->blocks_total) {
        log_cmd(q, FL_CMD_SEND_BLOCK, q->set.nodes[q->current].board_id,
                q->block_address, q->write_previous);
    } else {
        log_cmd(q, FL_CMD_EOF, q->set.nodes[q->current].board_id, 0,
                q->write_previous);
        set_state(q, q->current, FL_VERIFYING, FL_REASON_SELECTED);
    }
    return 0;
}

int fl_queue_eof_result(fl_queue_t *q, int ok) {
    if (q == 0 || q->status != FL_QUEUE_RUNNING || q->current < 0)
        return -1;
    if (!ok) {
        fail_board(q);
        return 0;
    }
    log_cmd(q, FL_CMD_REQUEST_BLOCK, q->set.nodes[q->current].board_id,
            FL_APP_START, q->write_previous);
    set_state(q, q->current, FL_VERIFYING, FL_REASON_SELECTED);
    return 0;
}

int fl_queue_readback_result(fl_queue_t *q, int match) {
    if (q == 0 || q->status != FL_QUEUE_RUNNING || q->current < 0)
        return -1;
    if (match) {
        log_cmd(q, FL_CMD_COMPLETE, q->set.nodes[q->current].board_id, 0,
                q->write_previous);
        set_state(q, q->current, FL_REBOOTING, FL_REASON_SELECTED);
        return 0;
    }
    if (!q->write_previous && q->image.previous != 0 &&
        q->image.previous_size > 0) {
        /* The new image failed read-back.  Write the previous image back
         * before any COMPLETE; the failed new image is not marked complete. */
        q->write_previous = 1;
        q->blocks_total =
            (q->image.previous_size + FL_BLOCK_SIZE - 1) / FL_BLOCK_SIZE;
        q->blocks_done = 0;
        q->block_address = FL_APP_START;
        log_cmd(q, FL_CMD_WRITE_PREVIOUS, q->set.nodes[q->current].board_id,
                0, 1);
        log_cmd(q, FL_CMD_SEND_BLOCK, q->set.nodes[q->current].board_id,
                q->block_address, 1);
        set_state(q, q->current, FL_WRITING, FL_REASON_SELECTED);
        return 0;
    }
    fail_board(q);
    return 0;
}

int fl_queue_complete_result(fl_queue_t *q, int ok) {
    if (q == 0 || q->status != FL_QUEUE_RUNNING || q->current < 0)
        return -1;
    if (!ok) {
        fail_board(q);
        return 0;
    }
    log_cmd(q, FL_CMD_ASSIGN, q->set.nodes[q->current].board_id, 0, 0);
    log_cmd(q, FL_CMD_VERSION_REQ, q->set.nodes[q->current].board_id, 0, 0);
    set_state(q, q->current, FL_REBOOTING, FL_REASON_SELECTED);
    return 0;
}

int fl_queue_version_result(fl_queue_t *q, uint32_t build, int answered) {
    if (q == 0 || q->status != FL_QUEUE_RUNNING || q->current < 0)
        return -1;
    if (!answered || build != q->image.build) {
        fail_board(q);                     /* F10: no next board */
        return 0;
    }
    set_state(q, q->current, FL_DONE, FL_REASON_SELECTED);
    if (q->stop_requested) {
        q->status = FL_QUEUE_STOPPED;      /* F11: do not dequeue the next */
        return 0;
    }
    if (q->next_order + 1 < q->set.selected_count) {
        ++q->next_order;
        q->current = q->set.selected[q->next_order];
        set_state(q, q->current, FL_ENTERING, FL_REASON_SELECTED);
        log_cmd(q, FL_CMD_ENTER_BOOTLOADER,
                q->set.nodes[q->current].board_id, 0, 0);
        return 1;
    }
    q->status = FL_QUEUE_DONE;
    return 0;
}

int fl_queue_stop(fl_queue_t *q) {
    if (q == 0)
        return -1;
    q->stop_requested = 1;
    if (q->status == FL_QUEUE_RUNNING && q->current < 0)
        q->status = FL_QUEUE_STOPPED;
    return 0;
}

int fl_queue_active(const fl_queue_t *q) {
    if (q == 0)
        return 0;
    if (q->status == FL_QUEUE_RUNNING)
        return 1;
    if (q->status == FL_QUEUE_STOPPED && q->current >= 0) {
        fl_state_t s = q->set.states[q->current];
        return (s == FL_ENTERING || s == FL_CONNECTING || s == FL_WRITING ||
                s == FL_VERIFYING || s == FL_REBOOTING) ? 1 : 0;
    }
    return 0;
}

int fl_queue_yellow_count(const fl_queue_t *q) {
    if (q == 0)
        return 0;
    int n = 0;
    for (int i = 0; i < q->set.tile_count; ++i)
        if (q->set.colors[i] == FL_COLOR_YELLOW)
            ++n;
    return n;
}

int fl_queue_count_cmd(const fl_queue_t *q, fl_cmd_kind_t kind) {
    if (q == 0)
        return 0;
    int n = 0;
    for (int i = 0; i < q->log_count; ++i)
        if (q->log[i].kind == kind)
            ++n;
    return n;
}

int fl_queue_count_cmd_for(const fl_queue_t *q, fl_cmd_kind_t kind,
                           int board_id) {
    if (q == 0)
        return 0;
    int n = 0;
    for (int i = 0; i < q->log_count; ++i)
        if (q->log[i].kind == kind && q->log[i].board_id == board_id)
            ++n;
    return n;
}

uint8_t fl_queue_progress(const fl_queue_t *q) {
    if (q == 0 || q->blocks_total == 0)
        return 0;
    if (q->blocks_done >= q->blocks_total)
        return 100;
    return (uint8_t)((uint64_t)q->blocks_done * 100u / q->blocks_total);
}
