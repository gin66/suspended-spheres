/* motion_core.cpp — the shared integer core (host, firmware, WASM).
 *
 * No float or double lives here (H3).  All divisions are integer; the
 * derivation documents are motion_streaming_whitepaper.md and
 * Fly_Micro4_addressing.md.
 */

#include "motion_core.h"

/* ------------------------------------------------------------------ */
/*  Compile-time limits (H2)                                           */
/* ------------------------------------------------------------------ */

int32_t  mc_get_ticks_per_s(void)          { return MC_TICKS_PER_S; }
int32_t  mc_get_min_cmd_ticks(void)        { return MC_MIN_CMD_TICKS; }
int16_t  mc_get_max_steps_per_chunk(void)  { return MC_MAX_STEPS_PER_CHUNK; }

/* ------------------------------------------------------------------ */
/*  Units and quantization (U1–U8)                                     */
/* ------------------------------------------------------------------ */

int32_t  mc_code_to_um(uint16_t code) {
    return (int32_t)((int64_t)code * 2000000 / 65535);
}

uint16_t mc_um_to_code(int32_t um) {
    /* round(z / 2000 mm × 65535) = round(z × 65535 / 2 000 000) */
    int64_t r = (int64_t)um * 65535 + 1000000;
    if (um < 0)
        r = (int64_t)um * 65535 - 999999;   /* round half away from zero */
    return (uint16_t)(r / 2000000);
}

int16_t  mc_speed_encode(int32_t speed_um_s) {
    /* 0.04 µm/ms LSB; the rails are the int16 ends (U5) */
    if (speed_um_s < -1310720 || speed_um_s > 1310719)
        return -1;                          /* rejected, not wrapped */
    if (speed_um_s < 0)
        return (int16_t)(-((-speed_um_s + 20) / 40));
    if (speed_um_s > 0)
        return (int16_t)((speed_um_s + 20) / 40);
    return 0;
}

int32_t  mc_speed_decode(int16_t speed_code) {
    return (int32_t)speed_code * 40;
}

