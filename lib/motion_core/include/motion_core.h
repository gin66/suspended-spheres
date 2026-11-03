#ifndef MOTION_CORE_H
#define MOTION_CORE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/*  Compile-time limits (H2) — FasTimed values, whitepaper §5.3        */
/* ------------------------------------------------------------------ */

#define MC_TICKS_PER_S          16000000  /* 62.5 ns per tick     */
#define MC_MIN_CMD_TICKS        3200      /* 200 µs minimum chunk */
#define MC_MAX_STEPS_PER_CHUNK  128       /* |steps| limit        */

/* Compile-time assert verifies these right here. */
typedef char mc_static_assert_ticks[(MC_TICKS_PER_S == 16000000) ? 1 : -1];
typedef char mc_static_assert_cmd[(MC_MIN_CMD_TICKS == 3200) ? 1 : -1];
typedef char mc_static_assert_steps[(MC_MAX_STEPS_PER_CHUNK == 128) ? 1 : -1];

/* Expose constants for harness compile-time checks. */
int32_t  mc_get_ticks_per_s(void);
int32_t  mc_get_min_cmd_ticks(void);
int16_t  mc_get_max_steps_per_chunk(void);

/* ------------------------------------------------------------------ */
/*  Units and quantization (§2 of the TDD list)                        */
/* ------------------------------------------------------------------ */

/* Position code ↔ micrometre conversion:
 *   z = code × 2 000 000 / 65 535                     (exact integer form)
 *   code = round(z / 2 000 mm × 65 535)
 */
int32_t  mc_code_to_um(uint16_t code);
uint16_t mc_um_to_code(int32_t um);

/* Speed encoding: 0.04 mm/s LSB, signed 16-bit (±1310 mm/s).
 * Values past ±1 310 mm/s are rejected with -1, not wrapped. */
int16_t  mc_speed_encode(int32_t speed_um_s);
int32_t  mc_speed_decode(int16_t speed_code);

/* Wire range check: 0 … 2 000 000 µm (§4.2). */
int mc_um_in_range(int32_t um);

/* ------------------------------------------------------------------ */
/*  11-bit identifiers (§3 of the TDD list, addressing doc §2/§4)      */
/* ------------------------------------------------------------------ */

#define MC_ID_MAX       0x7FF  /* 11-bit standard identifier only */
#define MC_BOARD_COUNT  30     /* board IDs 0…29                 */
#define MC_BOARD_ALL    127    /* broadcast on status/command    */
#define MC_NODE_COUNT   30     /* legacy alias for MC_BOARD_COUNT */
#define MC_AXES_PER_NODE 4

/* id = (field << 4) | type; -1 on any invalid combination:
 *   type 0     motion        field = motor 0…119     (I2)
 *   type 1     reserved      rejected                (I2)
 *   type 2     sync          field = 0               (I3)
 *   type 3–7   board frames  field = board 0…29 or 127 (I4)
 *   type 8     DISC          field = 0               (I5)
 *   type 9     reserved      rejected                (I5)
 *   type 10    ASSIGN        field = board 0…29      (I5)
 *   type 11    ASK           field = 0               (I5)
 *   type 12    VERSION_REQ   field = board 0…29 or 127
 *   type 13    VERSION_RSP   field = board 0…29
 *   type 14–15 reserved       rejected
 */
int mc_id_make(int field, int type);

/* Validity and decode. An id above 0x7FF is invalid (I1). */
int mc_id_valid(int id);
int mc_id_field(int id);   /* 0…127 for any id ≤ 0x7FF */
int mc_id_type(int id);    /* 0…15 for any id ≤ 0x7FF */

/* Entity decode (I7): the field is a motor for type 0, a board for
 * types 3–7 and 10. The other kinds return -1. */
int mc_id_motor(int id);
int mc_id_board(int id);
int mc_id_node(int id);    /* legacy alias for mc_id_board */

/* Ownership (I8): board b owns motors 4b … 4b+3. Board 0 owns 0–3.
 * Board 29 owns 116–119. Board IDs outside 0–29 own nothing. */
int mc_board_first_motor(int board);      /* -1 outside 0…29 */
int mc_board_owns_motor(int board, int motor);
int mc_node_first_motor(int node);        /* legacy aliases */
int mc_node_owns_motor(int node, int motor);

/* Accept filter (I9): a board accepts motion (type 0) for its four
 * motors, frames addressed to it, field 127 on types 3–7, and field 0 on
 * types 2, 8 and 11. A table drives all 30 boards. */
int mc_board_accepts_id(int board, int id);
int mc_node_accepts_id(int node, int id); /* legacy alias */

/* ------------------------------------------------------------------ */
/*  Motion frame: one cubic segment per motor (whitepaper §4.2)        */
/* ------------------------------------------------------------------ */

