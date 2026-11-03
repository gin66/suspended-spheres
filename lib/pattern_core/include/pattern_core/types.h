/* types.h — pattern model shared by the bake, limits and export modules. */

#ifndef PATTERN_CORE_TYPES_H
#define PATTERN_CORE_TYPES_H

#include <stdint.h>

#include <string>
#include <vector>

namespace pc {

enum Tier {
    TIER_ORDINARY = 0,
    TIER_NOMINAL = 1,
    TIER_OPTIONAL = 2
};

enum Op {
    OP_KEYFRAMES = 0,
    OP_SINE,
    OP_TRAVELLING_WAVE,
    OP_INTERFERENCE,
    OP_SPIRAL,
    OP_RIPPLE,
    OP_CASCADE,
    OP_RANDOM_WALK
};

enum GroupKind {
    GROUP_ALL = 0,
    GROUP_ROW,
    GROUP_COLUMN,
    GROUP_RING,
    GROUP_CHECKERBOARD,
    GROUP_MASK
};

struct Keyframe {
    int64_t t_ms;
    int32_t pos_um;      /* offset from the base position */
};

struct Group {
    GroupKind kind = GROUP_ALL;
    int index = 0;
    int center_col = 0;
    int center_row = 0;
    int radius = 0;
    int parity = 0;
    std::vector<int> balls;
};

struct Layer {
    Op op = OP_SINE;

    int32_t amplitude_um = 0;
    int32_t period_ms = 0;
    int32_t wavelength_cols = 0;
    int32_t phase_deg = 0;
    int axis = 0;            /* 0 = col, 1 = row */
    int direction = 1;
    uint32_t seed = 0;

    /* interference: second wave */
    int32_t amplitude2_um = 0;
    int32_t period2_ms = 0;
    int32_t wavelength2_cols = 0;
    int32_t phase2_deg = 0;
    int axis2 = 0;

    std::vector<Keyframe> keys;
    Group group;
};

struct AnimFile {
    int version = 0;
    int cols = 0;
    int rows = 0;
    int dt_ms = 0;
    int n = 0;
    int duration_ms = 0;
    Tier tier = TIER_ORDINARY;
    int32_t base_um = 1000000;
    std::vector<Layer> layers;
};

struct Trajectory {
    int cols = 0;
    int rows = 0;
    int dt_ms = 0;
    int n = 0;
    int duration_ms = 0;
    Tier tier = TIER_ORDINARY;
    int frame_count = 0;
    /* frame-major: frame * (cols*rows) + ball */
    std::vector<int32_t> samples;

    int ball_count() const { return cols * rows; }
    int32_t at(int frame, int ball) const {
        return samples[(size_t)frame * ball_count() + ball];
    }
};

struct Violation {
    int ball = 0;
    int64_t time_ms = 0;
    std::string limit;
    int64_t measured = 0;
    int64_t allowed = 0;
};

struct CheckResult {
    std::vector<Violation> violations;
    std::string parse_error;
    bool ok() const { return violations.empty() && parse_error.empty(); }
};

}  // namespace pc

#endif /* PATTERN_CORE_TYPES_H */
