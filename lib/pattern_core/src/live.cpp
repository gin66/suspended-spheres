/* live.cpp — debounced .anim.json reload with state carried across saves. */

#include "pattern_core/live.h"
#include "pattern_core/bake.h"
#include "pattern_core/json.h"
#include "pattern_core/limits.h"

namespace pc {
namespace {

uint32_t content_hash(const std::string &s) {
    uint32_t h = 2166136261u;
    for (unsigned char c : s) {
        h ^= c;
        h *= 16777619u;
    }
    return h;
}

bool is_anim_path(const std::string &path) {
    static const char suffix[] = ".anim.json";
    const size_t n = sizeof(suffix) - 1;
    return path.size() >= n &&
           path.compare(path.size() - n, n, suffix) == 0;
}

}  // namespace

void LiveReload::on_change(int64_t now_us, const std::string &path,
                           const std::string &bytes) {
    if (!is_anim_path(path))
        return;

    uint32_t h = content_hash(bytes);
    if (!pending_ && has_trajectory_ && h == loaded_hash_)
        return;
    if (!pending_)
        pending_since_us_ = now_us;

    pending_ = true;
    pending_path_ = path;
    pending_bytes_ = bytes;
    pending_hash_ = h;
}

bool LiveReload::poll(int64_t now_us) {
    if (!pending_)
        return false;
    if (now_us - pending_since_us_ < kDebounceUs)
        return false;

    pending_ = false;
    uint32_t h = pending_hash_;
    if (has_trajectory_ && h == loaded_hash_)
        return false;

    loaded_hash_ = h;
    AnimFile parsed;
    std::string error;
    if (!parse_anim_json(pending_bytes_, &parsed, &error)) {
        /* Keep the last good take and its violations; append the parse
         * error so the report can show that the reload failed. */
        report_.parse_error = error;
        return false;
    }

    Trajectory baked = bake(parsed);
    CheckResult checked = check(parsed, baked);
    file_ = std::move(parsed);
    trajectory_ = std::move(baked);
    report_ = std::move(checked);
    has_trajectory_ = true;
    return true;
}

int32_t LiveReload::position_at(int ball, int64_t time_ms) const {
    if (!has_trajectory_ || ball < 0 || ball >= trajectory_.ball_count() ||
        trajectory_.frame_count <= 0 || trajectory_.dt_ms <= 0)
        return -1;
    int64_t frame = time_ms / trajectory_.dt_ms;
    if (frame < 0)
        frame = 0;
    if (frame >= trajectory_.frame_count)
        frame = trajectory_.frame_count - 1;
    return trajectory_.at((int)frame, ball);
}

}  // namespace pc
