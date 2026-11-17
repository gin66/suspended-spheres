/* show_node.cpp — node playback: sync clock, pending segments, 2 ms chunks. */

#include "show_core/show_core.h"

#include <string.h>

namespace {

const int64_t kLeadUs = SHOW_SYNC_LEAD_US;
const int64_t kTimeoutUs = SHOW_SYNC_TIMEOUT_US;

bool has_motion(const show_node_t *node) {
    for (int i = 0; i < MC_AXES_PER_NODE; ++i) {
        if (node->active[i] || node->pending_count[i] > 0) return true;
    }
    return false;
}

void fault(show_node_t *node, int bits) {
    node->fault |= bits;
}

}  // namespace

void show_node_init(show_node_t *node) {
    if (node == NULL) return;
    memset(node, 0, sizeof(*node));
    for (int i = 0; i < MC_AXES_PER_NODE; ++i)
        node->position[i] = MC_POS_UNASSIGNED;
    mc_core_reset();
}

void show_node_reset(show_node_t *node) {
    if (node == NULL) return;
    uint8_t position[MC_AXES_PER_NODE];
    int valid = node->positions_valid;
    memcpy(position, node->position, sizeof(position));
    show_node_init(node);
    memcpy(node->position, position, sizeof(position));
    node->positions_valid = valid;
}

int show_node_set_posmap(show_node_t *node, const mc_posmap_t *map) {
    if (node == NULL || map == NULL) return -1;
    if (!mc_posmap_valid(map->position)) return -1;
    memcpy(node->position, map->position, sizeof(node->position));
    node->positions_valid = 1;
    return 0;
}

int64_t show_node_master_us(const show_node_t *node, int64_t local_us) {
    if (node == NULL || !node->synced) return 0;
    return local_us - node->offset_us;
}

int show_node_sync(show_node_t *node, const mc_sync_t *sync, int64_t local_us) {
    if (node == NULL || sync == NULL) return -1;
    if (sync->dt_us == 0 || sync->n == 0) return -1;

    const int64_t window_start =
        mc_sync_time_unwrap(&node->sync_time, sync->t_us_low);
    const int64_t expected = window_start - kLeadUs;

    if (!node->synced) {
        node->offset_us = local_us - expected;
        node->last_tick_us = window_start;
        node->ticking = 0;
        node->started = 0;
    } else {
        /* First-order correction.  The sync arrival carries scheduling
         * jitter, so take a small step per window instead of jumping to the
         * measured error.  35 ppm over 60 s is ~2 ms if left free-running. */
        const int64_t observed = local_us - node->offset_us;
        const int64_t drift = observed - expected;
        node->offset_us += drift / 16;
    }
    node->synced = 1;
    node->last_sync_local_us = local_us;
    node->window_start_us = window_start;
    node->tseg_us = (int64_t)sync->n * (int64_t)sync->dt_us;
    node->sync_seq = sync->seq;
    return 0;
}

int show_node_motion_motor(show_node_t *node, int motor,
                           const mc_segment_t *seg, int64_t local_us) {
    (void)local_us;
    if (node == NULL || seg == NULL) return -1;
    if (!node->synced) return 0;          /* frames before sync are dropped */
    if (motor < 0 || motor >= MC_AXES_PER_NODE) return 0;

    if (node->pending_count[motor] >= SHOW_PENDING_MAX) {
        fault(node, MC_STATUS_FAULT_UNDERRUN);
        return -1;
    }
    show_pending_t *p = &node->pending[motor][node->pending_count[motor]];
    p->seg = *seg;
    p->start_us = node->window_start_us;
    p->tseg_us = node->tseg_us;
    node->pending_count[motor]++;
    return 0;
}

int show_node_motion(show_node_t *node, int field, const mc_segment_t *seg,
                     int64_t local_us) {
    if (node == NULL) return -1;
    const int motor = mc_posmap_motor(node->position, field);
    if (motor < 0) return 0;              /* another board owns this position */
    return show_node_motion_motor(node, motor, seg, local_us);
}

int show_node_tick(show_node_t *node, int64_t local_us, show_chunk_t *out) {
    if (node == NULL || out == NULL) return -1;
    memset(out, 0, sizeof(*out));
    if (!node->synced) return 0;

    /* A missing master must hold the axes, but a finished show is not a
     * fault: there is nothing to hold. */
    if (has_motion(node) &&
        local_us - node->last_sync_local_us > kTimeoutUs) {
        fault(node, MC_STATUS_FAULT_SYNC_LOST);
        return -1;
    }

    const int64_t now = show_node_master_us(node, local_us);
    if (!node->started) {
        /* The first chunk must not be queued before T: the stepper starts
         * executing a queued timed move immediately.  Later chunks are fed
         * ahead because the queue is already running. */
        if (now < node->window_start_us) return 0;
        node->last_tick_us = node->window_start_us;
        node->started = 1;
    }
    /* Fill the stepper queue up to 10 ms ahead so a late loop iteration does
     * not leave a gap.  The lead is 20 ms, so a new window's frames are
     * always here before lookahead reaches its start. */
    if (node->last_tick_us >= now + SHOW_LOOKAHEAD_US) return 0;

    /* Activate the pending frame when its window starts and the previous
     * segment's chunks have all been emitted. */
    for (int i = 0; i < MC_AXES_PER_NODE; ++i) {
        while (node->pending_count[i] > 0) {
            show_pending_t *p = &node->pending[i][0];
            if (p->start_us > node->last_tick_us) break;
            if (node->active[i] && node->last_tick_us < node->active_end_us[i])
                break;
            if (node->active[i] && p->start_us != node->active_end_us[i]) {
                fault(node, MC_STATUS_FAULT_UNDERRUN);
                return -1;
            }
            if (mc_trajectory_push(i, 0, p->start_us, p->tseg_us, &p->seg) != 0) {
                fault(node, MC_STATUS_FAULT_FOLLOWING);
                return -1;
            }
            node->active[i] = 1;
            node->active_end_us[i] = p->start_us + p->tseg_us;
            node->pending_count[i]--;
            memmove(&node->pending[i][0], &node->pending[i][1],
                    sizeof(node->pending[i][0]) * (size_t)node->pending_count[i]);
        }
    }

    int64_t end = node->last_tick_us + SHOW_CHUNK_US;
    bool any = false;
    for (int i = 0; i < MC_AXES_PER_NODE; ++i) {
        if (!node->active[i]) continue;
        any = true;
        if (node->active_end_us[i] < end) end = node->active_end_us[i];
    }
    if (!any || end <= node->last_tick_us) return 0;

    const int32_t chunk_us = (int32_t)(end - node->last_tick_us);
    if (mc_step_chunk(out->steps, &out->ticks, end, chunk_us) != 0) {
        fault(node, MC_STATUS_FAULT_UNDERRUN);
        for (int i = 0; i < MC_AXES_PER_NODE; ++i) node->active[i] = 0;
        return -1;
    }
    node->last_tick_us = end;
    for (int i = 0; i < MC_AXES_PER_NODE; ++i) {
        if (!node->active[i]) continue;
        out->mask |= (uint8_t)(1u << i);
        if (end >= node->active_end_us[i]) node->active[i] = 0;
    }
    return 1;
}
