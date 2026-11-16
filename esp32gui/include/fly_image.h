#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bench-only embedded Fly-Micro4 app image.  It is generated from
 * SS_FLY_IMAGE at build time and is empty when that variable is unset. */
const uint8_t *ss_fly_image_data(void);
unsigned ss_fly_image_size(void);

#ifdef __cplusplus
}
#endif
