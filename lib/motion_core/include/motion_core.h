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

#define MC_ID_MAX      0x7FF  /* 11-bit standard identifier only */
#define MC_NODE_COUNT  30     /* short node ID 1…30              */
#define MC_AXES_PER_NODE 4

/* id = (field << 4) | type; -1 on any invalid combination:
 *   type 0,1   motion        field = motor 1…120     (I2)
 *   type 2     sync          field = 0               (I3)
 *   type 3–7   node frames   field = node 0…30       (I4)
 *   type 8,10,11 commission  field = 0               (I5)
 *   type 9     HELLO         field = node 1…30       (I5)
 *   type 12–15 reserved       rejected               (I6)
 */
int mc_id_make(int field, int type);

/* Validity and decode. An id above 0x7FF is invalid (I1). */
int mc_id_valid(int id);
int mc_id_field(int id);   /* 0…127 for any id ≤ 0x7FF */
int mc_id_type(int id);    /* 0…15 for any id ≤ 0x7FF */

/* Entity decode (I7): the field is a motor for types 0–1, a node for
 * types 3–7 and 9. The other kinds return -1. */
int mc_id_motor(int id);
int mc_id_node(int id);

/* Ownership (I8): node n owns motors 4(n−1)+1 … 4(n−1)+4.
 * Node 0 and node 31 own nothing. */
int mc_node_first_motor(int node);        /* -1 for node 0/31 */
int mc_node_owns_motor(int node, int motor);

/* Accept filter (I9): a node accepts motion for its four motors, node
 * frames addressed to itself, and field 0. A table drives all 30 nodes. */
int mc_node_accepts_id(int node, int id);

/* ------------------------------------------------------------------ */
/*  Motion payload (§4 P1/P2/P3/P10, whitepaper §4.2)                  */
/* ------------------------------------------------------------------ */

/* Pack a motion payload into exactly 8 bytes, little-endian.
 *   n == 3: [p1:16][p2:16][p3:16][v3:s16]
 *   n == 4: [p1:16][p2:16][p3:16][p4:16]
 * Returns 0 on success, -1 on error. */
int mc_pack_frame(int n, const uint16_t pos[4], int16_t speed,
                  uint8_t *buf);

/* Unpack a motion payload. len must be 8 (P3); expect_n must match the
 * variant on the wire. Returns 0 or -1; outputs untouched on error. */
int mc_unpack_frame(const uint8_t *buf, int len, int expect_n,
                    uint16_t pos[4], int16_t *speed);

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

/* ------------------------------------------------------------------ */
/*  Public C ABI stubs — implemented in later TDD slices (§6.3)        */
/* ------------------------------------------------------------------ */

/* Push a motion sample onto the trajectory buffer for one axis.
 * Returns 0 on success, -1 if rejected. */
int mc_trajectory_push(int axis_id, int64_t t_us,
                       uint16_t pos_code, int16_t speed_code);

/* Evaluate the interpolated position for one axis at a timeline tick.
 * Returns the position in micrometres (int32_t), -1 if not primed. */
int32_t mc_eval(int axis_id, int64_t t_us);

/* Compute step deltas for a 2 ms chunk at a given timeline tick.
 * Returns 0 on success, -1 if rejected. */
int mc_step_chunk(int16_t steps[4], uint16_t *ticks, int64_t t_us);

#ifdef __cplusplus
}
#endif

#endif /* MOTION_CORE_H */
