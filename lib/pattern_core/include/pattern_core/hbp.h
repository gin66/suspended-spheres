#ifndef PATTERN_CORE_HBP_H
#define PATTERN_CORE_HBP_H

#include "motion_core.h"
#include "pattern_core/types.h"

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

namespace pc {

/* One self-describing playback file for the installation master.  The body
 * is a sequence of 8-byte cubic wire frames in window-major order:
 *   window * ball_count + ball
 * Each window is n * dt_ms long.  No raw 32-bit trajectory is stored. */
struct Playback {
    AnimFile file;
    int window_ms = 0;
    int window_count = 0;
    std::vector<mc_segment_t> frames;   /* window-major */
    std::string codec = "none";
};

/* Build the wire-frame stream from a baked trajectory.  The first window
 * uses the trajectory position and velocity; later windows start from the
 * previous frame's quantized endpoint so the node's join check is exact. */
bool make_wire_frames(const AnimFile &file, const Trajectory &traj,
                      std::vector<mc_segment_t> *frames, int *window_ms,
                      std::string *error);

/* Write/read one HBP2 file.  write_hbp refuses a take whose report is not
 * clean and leaves an existing file untouched. */
bool write_hbp(const std::string &path, const AnimFile &file,
               const Trajectory &traj, std::string *error);
bool read_hbp(const std::string &path, Playback *playback, std::string *error);

std::string sha256_hex(const uint8_t *data, size_t size);

}  // namespace pc

#endif /* PATTERN_CORE_HBP_H */
