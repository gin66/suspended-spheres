#ifndef PATTERN_CORE_LIVE_H
#define PATTERN_CORE_LIVE_H

#include "pattern_core/types.h"

#include <stdint.h>
#include <string>

namespace pc {

struct CameraPose {
    int32_t x_mm = 0;
    int32_t y_mm = 0;
    int32_t z_mm = 0;
    int32_t yaw_mdeg = 0;
    int32_t pitch_mdeg = 0;
};

/* Live .anim.json state.  The caller owns the clock: on_change() and
 * poll() take microseconds, and the debounce interval is 50 ms.  Only
 * *.anim.json is an authoring source; a .hbp write is ignored. */
class LiveReload {
public:
    static const int64_t kDebounceUs = 50000;

    void on_change(int64_t now_us, const std::string &path,
                   const std::string &bytes);
    bool poll(int64_t now_us);

    bool pending() const { return pending_; }
    bool has_trajectory() const { return has_trajectory_; }
    const AnimFile &file() const { return file_; }
    const Trajectory &trajectory() const { return trajectory_; }
    const CheckResult &report() const { return report_; }
    bool export_enabled() const { return has_trajectory_ && report_.ok(); }

    /* New samples apply from the current playhead forward.  These are
     * deliberately not touched by a reload. */
    int64_t playhead_ms = 0;
    int selected_ball = 0;
    CameraPose camera;

    int32_t position_at(int ball, int64_t time_ms) const;

private:
    bool pending_ = false;
    int64_t pending_since_us_ = 0;
    std::string pending_path_;
    std::string pending_bytes_;
    uint32_t pending_hash_ = 0;
    uint32_t loaded_hash_ = 0;

    bool has_trajectory_ = false;
    AnimFile file_;
    Trajectory trajectory_;
    CheckResult report_;
};

}  // namespace pc

#endif /* PATTERN_CORE_LIVE_H */
