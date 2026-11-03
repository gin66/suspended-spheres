#ifndef PATTERN_CORE_JSON_H
#define PATTERN_CORE_JSON_H

#include "pattern_core/types.h"

#include <string>

namespace pc {

bool parse_anim_json(const std::string &text, AnimFile *out,
                     std::string *error);
bool parse_anim_file(const std::string &path, AnimFile *out,
                     std::string *error);

std::string report_json(const CheckResult &result);
bool write_report_file(const std::string &path, const CheckResult &result);

}  // namespace pc

#endif /* PATTERN_CORE_JSON_H */
