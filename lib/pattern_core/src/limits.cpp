/* limits.cpp — tier limits, bus check and continuity checks. */

#include "pattern_core/limits.h"
#include "pattern_core/bake.h"
#include "motion_core.h"

namespace pc {

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
    int64_t per_s = (120000 + (n * dt_ms) / 2) / (n * dt_ms);
    int64_t load = (per_s + 350) * 130 / 10000;
    return load < 60;
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

    for (int b = 0; b < balls; ++b) {
        int64_t prev_v = 0;
        for (int f = 0; f < traj.frame_count; ++f) {
            int32_t p = traj.at(f, b);
            int64_t t = (int64_t)f * dt;

            if (p < 0 || p > 2000000)
                r.violations.push_back(
                    {b, t, "position", p, p < 0 ? 0 : 2000000});

            if (f > 0) {
                int32_t prev = traj.at(f - 1, b);
                int64_t delta = (int64_t)p - prev;
                int64_t v = delta * 1000 / dt;
                int64_t av = v < 0 ? -v : v;
                if (av > speed_limit)
                    r.violations.push_back(
                        {b, t, "peak_speed", av, speed_limit});
                if (delta < -100000 || delta > 100000)
                    r.violations.push_back(
                        {b, t, "continuity", delta, 100000});

                int64_t a = (v - prev_v) * 1000 / dt;
                if (a < -9810000)
                    r.violations.push_back(
                        {b, t, "downward_accel", a, -9810000});
                if (accel_limit > 0 && (a < 0 ? -a : a) > accel_limit)
                    r.violations.push_back(
                        {b, t, "acceleration", a < 0 ? -a : a, accel_limit});
                prev_v = v;

                int32_t disp = (int32_t)(av * 2000 / 1000000);
                int32_t steps = mc_um_to_steps(disp);
                if (steps > 128)
                    r.violations.push_back({b, t, "chunk_steps", steps, 128});
            }
        }
    }

    if (!bus_load_ok(file.n, file.dt_ms))
        r.violations.push_back({-1, 0, "bus_load", 0, 60});
    return r;
}

CheckResult bake_and_check(const AnimFile &file, Trajectory *traj_out) {
    Trajectory t = bake(file);
    if (traj_out)
        *traj_out = t;
    return check(file, t);
}

}  // namespace pc
