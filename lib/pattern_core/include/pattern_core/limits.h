#ifndef PATTERN_CORE_LIMITS_H
#define PATTERN_CORE_LIMITS_H

#include "pattern_core/types.h"

namespace pc {

CheckResult check(const AnimFile &file, const Trajectory &traj);
CheckResult bake_and_check(const AnimFile &file,
                           Trajectory *traj_out = nullptr);

bool bus_load_ok(int n, int dt_ms);
int64_t tier_speed_limit_um_s(Tier tier);
int64_t tier_accel_limit_um_s2(Tier tier);

}  // namespace pc

#endif /* PATTERN_CORE_LIMITS_H */