/* Wire layout, exactly 8 bytes, little-endian:
 *   [ p0 : u16 ][ v0 : s16 ][ a0 : s16 ][ j : s16 ]
 *   p0  absolute position code (0 = 0 mm, 65535 = 2 000 mm)
 *   v0  speed, 0.04 mm/s units
 *   a0  acceleration, 1 mm/s² units
 *   j   constant jerk, 1 m/s³ units
 * Type 0 is the motion type. Type 1 is reserved and rejected. */
typedef struct {
    uint16_t p0;
    int16_t  v0;
    int16_t  a0;
    int16_t  j;
} mc_segment_t;

int mc_pack_frame(int type, const mc_segment_t *seg, uint8_t buf[8]);
int mc_unpack_frame(const uint8_t buf[8], int len, int expect_type,
                    mc_segment_t *seg);

/* Alternative per-node packing (whitepaper §4.3, TDD P9): four
 * full-range positions in one 8-byte frame, little-endian:
 *   [ p_axis0 : u16 ][ p_axis1 : u16 ][ p_axis2 ][ p_axis3 ]
 * Velocity is derived by the node from buffered frames.  Unpacking sets
 * per_node = 1, so the result cannot be mistaken for the type-0 cubic
 * per-motor baseline. */
typedef struct {
    uint16_t pos[MC_AXES_PER_NODE];
    int per_node;
} mc_node_positions_t;

int mc_pack_node_frame(const mc_node_positions_t *f, uint8_t buf[8]);
int mc_unpack_node_frame(const uint8_t buf[8], int len,
                         mc_node_positions_t *f);

/* Pure segment evaluation, shared by the stateful playback API and the
 * host node tests. τ is microseconds since the segment start. */
int32_t mc_segment_eval_um(const mc_segment_t *seg, int64_t tau_us);
int32_t mc_segment_speed_um_s(const mc_segment_t *seg, int64_t tau_us);
int32_t mc_segment_accel_um_s2(const mc_segment_t *seg, int64_t tau_us);

/* ------------------------------------------------------------------ */
/*  Sync, status and commissioning payloads (§4.5; TDD P4–P8)          */
/* ------------------------------------------------------------------ */

/* Sync / window header, type 2, field 0, exactly 8 bytes, little-endian:
 *   [0..3] t_us_low (u32) [4..5] dt_us (u16) [6] n [7] seq */
typedef struct {
    uint32_t t_us_low;
    uint16_t dt_us;
    uint8_t  n;
    uint8_t  seq;
} mc_sync_t;

int mc_pack_sync(const mc_sync_t *s, uint8_t buf[8]);
int mc_unpack_sync(const uint8_t buf[8], mc_sync_t *s);

/* Reconstruct the int64 timeline from a sequence of t_us_low values. */
typedef struct {
    uint32_t last_low;
    int64_t  high_us;
    int      valid;
} mc_sync_time_t;

void mc_sync_time_reset(mc_sync_time_t *t);
int64_t mc_sync_time_unwrap(mc_sync_time_t *t, uint32_t low);

/* Status frame, type 3, exactly 8 bytes, little-endian. */
#define MC_STATUS_IDLE     0
#define MC_STATUS_HOMING   1
#define MC_STATUS_PRIMED   2
#define MC_STATUS_PLAYING  3
#define MC_STATUS_HOLDING  4
#define MC_STATUS_FAULT    5

#define MC_STATUS_FAULT_SAMPLE_TIMEOUT  (1u << 0)
#define MC_STATUS_FAULT_SYNC_LOST       (1u << 1)
#define MC_STATUS_FAULT_OUT_OF_RANGE    (1u << 2)
#define MC_STATUS_FAULT_ACCEL           (1u << 3)
#define MC_STATUS_FAULT_UNDERRUN        (1u << 4)
#define MC_STATUS_FAULT_FOLLOWING       (1u << 5)
#define MC_STATUS_FAULT_RESET           (1u << 6)

typedef struct {
    uint8_t state;
    uint8_t fault_flags;
    uint8_t following_error[4];   /* steps, saturated at 255 */
    uint8_t buffer_depth;
    uint8_t last_seq;
} mc_status_t;

int mc_pack_status(const mc_status_t *s, uint8_t buf[8]);
int mc_unpack_status(const uint8_t buf[8], mc_status_t *s);

/* Version query and response, types 12 and 13. The request is eight zeros;
 * the response carries a semantic version and a 32-bit build id,
 * little-endian:
 *   [0] major  [1] minor  [2] patch  [3] flags  [4..7] build (u32)
 * flags bit 0 means a dirty working tree. */
#define MC_TYPE_VERSION_REQ  12
#define MC_TYPE_VERSION_RSP  13
#define MC_VERSION_FLAG_DIRTY (1u << 0)

typedef struct {
    uint8_t  major;
    uint8_t  minor;
    uint8_t  patch;
    uint8_t  flags;
    uint32_t build;
} mc_version_t;

int mc_pack_version_req(uint8_t buf[8]);
int mc_pack_version_rsp(const mc_version_t *v, uint8_t buf[8]);
int mc_unpack_version_rsp(const uint8_t buf[8], mc_version_t *v);

