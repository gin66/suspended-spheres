#pragma once

#include <stdint.h>

class FastAccelStepper;

namespace stepper_axes {

void begin();
FastAccelStepper* axis(uint8_t index);
void setEnabled(bool enabled);
void setAxisEnabled(uint8_t index, bool enabled);
void setSpeed(uint8_t index, uint32_t speed_hz);
void setAcceleration(uint8_t index, uint32_t accel_steps_s2);
bool move(uint8_t index, int32_t steps);
bool run(uint8_t index, bool forward);
void stop(uint8_t index);
void stopAll();
bool moveTo(uint8_t index, int32_t position);

}  // namespace stepper_axes