int mc_um_in_range(int32_t um) {
    return (um >= 0 && um <= 2000000) ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/*  11-bit identifiers (I1–I10, Fly_Micro4_addressing.md §2, §4)       */
/* ------------------------------------------------------------------ */

int mc_id_make(int field, int type) {
    if (field < 0 || field > 127)
        return -1;
    switch (type) {
    case 0:   /* motion: motor 0…119 (I2) */
        return (field <= 119) ? ((field << 4) | type) : -1;
    case 1:   /* reserved; reject (I2) */
        return -1;
    case 2:   /* sync: field 0 only (I3) */
        return (field == 0) ? 2 : -1;
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:   /* board frames: board 0…29, or 127 for all (I4) */
        return (field <= 29 || field == MC_BOARD_ALL)
                   ? ((field << 4) | type) : -1;
    case 8:   /* DISC: field 0 only (I5) */
        return (field == 0) ? 8 : -1;
    case 9:   /* reserved (I5) */
        return -1;
    case 10:  /* ASSIGN: board 0…29 (I5) */
        return (field <= 29) ? ((field << 4) | 10) : -1;
    case 11:  /* ASK: field 0 only (I5) */
        return (field == 0) ? 11 : -1;
    case 12:  /* VERSION_REQ: board 0…29, or 127 for all */
    case 13:  /* VERSION_RSP: board 0…29 */
        return (field <= 29 || field == MC_BOARD_ALL)
                   ? ((field << 4) | type) : -1;
    case 14:  /* HEALTH: board 0…29; 127 is not valid */
        return (field <= 29) ? ((field << 4) | type) : -1;
    default:  /* type 15 reserved */
        return -1;
    }
}

int mc_id_valid(int id) {
    if (id < 0 || id > MC_ID_MAX)
        return 0;
    return mc_id_make(mc_id_field(id), mc_id_type(id)) == id ? 1 : 0;
}

int mc_id_field(int id) {
    if (id < 0 || id > MC_ID_MAX)
        return -1;
    return (id >> 4) & 0x7F;
}

int mc_id_type(int id) {
    if (id < 0 || id > MC_ID_MAX)
        return -1;
    return id & 0x0F;
}

int mc_id_motor(int id) {
    if (id < 0 || id > MC_ID_MAX)
        return -1;
    if (mc_id_type(id) != 0)
        return -1;
    int field = mc_id_field(id);
    return (field <= 119) ? field : -1;
}

int mc_id_board(int id) {
    if (id < 0 || id > MC_ID_MAX)
        return -1;
    int type = mc_id_type(id);
    if ((type >= 3 && type <= 7) || type == 10 ||
        (type >= 12 && type <= 14))
        return mc_id_field(id);
    return -1;
}

int mc_id_node(int id) {
    return mc_id_board(id);
}

int mc_board_first_motor(int board) {
    if (board < 0 || board >= MC_BOARD_COUNT)
        return -1;
    return board * MC_AXES_PER_NODE;
}

int mc_board_owns_motor(int board, int motor) {
    int first = mc_board_first_motor(board);
    if (first < 0 || motor < 0 || motor > 119)
        return 0;
    return (motor >= first && motor < first + MC_AXES_PER_NODE) ? 1 : 0;
}

int mc_board_accepts_id(int board, int id) {
    if (mc_id_valid(id) <= 0)
        return 0;
    if (board < 0 || board >= MC_BOARD_COUNT)
        return 0;

    int type = mc_id_type(id);
    int field = mc_id_field(id);

    if (type == 0)                           /* motion for own motors */
        return mc_board_owns_motor(board, field);
    if (type == 2 || type == 8 || type == 11)
        return field == 0 ? 1 : 0;           /* system broadcasts */
    if (type >= 3 && type <= 7)
        return (field == board || field == MC_BOARD_ALL) ? 1 : 0;
    if (type == 10)                          /* ASSIGN to this board */
        return field == board ? 1 : 0;
    if (type == MC_TYPE_VERSION_REQ)
        return (field == board || field == MC_BOARD_ALL) ? 1 : 0;
    return 0;
}

int mc_node_first_motor(int node) {
    return mc_board_first_motor(node);
}

int mc_node_owns_motor(int node, int motor) {
    return mc_board_owns_motor(node, motor);
}

int mc_node_accepts_id(int node, int id) {
    return mc_board_accepts_id(node, id);
}

/* ------------------------------------------------------------------ */
/*  Motion frame: one cubic segment per motor (P1–P3, P10; §4.2)       */
/* ------------------------------------------------------------------ */

static void put_u16le(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static uint16_t get_u16le(const uint8_t *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

int mc_pack_frame(int type, const mc_segment_t *seg, uint8_t buf[8]) {
    if (seg == 0 || buf == 0 || type != 0)
        return -1;
    put_u16le(buf + 0, seg->p0);
    put_u16le(buf + 2, (uint16_t)seg->v0);
    put_u16le(buf + 4, (uint16_t)seg->a0);
    put_u16le(buf + 6, (uint16_t)seg->j);
    return 0;
}

int mc_unpack_frame(const uint8_t buf[8], int len, int expect_type,
                    mc_segment_t *seg) {
    if (buf == 0 || len != 8 || expect_type != 0 || seg == 0)
        return -1;
    mc_segment_t tmp;
    tmp.p0 = get_u16le(buf + 0);
    tmp.v0 = (int16_t)get_u16le(buf + 2);
    tmp.a0 = (int16_t)get_u16le(buf + 4);
    tmp.j  = (int16_t)get_u16le(buf + 6);
    *seg = tmp;
    return 0;
}

int mc_pack_node_frame(const mc_node_positions_t *f, uint8_t buf[8]) {
    if (f == 0 || buf == 0)
        return -1;
    for (int i = 0; i < MC_AXES_PER_NODE; ++i)
        put_u16le(buf + i * 2, f->pos[i]);
    return 0;
}

int mc_unpack_node_frame(const uint8_t buf[8], int len,
                         mc_node_positions_t *f) {
    if (buf == 0 || len != 8 || f == 0)
        return -1;
    mc_node_positions_t tmp;
    for (int i = 0; i < MC_AXES_PER_NODE; ++i)
        tmp.pos[i] = get_u16le(buf + i * 2);
    tmp.per_node = 1;
    *f = tmp;
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Sync, status and commissioning payloads (P4–P8; §4.5)              */
/* ------------------------------------------------------------------ */

static void put_u32le(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static uint32_t get_u32le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int mc_pack_sync(const mc_sync_t *s, uint8_t buf[8]) {
    if (s == 0 || buf == 0)
        return -1;
    put_u32le(buf + 0, s->t_us_low);
    put_u16le(buf + 4, s->dt_us);
    buf[6] = s->n;
    buf[7] = s->seq;
    return 0;
}

int mc_unpack_sync(const uint8_t buf[8], mc_sync_t *s) {
    if (buf == 0 || s == 0)
        return -1;
    mc_sync_t tmp;
    tmp.t_us_low = get_u32le(buf + 0);
    tmp.dt_us = get_u16le(buf + 4);
    tmp.n = buf[6];
    tmp.seq = buf[7];
    *s = tmp;
    return 0;
}

void mc_sync_time_reset(mc_sync_time_t *t) {
    if (t == 0)
        return;
    t->last_low = 0;
    t->high_us = 0;
    t->valid = 0;
}

int64_t mc_sync_time_unwrap(mc_sync_time_t *t, uint32_t low) {
    if (t == 0)
        return -1;
    if (!t->valid) {
        t->valid = 1;
        t->last_low = low;
        return (int64_t)low;
    }
    if (low < t->last_low)
        t->high_us += (int64_t)1 << 32;
    t->last_low = low;
    return t->high_us + (int64_t)low;
}

int mc_pack_status(const mc_status_t *s, uint8_t buf[8]) {
    if (s == 0 || buf == 0)
        return -1;
    buf[0] = s->state;
    buf[1] = s->fault_flags;
    for (int i = 0; i < 4; ++i)
        buf[2 + i] = s->following_error[i];
    buf[6] = s->buffer_depth;
    buf[7] = s->last_seq;
    return 0;
}

int mc_unpack_status(const uint8_t buf[8], mc_status_t *s) {
    if (buf == 0 || s == 0)
        return -1;
    mc_status_t tmp;
    tmp.state = buf[0];
    tmp.fault_flags = buf[1];
    for (int i = 0; i < 4; ++i)
        tmp.following_error[i] = buf[2 + i];
    tmp.buffer_depth = buf[6];
    tmp.last_seq = buf[7];
    *s = tmp;
    return 0;
}

static int command_action_valid(uint8_t action) {
    return action == MC_CMD_UP || action == MC_CMD_DOWN ||
           action == MC_CMD_STOP || action == MC_CMD_REIDENTIFY;
}

int mc_pack_command(const mc_command_t *c, uint8_t buf[8]) {
    if (c == 0 || buf == 0)
        return -1;
    if (!command_action_valid(c->action))
        return -1;
    if ((c->axis_mask & (uint8_t)~MC_AXIS_MASK_ALL) != 0)
        return -1;
    buf[0] = c->action;
    buf[1] = c->axis_mask;
    put_u16le(buf + 2, c->distance_mm);
    put_u16le(buf + 4, c->speed_mm_s);
    buf[6] = c->seq;
    buf[7] = 0;
    return 0;
}

int mc_unpack_command(const uint8_t buf[8], int len, mc_command_t *c) {
    if (buf == 0 || c == 0 || len != 8)
        return -1;
    if (!command_action_valid(buf[0]))
        return -1;
    if ((buf[1] & (uint8_t)~MC_AXIS_MASK_ALL) != 0)
        return -1;
    mc_command_t tmp;
    tmp.action = buf[0];
    tmp.axis_mask = buf[1];
    tmp.distance_mm = get_u16le(buf + 2);
    tmp.speed_mm_s = get_u16le(buf + 4);
    tmp.seq = buf[6];
    *c = tmp;
    return 0;
}

int mc_pack_home(uint8_t axis_mask, uint8_t buf[8]) {
    if (buf == 0 || (axis_mask & (uint8_t)~MC_AXIS_MASK_ALL) != 0)
        return -1;
    for (int i = 0; i < 8; ++i)
        buf[i] = 0;
    buf[0] = axis_mask;
    return 0;
}

int mc_unpack_home(const uint8_t buf[8], int len, uint8_t *axis_mask) {
    if (buf == 0 || axis_mask == 0 || len != 8)
        return -1;
    if ((buf[0] & (uint8_t)~MC_AXIS_MASK_ALL) != 0)
        return -1;
    *axis_mask = buf[0];
    return 0;
}

int mc_pack_version_req(uint8_t buf[8]) {
    if (buf == 0)
        return -1;
    for (int i = 0; i < 8; ++i)
        buf[i] = 0;
    return 0;
}

int mc_pack_version_rsp(const mc_version_t *v, uint8_t buf[8]) {
    if (v == 0 || buf == 0)
        return -1;
    buf[0] = v->major;
    buf[1] = v->minor;
    buf[2] = v->patch;
    buf[3] = v->flags;
    buf[4] = (uint8_t)(v->build & 0xFFu);
    buf[5] = (uint8_t)((v->build >> 8) & 0xFFu);
    buf[6] = (uint8_t)((v->build >> 16) & 0xFFu);
    buf[7] = (uint8_t)((v->build >> 24) & 0xFFu);
    return 0;
}

int mc_unpack_version_rsp(const uint8_t buf[8], mc_version_t *v) {
    if (buf == 0 || v == 0)
        return -1;
    mc_version_t tmp;
    tmp.major = buf[0];
    tmp.minor = buf[1];
    tmp.patch = buf[2];
    tmp.flags = buf[3];
    tmp.build = (uint32_t)buf[4] | ((uint32_t)buf[5] << 8) |
                ((uint32_t)buf[6] << 16) | ((uint32_t)buf[7] << 24);
    *v = tmp;
    return 0;
}

int mc_pack_health(const mc_health_t *h, uint8_t buf[8]) {
    if (h == 0 || buf == 0)
        return -1;
    buf[0] = (uint8_t)h->mcu_temp_c;
    buf[1] = (uint8_t)((h->otpw_mask & 0x0Fu) |
                       ((h->ot_mask & 0x0Fu) << 4));
    buf[2] = (uint8_t)(h->diag_mask & 0x0Fu);
    buf[3] = (uint8_t)h->ntc_t0_c;
    buf[4] = (uint8_t)h->ntc_bed_c;
    buf[5] = h->valid_mask;
    buf[6] = h->seq;
    buf[7] = 0;
    return 0;
}

int mc_unpack_health(const uint8_t buf[8], mc_health_t *h) {
    if (buf == 0 || h == 0)
        return -1;
    mc_health_t tmp;
    tmp.mcu_temp_c = (int8_t)buf[0];
    tmp.otpw_mask = (uint8_t)(buf[1] & 0x0Fu);
    tmp.ot_mask = (uint8_t)((buf[1] >> 4) & 0x0Fu);
    tmp.diag_mask = (uint8_t)(buf[2] & 0x0Fu);
    tmp.ntc_t0_c = (int8_t)buf[3];
    tmp.ntc_bed_c = (int8_t)buf[4];
    tmp.valid_mask = buf[5];
    tmp.seq = buf[6];
    *h = tmp;
    return 0;
}

int mc_pack_flash_id(const uint8_t flash_id[8], uint8_t buf[8]) {
    if (flash_id == 0 || buf == 0)
        return -1;
    for (int i = 0; i < 8; ++i)
        buf[i] = flash_id[i];
    return 0;
}

int mc_unpack_flash_id(const uint8_t buf[8], uint8_t flash_id[8]) {
    if (buf == 0 || flash_id == 0)
        return -1;
    for (int i = 0; i < 8; ++i)
        flash_id[i] = buf[i];
    return 0;
}

void mc_pack_ask(uint8_t buf[8]) {
    if (buf == 0)
        return;
    for (int i = 0; i < 8; ++i)
        buf[i] = 0;
}

int mc_assign_board(int *board_id, int field, const uint8_t payload[8],
                    const uint8_t own_flash_id[8]) {
    if (board_id == 0 || payload == 0 || own_flash_id == 0)
        return -1;
    if (field < 0 || field >= MC_BOARD_COUNT)
        return -1;
    for (int i = 0; i < 8; ++i)
        if (payload[i] != own_flash_id[i])
            return -1;
    *board_id = field;
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Bus budget (B1–B5, B7; whitepaper §3.3, §4.6, Appendix A)          */
/* ------------------------------------------------------------------ */

int32_t mc_bus_frame_cost_bits(void) {
    return 130;   /* ~111 bits + worst-case stuffing */
}

int32_t mc_bus_frame_time_us(void) {
    return 130;   /* at 1 Mbit/s */
}

int64_t mc_bus_ceiling_frames_s(void) {
    /* 1 000 000 µs / 130 µs ≈ 7692 ≈ "7 700 frames/s" */
    return 1000000 / 130;
}

int64_t mc_bus_motion_frames_s(int n, int dt_ms) {
    if ((n != 3 && n != 4) || (dt_ms != 10 && dt_ms != 11 && dt_ms != 15))
        return -1;
    /* round(120 000 / (N·Δt_ms)), denominator ≤ 60 so half-up add is 2×d/2 */
    int64_t den = (int64_t)n * dt_ms;
    return (120000 + den / 2) / den;
}

int mc_bus_load_percent_frames(int64_t frames_s) {
    if (frames_s < 0)
        return -1;
    /* percent = round(frames × 130 / 10 000) */
    return (int)((frames_s * 130 + 5000) / 10000);
}

int mc_bus_load_percent(int n, int dt_ms) {
    return mc_bus_load_percent_frames(mc_bus_motion_frames_s(n, dt_ms));
}

int mc_bus_load_percent_with_overhead(int n, int dt_ms) {
    /* 50 Hz sync + 30 × 10 Hz status = 350 frames/s */
    return mc_bus_load_percent_frames(
        mc_bus_motion_frames_s(n, dt_ms) + 350);
}

int mc_bus_budget_percent(void) {
    return 60;
}

int64_t mc_bus_frames_time_us(int64_t frames) {
    if (frames < 0)
        return -1;
    return frames * 130;
}

int mc_window_plan_ok(int64_t start_us, int64_t finish_us, int64_t T_us,
                      int64_t dt_us) {
    if (dt_us <= 0 || finish_us < start_us)
        return 0;
    int64_t lead = T_us - start_us;
    if (lead < 15000 || lead > 20000)
        return 0;
    return finish_us <= T_us + dt_us ? 1 : 0;
}

int mc_cadence_change(int n, int dt_ms, int segment_in_flight,
                      int at_boundary, int *cur_n, int *cur_dt_ms) {
    if (cur_n == 0 || cur_dt_ms == 0)
        return -1;
    if ((n != 3 && n != 4) || (dt_ms != 10 && dt_ms != 11 && dt_ms != 15))
        return -1;
    if (segment_in_flight || !at_boundary)
        return -1;
    *cur_n = n;
    *cur_dt_ms = dt_ms;
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Playback polynomial (C1–C10; whitepaper §4.7)                      */
/* ------------------------------------------------------------------ */

#define MC_UM_PER_M       1000000
#define MC_ACCEL_FLOOR_UM (-9810000)   /* −9.81 m/s² in µm/s² */

typedef struct {
    mc_segment_t seg;
    int64_t start_us;
    int64_t tseg_us;
    int state;
    int fault;
    int32_t held_um;
    int32_t step_pos;
} mc_axis_t;

static mc_axis_t g_axes[MC_AXIS_COUNT];

static int32_t abs_i32(int32_t v) {
    return v < 0 ? -v : v;
}

/* Integer Horner form, whitepaper §4.7. Division truncates toward zero. */
static int64_t seg_eval_um(const mc_segment_t *s, int64_t tau_us) {
    int64_t p = mc_code_to_um(s->p0);
    int64_t v = (int64_t)s->v0 * 40;                 /* µm/s          */
    int64_t a = (int64_t)s->a0 * 1000;               /* µm/s²         */
    int64_t j = (int64_t)s->j * MC_UM_PER_M;         /* µm/s³         */

    int64_t x = j * tau_us / 6 / MC_UM_PER_M + a / 2;
    x = x * tau_us / MC_UM_PER_M + v;
    x = x * tau_us / MC_UM_PER_M + p;
    return x;
}

static int64_t seg_speed_um_s(const mc_segment_t *s, int64_t tau_us) {
    int64_t v = (int64_t)s->v0 * 40;                 /* µm/s          */
    int64_t a = (int64_t)s->a0 * 1000;               /* µm/s²         */
    int64_t j = (int64_t)s->j * MC_UM_PER_M;         /* µm/s³         */

    /* v(τ) = v0 + a·τ/1e6 + j·τ²/(2·1e12) */
    return v + a * tau_us / MC_UM_PER_M
             + j * tau_us / MC_UM_PER_M * tau_us / 2 / MC_UM_PER_M;
}

static int32_t seg_accel_um_s2(const mc_segment_t *s, int64_t tau_us) {
    /* a(τ) = a0 + j·τ; with j in µm/s³ and τ in µs this is a0 + j_code·τ */
    return (int32_t)((int64_t)s->a0 * 1000 + (int64_t)s->j * tau_us);
}

int32_t mc_segment_eval_um(const mc_segment_t *seg, int64_t tau_us) {
    if (seg == 0 || tau_us < 0)
        return -1;
    return (int32_t)seg_eval_um(seg, tau_us);
}

int32_t mc_segment_speed_um_s(const mc_segment_t *seg, int64_t tau_us) {
    if (seg == 0 || tau_us < 0)
        return -1;
    return (int32_t)seg_speed_um_s(seg, tau_us);
}

int32_t mc_segment_accel_um_s2(const mc_segment_t *seg, int64_t tau_us) {
    if (seg == 0 || tau_us < 0)
        return -1;
    return seg_accel_um_s2(seg, tau_us);
}

void mc_core_reset(void) {
    for (int i = 0; i < MC_AXIS_COUNT; ++i) {
        g_axes[i].seg.p0 = 0;
        g_axes[i].seg.v0 = 0;
        g_axes[i].seg.a0 = 0;
        g_axes[i].seg.j  = 0;
        g_axes[i].start_us = 0;
        g_axes[i].tseg_us = 0;
        g_axes[i].state = MC_AXIS_IDLE;
        g_axes[i].fault = 0;
        g_axes[i].held_um = 0;
        g_axes[i].step_pos = 0;
    }
}

int mc_trajectory_push(int axis_id, int type, int64_t t_start_us,
                       int64_t tseg_us, const mc_segment_t *seg) {
    if (axis_id < 0 || axis_id >= MC_AXIS_COUNT || seg == 0 || tseg_us <= 0)
        return -1;

    mc_axis_t *ax = &g_axes[axis_id];

    if (type != 0) {
        ax->fault |= MC_FAULT_TYPE;
        return -1;
    }

    int32_t p0_um = mc_code_to_um(seg->p0);
    int64_t p_end = seg_eval_um(seg, tseg_us);
    int32_t a0_um = seg_accel_um_s2(seg, 0);
    int32_t a_end_um = seg_accel_um_s2(seg, tseg_us);

    if (mc_um_in_range(p0_um) == 0 || p_end < 0 || p_end > 2000000) {
        ax->fault |= MC_FAULT_RANGE;
        ax->state = MC_AXIS_HOLD;
        return -1;
    }
    if (a0_um < MC_ACCEL_FLOOR_UM || a_end_um < MC_ACCEL_FLOOR_UM) {
        ax->fault |= MC_FAULT_ACCEL;
        ax->state = MC_AXIS_HOLD;
        return -1;
    }

    if (ax->state == MC_AXIS_PLAYING) {
        int64_t prev_end = seg_eval_um(&ax->seg, ax->tseg_us);
        int32_t prev_end_code = mc_um_to_code((int32_t)prev_end);
        int32_t prev_v_code =
            mc_speed_encode((int32_t)seg_speed_um_s(&ax->seg, ax->tseg_us));
        int32_t p_gap = (int32_t)seg->p0 - prev_end_code;
        int32_t v_gap = (int32_t)seg->v0 - prev_v_code;

        if (abs_i32(p_gap) > 1 || abs_i32(v_gap) > 1) {
            ax->state = MC_AXIS_HOLD;
            ax->fault |= MC_FAULT_JOIN;
            ax->held_um = (int32_t)prev_end;
            return -1;
        }
    } else if (ax->state == MC_AXIS_HOLD) {
        int32_t held_code = mc_um_to_code(ax->held_um);
        int32_t p_gap = (int32_t)seg->p0 - held_code;
        if (abs_i32(p_gap) > 1) {
            ax->fault |= MC_FAULT_JOIN;
            return -1;
        }
    }

    ax->seg = *seg;
    ax->start_us = t_start_us;
    ax->tseg_us = tseg_us;
    ax->state = MC_AXIS_PLAYING;
    ax->fault = 0;
    ax->held_um = (int32_t)p_end;
    ax->step_pos = mc_um_to_steps(p0_um);
    return 0;
}

int32_t mc_eval(int axis_id, int64_t t_us) {
    if (axis_id < 0 || axis_id >= MC_AXIS_COUNT)
        return -1;

    mc_axis_t *ax = &g_axes[axis_id];
    if (ax->state == MC_AXIS_PLAYING) {
        int64_t tau = t_us - ax->start_us;
        if (tau < 0)
            tau = 0;
        if (tau > ax->tseg_us)
            tau = ax->tseg_us;
        return (int32_t)seg_eval_um(&ax->seg, tau);
    }
    if (ax->state == MC_AXIS_HOLD)
        return ax->held_um;
    return -1;
}

int mc_axis_state(int axis_id) {
    if (axis_id < 0 || axis_id >= MC_AXIS_COUNT)
        return -1;
    return g_axes[axis_id].state;
}

int mc_axis_fault_flags(int axis_id) {
    if (axis_id < 0 || axis_id >= MC_AXIS_COUNT)
        return 0;
    return g_axes[axis_id].fault;
}

int32_t mc_axis_held_um(int axis_id) {
    if (axis_id < 0 || axis_id >= MC_AXIS_COUNT)
        return -1;
    return g_axes[axis_id].held_um;
}

/* ------------------------------------------------------------------ */
/*  Steps and chunks (S1–S8; whitepaper §4.7, §5.3, §5.4)              */
/* ------------------------------------------------------------------ */

int32_t mc_steps_per_mm_q16(void) {
    return MC_STEPS_PER_MM_Q16;
}

static int64_t round_div(int64_t num, int64_t den) {
    if (num >= 0)
        return (num + den / 2) / den;
    return -((-num + den / 2) / den);
}

int32_t mc_um_to_steps(int32_t um) {
    /* steps = round(um / 1000 mm × steps_per_mm_q16 / 65536) */
    return (int32_t)round_div((int64_t)um * MC_STEPS_PER_MM_Q16,
                              1000LL * 65536LL);
}

int32_t mc_steps_to_um(int32_t steps) {
    /* um = round(steps × 1000 mm × 65536 / steps_per_mm_q16) */
    return (int32_t)round_div((int64_t)steps * 1000LL * 65536LL,
                              (int64_t)MC_STEPS_PER_MM_Q16);
}

int mc_step_chunk(int16_t steps[4], uint16_t *ticks, int64_t t_us,
                  int32_t chunk_us) {
    if (steps == 0 || ticks == 0 || chunk_us <= 0)
        return -1;

    int64_t tick64 = (int64_t)chunk_us * MC_TICKS_PER_S / 1000000;
    if (tick64 < MC_MIN_CMD_TICKS || tick64 > 65535)
        return -1;

    int16_t out[MC_AXIS_COUNT];
    int32_t next[MC_AXIS_COUNT];

    for (int i = 0; i < MC_AXIS_COUNT; ++i) {
        mc_axis_t *ax = &g_axes[i];
        if (ax->state != MC_AXIS_PLAYING) {
            out[i] = 0;
            next[i] = ax->step_pos;
            continue;
        }

        int32_t um = mc_eval(i, t_us);
        if (um < 0) {
            ax->state = MC_AXIS_HOLD;
            ax->fault |= MC_FAULT_RANGE;
            return -1;
        }

        int32_t target = mc_um_to_steps(um);
        int32_t delta = target - ax->step_pos;
        if (delta > MC_MAX_STEPS_PER_CHUNK || delta < -MC_MAX_STEPS_PER_CHUNK) {
            ax->state = MC_AXIS_HOLD;
            ax->fault |= MC_FAULT_CHUNK;
            ax->held_um = um;
            return -1;
        }
        out[i] = (int16_t)delta;
        next[i] = target;
    }

    for (int i = 0; i < MC_AXIS_COUNT; ++i) {
        g_axes[i].step_pos = next[i];
        steps[i] = out[i];
    }
    *ticks = (uint16_t)tick64;
    return 0;
}
