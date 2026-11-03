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
    case 0:
    case 1:   /* motion: motor 1…120 (I2) */
        return (field >= 1 && field <= 120) ? ((field << 4) | type) : -1;
    case 2:   /* sync: field 0 only (I3) */
        return (field == 0) ? 2 : -1;
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:   /* node frames: broadcast 0 or node 1…30 (I4) */
        return (field <= 30) ? ((field << 4) | type) : -1;
    case 8:
    case 10:
    case 11:  /* DISC / SET_ID A/B: field 0 only (I5) */
        return (field == 0) ? type : -1;
    case 9:   /* HELLO: short node ID 1…30 (I5) */
        return (field >= 1 && field <= 30) ? ((field << 4) | 9) : -1;
    default:  /* types 12–15 reserved (I6) */
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
    int type = mc_id_type(id);
    if (type != 0 && type != 1)
        return -1;
    int field = mc_id_field(id);
    if (field < 1 || field > 120)
        return -1;
    return field;
}

int mc_id_node(int id) {
    if (id < 0 || id > MC_ID_MAX)
        return -1;
    int type = mc_id_type(id);
    if ((type >= 3 && type <= 7) || type == 9)
        return mc_id_field(id);
    return -1;
}

int mc_node_first_motor(int node) {
    /* a table entry per short ID 0…31; the unassigned ends own nothing */
    static const int first[32] = {
        /*  0 */  0,
        /*  1 */  4 * 1 - 3,   /* 1 */
        /*  2 */  4 * 2 - 3,   /* 5 */
        /*  3 */  4 * 3 - 3,   /* 9 */
        /*  4 */  13,
        /*  5 */  17,
        /*  6 */  21,
        /*  7 */  25,
        /*  8 */  29,
        /*  9 */  33,
        /* 10 */  37,
        /* 11 */  41,
        /* 12 */  45,
        /* 13 */  49,
        /* 14 */  53,
        /* 15 */  57,
        /* 16 */  61,
        /* 17 */  65,
        /* 18 */  69,
        /* 19 */  73,
        /* 20 */  77,
        /* 21 */  81,
        /* 22 */  85,
        /* 23 */  89,
        /* 24 */  93,
        /* 25 */  97,
        /* 26 */ 101,
        /* 27 */ 105,
        /* 28 */ 109,
        /* 29 */ 113,
        /* 30 */ 117,
        /* 31 */  0,
    };
    if (node < 0 || node > 31 || node == 0 || node == 31)
        return -1;
    return first[node];
}

int mc_node_owns_motor(int node, int motor) {
    int first = mc_node_first_motor(node);
    if (first < 0 || motor < 1 || motor > 120)
        return 0;
    return (motor >= first && motor < first + MC_AXES_PER_NODE) ? 1 : 0;
}

int mc_node_accepts_id(int node, int id) {
    if (mc_id_valid(id) <= 0)
        return 0;
    if (node < 1 || node > MC_NODE_COUNT)
        return 0;

    int type = mc_id_type(id);
    int field = mc_id_field(id);

    if (type == 0 || type == 1)             /* motion for own motors */
        return mc_node_owns_motor(node, field);
    if (field == 0)                          /* any broadcast reaches us */
        return 1;
    if ((type >= 3 && type <= 7) || type == 9)
        return field == node ? 1 : 0;        /* addressed to node n */
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Motion payload (P1, P2, P3, P10; whitepaper §4.2)                  */
/* ------------------------------------------------------------------ */

static void put_u16le(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static uint16_t get_u16le(const uint8_t *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

int mc_pack_frame(int n, const uint16_t pos[4], int16_t speed,
                  uint8_t *buf) {
    if (pos == 0 || buf == 0)
        return -1;
    if (n == 3) {
        put_u16le(buf + 0, pos[0]);
        put_u16le(buf + 2, pos[1]);
        put_u16le(buf + 4, pos[2]);
        put_u16le(buf + 6, (uint16_t)speed);
        return 0;
    }
    if (n == 4) {
        put_u16le(buf + 0, pos[0]);
        put_u16le(buf + 2, pos[1]);
        put_u16le(buf + 4, pos[2]);
        put_u16le(buf + 6, pos[3]);
        return 0;
    }
    return -1;
}

int mc_unpack_frame(const uint8_t *buf, int len, int expect_n,
                    uint16_t pos[4], int16_t *speed) {
    if (buf == 0 || len != 8)
        return -1;
    if (expect_n != 3 && expect_n != 4)
        return -1;
    /* fill nothing until the inputs are known-good */
    uint16_t tmp[4];
    tmp[0] = get_u16le(buf + 0);
    tmp[1] = get_u16le(buf + 2);
    tmp[2] = get_u16le(buf + 4);
    tmp[3] = get_u16le(buf + 6);
    pos[0] = tmp[0];
    pos[1] = tmp[1];
    pos[2] = tmp[2];
    if (expect_n == 3) {
        if (speed != 0)
            *speed = (int16_t)tmp[3];
    } else {
        pos[3] = tmp[3];
    }
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

/* ------------------------------------------------------------------ */
/*  Public C ABI stubs — later slices (§6.3 whitepaper)                */
/* ------------------------------------------------------------------ */

int mc_trajectory_push(int axis_id, int64_t t_us,
                       uint16_t pos_code, int16_t speed_code) {
    (void)axis_id; (void)t_us; (void)pos_code; (void)speed_code;
    return -1; /* stub */
}

int32_t mc_eval(int axis_id, int64_t t_us) {
    (void)axis_id; (void)t_us;
    return -1; /* stub */
}

int mc_step_chunk(int16_t steps[4], uint16_t *ticks, int64_t t_us) {
    (void)steps; (void)ticks; (void)t_us;
    return -1; /* stub */
}
