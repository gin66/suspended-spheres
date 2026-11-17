#pragma once

#include <stdint.h>

class FastAccelStepper;

namespace stepper_axes {

void begin();
FastAccelStepper* axis(uint8_t index);
void setEnabled(bool enabled);
void setAxisEnabled(uint8_t index, bool enabled);
bool isAxisEnabled(uint8_t index);
bool isRunning(uint8_t index);
void setSpeed(uint8_t index, uint32_t speed_hz);
void setAcceleration(uint8_t index, uint32_t accel_steps_s2);
bool move(uint8_t index, int32_t steps);
bool run(uint8_t index, bool forward);
void stop(uint8_t index);
void stopAll();
// Abrupt queue abort for the show path: no further timed command is added and
// the admission latch is rearmed so the next show can queue again.
void abortAll();
bool moveTo(uint8_t index, int32_t position);

// One fixed-duration chunk for the HBP2 playback path.  steps is signed and
// ticks is the chunk length in 16 MHz units.  Returns 0 when queued, a
// positive MoveTimedResultCode when the queue refused it, -1 when the axis is
// missing.  Direction-change pauses are retried here.
int timedChunk(uint8_t index, int16_t steps, uint16_t ticks);
// Abrupt stop with no deceleration; the caller owns the position afterwards.
bool forceStopAndSetPosition(uint8_t index, int32_t position);

}  // namespace stepper_axes
