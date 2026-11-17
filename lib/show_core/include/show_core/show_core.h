#ifndef SHOW_CORE_H
#define SHOW_CORE_H

/*
 * show_core — the HBP2 show path shared by the panel master and the
 * Fly-Micro4 node.  No SD, no CAN driver, no LVGL and no floating point.
 *
 * Position model (Fly_Micro4_addressing.md §2.1): the master sends one
 * type-0 motion frame per grid position, addressed by
 * mc_position_field(position).  It does not know which board owns the
 * position.  A Fly is told, per local axis, which position it drives with a
 * PARAM/POSMAP frame and accepts only those four fields.
 *
 * A show window is N·Δt long.  The master sends a type-2 sync frame carrying
 * the window start T at T - SHOW_SYNC_LEAD_US, then the position frames.  The
 * node starts a 2 ms step-chunk timeline at T.
 */

#include "motion_core.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------- */
/*  HBP2 view over a caller-owned buffer                             */
/* ---------------------------------------------------------------- */

#define SHOW_MAX_POSITIONS 120

typedef struct {
    const uint8_t *data;        /* whole file, caller owns it */
    size_t size;
    const uint8_t *frames;      /* body: window-major 8-byte segments */
    size_t frame_bytes;

    int cols;
    int rows;
    int balls;
    int dt_ms;
    int n;
    int window_ms;
    int window_count;
    int frame_count;
    int block_count;
    uint32_t duration_ms;
    uint32_t playback_ms;
} show_hbp_t;

/* Parse and validate one HBP2 file.  Every block boundary and CRC32 is
 * checked; the body must be contiguous in window order so a frame is
 * addressed as frames[(window * balls + ball) * 8].  Returns 0 on success,
 * -1 with a short reason in `error`. */
int show_hbp_parse(const uint8_t *data, size_t size, show_hbp_t *out,
                   char *error, size_t error_cap);

int show_hbp_window_ms(const show_hbp_t *hbp);
int show_hbp_frame_count(const show_hbp_t *hbp);

/* Unpack one wire segment (little-endian p0,v0,a0,j). */
int show_hbp_segment(const show_hbp_t *hbp, int window, int ball,
                     mc_segment_t *seg);

/* True when the last window ends where the first window starts and at the
 * same speed, so CYCLE can restart without a positioning jump.  The final
 * window of a take is padded to rest, so a take that does not end at its
 * start position is correctly reported as not loopable. */
int show_hbp_loopable(const show_hbp_t *hbp, int32_t position_tol_um,
                      int32_t speed_tol_um_s);

/* ---------------------------------------------------------------- */
/*  Window plan: sync + one frame per position                       */
/* ---------------------------------------------------------------- */

#define SHOW_SYNC_LEAD_US 20000
#define SHOW_MAX_WINDOW_FRAMES (SHOW_MAX_POSITIONS + 1)

typedef struct {
    uint16_t id;
    uint8_t data[8];
} show_frame_t;

/* Fill `out` with the sync frame followed by one type-0 frame per ball, in
 * position order 0…balls-1.  `t_start_us` is the window start T in master
 * time.  Returns the frame count (1 + balls), or -1 when `cap` is too small
 * or the file is malformed. */
int show_plan_window(const show_hbp_t *hbp, int window, int64_t t_start_us,
                     uint8_t seq, show_frame_t *out, int cap);

/* ---------------------------------------------------------------- */
/*  Node playback                                                    */
/* ---------------------------------------------------------------- */

#define SHOW_CHUNK_US 2000
#define SHOW_SYNC_TIMEOUT_US 100000
#define SHOW_LOOKAHEAD_US 10000
#define SHOW_PENDING_MAX 2

typedef struct {
    mc_segment_t seg;
    int64_t start_us;
    int64_t tseg_us;
} show_pending_t;

typedef struct {
    /* Configuration */
    uint8_t position[MC_AXES_PER_NODE];
    int positions_valid;

    /* Sync clock (master = local - offset) */
    int synced;
    int64_t offset_us;
    int64_t last_sync_local_us;
    mc_sync_time_t sync_time;
    uint8_t sync_seq;

    /* Current window */
    int64_t window_start_us;
    int64_t tseg_us;

    /* Per-axis pending frames (frames arrive ~20 ms before T) */
    show_pending_t pending[MC_AXES_PER_NODE][SHOW_PENDING_MAX];
    int pending_count[MC_AXES_PER_NODE];

    /* Per-axis active segment end, and whether a segment is active */
    int64_t active_end_us[MC_AXES_PER_NODE];
    int active[MC_AXES_PER_NODE];

    int64_t last_tick_us;
    int ticking;
    int started;
    int fault;              /* MC_STATUS_FAULT_* bits for the status frame */
} show_node_t;

typedef struct {
    int16_t steps[MC_AXES_PER_NODE];
    uint16_t ticks;
    uint8_t mask;           /* bit n: axis n has an active segment */
} show_chunk_t;

void show_node_init(show_node_t *node);

/* Drop the running timeline but keep the POSMAP.  A later show starts from
 * an absolute position again. */
void show_node_reset(show_node_t *node);

int show_node_set_posmap(show_node_t *node, const mc_posmap_t *map);

/* Feed a type-2 frame.  `local_us` is the node's free-running 1 MHz clock. */
int show_node_sync(show_node_t *node, const mc_sync_t *sync, int64_t local_us);

/* Feed one type-0 frame.  Frames for positions this node does not drive are
 * ignored.  Returns 0 when accepted or ignored, -1 on an overflow/fault. */
int show_node_motion(show_node_t *node, int field, const mc_segment_t *seg,
                     int64_t local_us);

/* Same, when the caller already resolved the field to a local motor with the
 * CAN filter's position map (the firmware path). */
int show_node_motion_motor(show_node_t *node, int motor,
                           const mc_segment_t *seg, int64_t local_us);

/* Produce at most one 2 ms chunk when it is due.  Returns 1 with a chunk,
 * 0 when nothing is due, -1 on a fault (the caller stops the axes). */
int show_node_tick(show_node_t *node, int64_t local_us, show_chunk_t *out);

int64_t show_node_master_us(const show_node_t *node, int64_t local_us);

/* ---------------------------------------------------------------- */
/*  CRC32 (the HBP2 block checksum)                                  */
/* ---------------------------------------------------------------- */

uint32_t show_crc32(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* SHOW_CORE_H */
