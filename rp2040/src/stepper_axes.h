#pragma once

#include <stdint.h>

class FastAccelStepper;

namespace stepper_axes {

void begin();
FastAccelStepper* axis(uint8_t index);
void setEnabled(bool enabled);
bool moveTo(uint8_t index, int32_t position);

}  // namespace stepper_axes
