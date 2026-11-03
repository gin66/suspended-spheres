/* bake.cpp — deterministic integer layer bake and fidelity helpers. */

#include "pattern_core/bake.h"
#include "sine_table.h"

#include "motion_core.h"

#include <stdint.h>

namespace pc {
namespace {

/* ------------------------------------------------------------------ */
/*  Deterministic integer wave helpers                                 */
/* ------------------------------------------------------------------ */

const int64_t PHASE_FULL = 4294967296LL;   /* 2^32 */

int32_t sin_q15(uint32_t phase) {
    int idx = (int)((phase >> 22) & 1023u);
    int32_t frac = (int32_t)((phase >> 10) & 0xFFFu);
    int32_t a = PC_SIN_Q15[idx];
    int32_t b = PC_SIN_Q15[idx + 1];
    return a + (int32_t)(((int64_t)(b - a) * frac) / 4096);
}

int64_t pos_mod(int64_t a, int64_t m) {
    if (m <= 0)
        return 0;
    int64_t r = a % m;
    return r < 0 ? r + m : r;
}

uint32_t time_phase(int64_t t_ms, int period_ms) {
    if (period_ms <= 0)
        return 0;
    int64_t t = pos_mod(t_ms, period_ms);
    return (uint32_t)((t * PHASE_FULL) / period_ms);
}

uint32_t spatial_phase(int index, int wavelength, int direction) {
    if (wavelength <= 0)
        return 0;
    int64_t p = ((int64_t)index * PHASE_FULL) / wavelength;
    if (direction < 0)
        p = -p;
    return (uint32_t)p;
}

uint32_t deg_phase(int32_t deg) {
    return (uint32_t)(((int64_t)pos_mod(deg, 360) * PHASE_FULL) / 360);
}

int32_t wave(int32_t amplitude_um, uint32_t phase) {
    if (amplitude_um == 0)
        return 0;
    return (int32_t)(((int64_t)amplitude_um * sin_q15(phase)) / 32767);
}

uint32_t hash32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

int32_t random_waypoint(uint32_t seed, int ball, int k) {
    uint32_t h = hash32(seed ^ hash32((uint32_t)ball * 0x9e3779b9u ^
                                      (uint32_t)k * 0x85ebca6bu));
    return (int32_t)(h % 65536u) - 32768;
}

int32_t random_walk_offset(uint32_t seed, int ball, int64_t t_ms,
                           int period_ms, int32_t amplitude_um) {
    if (period_ms <= 0 || amplitude_um == 0)
        return 0;
    int64_t block = t_ms / period_ms;
    int64_t frac = pos_mod(t_ms, period_ms);
    int32_t a = random_waypoint(seed, ball, (int)block);
    int32_t b = random_waypoint(seed, ball, (int)block + 1);
    /* smoothstep in Q16 */
    int64_t s = (frac * 65536) / period_ms;
    int64_t smooth = s * s * (3 * 65536 - 2 * s) / (65536LL * 65536LL);
    int32_t mixed = a + (int32_t)(((int64_t)(b - a) * smooth) / 65536);
    return (int32_t)(((int64_t)mixed * amplitude_um) / 32768);
}

int32_t keyframes_offset(const Layer &layer, int64_t t_ms) {
    const auto &k = layer.keys;
    if (k.empty())
        return 0;
    if (t_ms <= k.front().t_ms)
        return k.front().pos_um;
    if (t_ms >= k.back().t_ms)
        return k.back().pos_um;
    for (size_t i = 1; i < k.size(); ++i) {
        if (t_ms <= k[i].t_ms) {
            int64_t span = k[i].t_ms - k[i - 1].t_ms;
            if (span <= 0)
                return k[i].pos_um;
            int64_t dp = (int64_t)k[i].pos_um - k[i - 1].pos_um;
            return (int32_t)(k[i - 1].pos_um + dp * (t_ms - k[i - 1].t_ms) /
                                              span);
        }
    }
    return k.back().pos_um;
}

int isqrt_i32(int32_t v) {
    if (v <= 0)
        return 0;
    int32_t x = v;
    int32_t y = (x + 1) / 2;
    while (y < x) {
        x = y;
        y = (x + v / x) / 2;
    }
    return x;
}

bool in_group(const Group &g, int ball, int cols, int rows) {
    int col = cols > 0 ? ball % cols : 0;
    int row = cols > 0 ? ball / cols : 0;
    switch (g.kind) {
    case GROUP_ALL: return true;
    case GROUP_ROW: return row == g.index;
    case GROUP_COLUMN: return col == g.index;
    case GROUP_RING: {
        int dc = col - g.center_col;
        int dr = row - g.center_row;
        if (dc < 0) dc = -dc;
        if (dr < 0) dr = -dr;
        return (dc > dr ? dc : dr) == g.radius;
    }
    case GROUP_CHECKERBOARD: return (((col + row) & 1) == g.parity);
    case GROUP_MASK:
        for (int b : g.balls)
            if (b == ball)
                return true;
        return false;
    }
    (void)rows;
    return true;
}

int32_t layer_offset(const Layer &layer, int col, int row, int64_t t_ms,
                     int cols, int rows, int ball) {
    switch (layer.op) {
    case OP_KEYFRAMES:
        return keyframes_offset(layer, t_ms);
    case OP_SINE: {
        uint32_t ph = time_phase(t_ms, layer.period_ms);
        int idx = (layer.axis == 0) ? col : row;
        ph += spatial_phase(idx, layer.wavelength_cols, 1);
        ph += deg_phase(layer.phase_deg);
        return wave(layer.amplitude_um, ph);
    }
    case OP_TRAVELLING_WAVE: {
        uint32_t ph = time_phase(t_ms, layer.period_ms);
        int idx = (layer.axis == 0) ? col : row;
        ph += spatial_phase(idx, layer.wavelength_cols, layer.direction);
        ph += deg_phase(layer.phase_deg);
        return wave(layer.amplitude_um, ph);
    }
    case OP_INTERFERENCE: {
        uint32_t ph1 = time_phase(t_ms, layer.period_ms) +
                       spatial_phase(layer.axis == 0 ? col : row,
                                     layer.wavelength_cols, 1) +
                       deg_phase(layer.phase_deg);
        uint32_t ph2 = time_phase(t_ms, layer.period2_ms) +
                       spatial_phase(layer.axis2 == 0 ? col : row,
                                     layer.wavelength2_cols, 1) +
                       deg_phase(layer.phase2_deg);
        return wave(layer.amplitude_um, ph1) +
               wave(layer.amplitude2_um, ph2);
    }
    case OP_SPIRAL:
    case OP_RIPPLE: {
        int dc = col - layer.group.center_col;
        int dr = row - layer.group.center_row;
        int r = isqrt_i32(dc * dc + dr * dr);
        uint32_t ph = time_phase(t_ms, layer.period_ms) +
                       spatial_phase(r, layer.wavelength_cols, 1) +
                       deg_phase(layer.phase_deg);
        return wave(layer.amplitude_um, ph);
    }
    case OP_CASCADE: {
        int64_t t = t_ms - (int64_t)(col + row) * (layer.phase_deg > 0
                                                       ? layer.phase_deg
                                                       : 1);
        uint32_t ph = time_phase(t, layer.period_ms);
        return wave(layer.amplitude_um, ph);
    }
    case OP_RANDOM_WALK:
        return random_walk_offset(layer.seed, ball, t_ms, layer.period_ms,
                                  layer.amplitude_um);
    }
    (void)cols;
    (void)rows;
    return 0;
}


}  // namespace

Trajectory bake(const AnimFile &file) {
    Trajectory t;
    t.cols = file.cols;
    t.rows = file.rows;
    t.dt_ms = file.dt_ms;
    t.n = file.n;
    t.duration_ms = file.duration_ms;
    t.tier = file.tier;
    int balls = file.cols * file.rows;
    int frames = file.duration_ms / file.dt_ms + 1;
    t.frame_count = frames;
    t.samples.resize((size_t)frames * balls);

    for (int f = 0; f < frames; ++f) {
        int64_t t_ms = (int64_t)f * file.dt_ms;
        for (int b = 0; b < balls; ++b) {
            int col = b % file.cols;
            int row = b / file.cols;
            int64_t pos = file.base_um;
            for (const auto &layer : file.layers) {
                if (!in_group(layer.group, b, file.cols, file.rows))
                    continue;
                pos += layer_offset(layer, col, row, t_ms, file.cols,
                                    file.rows, b);
            }
            t.samples[(size_t)f * balls + b] = (int32_t)pos;
        }
    }
    return t;
}

int32_t wire_sample_um(int32_t ideal_um) {
    return mc_code_to_um(mc_um_to_code(ideal_um));
}

int32_t step_sample_um(int32_t ideal_um) {
    int32_t steps = mc_um_to_steps(ideal_um);
    return (int32_t)(((int64_t)steps * 1000 * 65536 +
                      MC_STEPS_PER_MM_Q16 / 2) / MC_STEPS_PER_MM_Q16);
}

}  // namespace pc
