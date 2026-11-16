#include "fly_image.h"
#include "fly_image_data.h"

extern "C" const uint8_t *ss_fly_image_data(void) { return kFlyImage; }
extern "C" unsigned ss_fly_image_size(void) { return kFlyImageLen; }
