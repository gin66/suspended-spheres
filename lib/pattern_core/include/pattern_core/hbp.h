#ifndef PATTERN_CORE_HBP_H
#define PATTERN_CORE_HBP_H

#include "pattern_core/types.h"

#include <stddef.h>
#include <stdint.h>
#include <string>

namespace pc {

bool write_hbp(const std::string &path, const AnimFile &file,
               const Trajectory &traj, std::string *error);
bool read_hbp(const std::string &path, AnimFile *file, Trajectory *traj,
              std::string *error);

std::string sha256_hex(const uint8_t *data, size_t size);

}  // namespace pc

#endif /* PATTERN_CORE_HBP_H */
