#ifndef PATTERN_CORE_BAKE_H
#define PATTERN_CORE_BAKE_H

#include "pattern_core/types.h"

namespace pc {

Trajectory bake(const AnimFile &file);

/* Wire fidelity: full-range 16-bit code decoded back to micrometres.
 * Step fidelity: the same position on the microstep grid. */
int32_t wire_sample_um(int32_t ideal_um);
int32_t step_sample_um(int32_t ideal_um);

}  // namespace pc

#endif /* PATTERN_CORE_BAKE_H */
