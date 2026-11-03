/* limits.cpp — tier limits, bus check and continuity checks. */

#include "pattern_core/limits.h"
#include "pattern_core/bake.h"
#include "motion_core.h"

#include <stdint.h>

namespace pc {
namespace {

int64_t abs_i64(int64_t v) { return v < 0 ? -v : v; }

int64_t bus_frames_per_s(int n, int dt_ms) {
    int64_t den = (int64_t)n * dt_ms;
    if (den <= 0)
        return -1;
    return (120000 + den / 2) / den;
}

int bus_load_percent(int n, int dt_ms) {
    int64_t frames = bus_frames_per_s(n, dt_ms);
    if (frames < 0)
        return 1000;
    return mc_bus_load_percent_frames(frames + 350);
}

void add(CheckResult *r, int ball, int64_t t, const char *limit,
         int64_t measured, int64_t allowed) {
    Violation v;
    v.ball = ball;
    v.time_ms = t;
    v.limit = limit;
    v.measured = measured;
    v.allowed = allowed;
    r->violations.push_back(v);
}

}  // namespace

int64_t tier_speed_limit_um_s(Tier tier) {
    switch (tier) {
    case TIER_ORDINARY: return 300000;
    case TIER_NOMINAL: return 590000;
    case TIER_OPTIONAL: return 1300000;
    }
    return 300000;
}

int64_t tier_accel_limit_um_s2(Tier tier) {
    switch (tier) {
    case TIER_ORDINARY: return 0;        /* no extra upper bound */
    case TIER_NOMINAL: return 1500000;
    case TIER_OPTIONAL: return 4000000;
    }
    return 0;
}

bool bus_load_ok(int n, int dt_ms) {
    if (n <= 0 || dt_ms <= 0)
        return false;
    return bus_load_percent(n, dt_ms) < 60;
}

CheckResult check(const AnimFile &file, const Trajectory &traj) {
    CheckResult r;
    int balls = file.cols * file.rows;
    if (balls <= 0 || traj.frame_count <= 0 ||
        (int)traj.samples.size() < balls * traj.frame_count) {
        r.parse_error = "trajectory does not match the file";
        return r;
    }

    const int64_t speed_limit = tier_speed_limit_um_s(file.tier);
    const int64_t accel_limit = tier_accel_limit_um_s2(file.tier);
    const int dt = file.dt_ms;
    const int frames = traj.frame_count;

    for (int b = 0; b < balls; ++b) {
        for (int f = 0; f < frames; ++f) {
            const int32_t p = traj.at(f, b);
            const int64_t t = traj.time_ms(f);

            if (p < 0 || p > 2000000)
                add(&r, b, t, "position", p, p < 0 ? 0 : 2000000);

            /* FR-PAT-008: one ball cannot have two positions at one time. */
            if (f > 0 && traj.time_ms(f - 1) == t &&
                traj.at(f - 1, b) != p)
                add(&r, b, t, "duplicate_time", p, traj.at(f - 1, b));
        }

        int64_t prev_speed = 0;
        int have_prev_speed = 0;
        for (int f = 0; f < frames; ++f) {
            const int32_t p = traj.at(f, b);
            const int64_t t = traj.time_ms(f);

            /* Central difference where both neighbours exist; one-sided
             * at the ends.  Time spans use the explicit times when set. */
            int64_t speed = 0;
            if (frames == 1) {
                speed = 0;
            } else if (f == 0) {
                int64_t span = traj.time_ms(1) - t;
                speed = span > 0
                    ? (int64_t)(traj.at(1, b) - p) * 1000 / span : 0;
            } else if (f == frames - 1) {
                int64_t span = t - traj.time_ms(f - 1);
                speed = span > 0
                    ? (int64_t)(p - traj.at(f - 1, b)) * 1000 / span : 0;
            } else {
                int64_t span = traj.time_ms(f + 1) - traj.time_ms(f - 1);
                speed = span > 0
                    ? (int64_t)(traj.at(f + 1, b) -
                                traj.at(f - 1, b)) * 1000 / span : 0;
            }

            const int64_t av = abs_i64(speed);
            if (av > speed_limit)
                add(&r, b, t, "peak_speed", av, speed_limit);

            /* A 2 ms chunk at this speed must fit in 128 microsteps. */
            const int32_t displacement_2ms = (int32_t)(av * 2 / 1000);
            const int32_t steps = mc_um_to_steps(displacement_2ms);
            if (steps > MC_MAX_STEPS_PER_CHUNK)
                add(&r, b, t, "chunk_steps", steps, MC_MAX_STEPS_PER_CHUNK);

            if (have_prev_speed) {
                int64_t span = t - traj.time_ms(f - 1);
                if (span > 0) {
                    int64_t a = (speed - prev_speed) * 1000 / span;
                    if (a <= -9810000)
                        add(&r, b, t, "downward_accel", a, -9810000);
                    if (accel_limit > 0 && abs_i64(a) > accel_limit)
                        add(&r, b, t, "acceleration", abs_i64(a),
                            accel_limit);
                }
            }
            prev_speed = speed;
            have_prev_speed = 1;
        }
    }

    if (!bus_load_ok(file.n, file.dt_ms)) {
        int load = bus_load_percent(file.n, file.dt_ms);
        add(&r, -1, 0, "bus_load", load, 60);
    }
    return r;
}

CheckResult bake_and_check(const AnimFile &file, Trajectory *traj_out) {
    Trajectory t = bake(file);
    if (traj_out)
        *traj_out = t;
    return check(file, t);
}

}  // namespace pc