/* DISC and ASSIGN carry the 64-bit flash ID; ASK is eight zero bytes. */
int mc_pack_flash_id(const uint8_t flash_id[8], uint8_t buf[8]);
int mc_unpack_flash_id(const uint8_t buf[8], uint8_t flash_id[8]);
void mc_pack_ask(uint8_t buf[8]);

/* ASSIGN acceptance: store field (board 0…29) only when the payload is
 * this board's own flash ID. *board_id is -1 while unidentified. */
int mc_assign_board(int *board_id, int field, const uint8_t payload[8],
                    const uint8_t own_flash_id[8]);

/* ------------------------------------------------------------------ */
/*  Bus budget (§5 B1–B5/B7, whitepaper §3.3, §4.6, Appendix A)        */
/* ------------------------------------------------------------------ */

int32_t mc_bus_frame_cost_bits(void);   /* 130                     */
int32_t mc_bus_frame_time_us(void);     /* 130 µs at 1 Mbit/s      */
int64_t mc_bus_ceiling_frames_s(void);  /* ≈ 7 700 frames/s        */

/* Motion frames/s = round(120·1000/(N·Δt)); -1 on bad N or Δt. */
int64_t mc_bus_motion_frames_s(int n, int dt_ms);

/* Bus load in whole percent from a frames/s count:
 * percent = round(frames × 130 / 10 000). */
int mc_bus_load_percent_frames(int64_t frames_s);

/* Motion-only load for the six §3.3 cases (52/47/35/39/35/26). */
int mc_bus_load_percent(int n, int dt_ms);

/* Load with 50 Hz sync + 30×10 Hz status (57/52/39/44/40/31). */
int mc_bus_load_percent_with_overhead(int n, int dt_ms);

/* The pattern checker budget (B4). */
int mc_bus_budget_percent(void);

/* Time a frame count occupies on the bus, in µs (B5). */
int64_t mc_bus_frames_time_us(int64_t frames);

/* Window plan check (B6): start 15–20 ms before T and finish by T+Δt. */
int mc_window_plan_ok(int64_t start_us, int64_t finish_us, int64_t T_us,
                      int64_t dt_us);

/* Cadence change (B8): reject while a segment is in flight or away from a
 * pattern boundary; update *cur_n / *cur_dt_ms on success. */
int mc_cadence_change(int n, int dt_ms, int segment_in_flight,
                      int at_boundary, int *cur_n, int *cur_dt_ms);

/* ------------------------------------------------------------------ */
/*  Playback polynomial (whitepaper §4.7; TDD §6)                      */
/* ------------------------------------------------------------------ */

#define MC_AXIS_COUNT 4

enum {
    MC_AXIS_IDLE = 0,
    MC_AXIS_PLAYING = 1,
    MC_AXIS_HOLD = 2
};

#define MC_FAULT_TYPE   (1u << 0)
#define MC_FAULT_RANGE  (1u << 1)
#define MC_FAULT_ACCEL  (1u << 2)
#define MC_FAULT_JOIN   (1u << 3)
#define MC_FAULT_CHUNK  (1u << 4)

/* Submit one complete segment for one axis. tseg_us is the segment length.
 * Returns 0 when accepted, -1 when rejected (type, range, thread, join).
 * An accepted segment does not consult the previous message. */
int mc_trajectory_push(int axis_id, int type, int64_t t_start_us,
                       int64_t tseg_us, const mc_segment_t *seg);

/* Evaluate the active segment at a timeline tick. Returns micrometres,
 * -1 while the axis has no accepted segment. */
int32_t mc_eval(int axis_id, int64_t t_us);

/* Axis state and the last held position (for join and fault tests). */
int mc_axis_state(int axis_id);
int mc_axis_fault_flags(int axis_id);
int32_t mc_axis_held_um(int axis_id);

/* Clear all axis state and step accumulators. */
void mc_core_reset(void);

/* ------------------------------------------------------------------ */
/*  Steps and chunks (whitepaper §4.7, §5.3, §5.4; TDD §7)             */
/* ------------------------------------------------------------------ */

/* Q16.16 steps/mm for a 40 mm drum, 200 steps/rev.
 * Microstepping is TBD; this constant is the 1/16 worked case
 * (25.4648 steps/mm) until the count is chosen. */
#define MC_STEPS_PER_MM_Q16 1668861
int32_t mc_steps_per_mm_q16(void);

/* Round a micrometre position to the nearest whole step. */
int32_t mc_um_to_steps(int32_t um);

/* Convert whole steps back to micrometres (inverse of mc_um_to_steps). */
int32_t mc_steps_to_um(int32_t steps);

/* Compute one timed chunk ending at t_us. The tick count is
 * chunk_us × 16. Returns 0 on success, -1 when the chunk is outside the
 * FasTimed limits or a per-axis delta exceeds MC_MAX_STEPS_PER_CHUNK. */
int mc_step_chunk(int16_t steps[4], uint16_t *ticks, int64_t t_us,
                  int32_t chunk_us);

#ifdef __cplusplus
}
#endif

#endif /* MOTION_CORE_H */
