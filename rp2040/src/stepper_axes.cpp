#include "stepper_axes.h"

#include <Arduino.h>
#include <FastAccelStepper.h>
#include <hardware/pio.h>

#include "board_pins.h"

static_assert(board::kDriveCount == NUM_PIO_STATE_MACHINES,
              "FastAccelStepper needs one PIO state machine per Fly-Micro4 drive");

namespace stepper_axes {
namespace {

FastAccelStepperEngine engine;
FastAccelStepper* axes[board::kDriveCount] = {};
bool s_enabled[board::kDriveCount] = {};

}  // namespace

void begin() {
    // FastAccelStepper claims the first PIO block with free state machines.
    // PIO1 is already claimed by can2040, so the engine loads its program on
    // PIO0 and takes all four state machines there.
    engine.init();

    for (uint8_t i = 0; i < board::kDriveCount; i++) {
        const board::DrivePins& pins = board::kDrives[i];
        // The official Klipper config uses ^gpio13..15/12, i.e. a pull-up.
        pinMode(pins.diag, INPUT_PULLUP);

        axes[i] = engine.stepperConnectToPin(pins.step);
        if (axes[i] == nullptr) {
            continue;
        }
        axes[i]->setDirectionPin(pins.dir);
        axes[i]->setEnablePin(pins.en, board::kEnableActiveLow);
        // A suspended ball is held by current, not by a brake. Auto-enable
        // would drop the driver after the auto-disable delay, so outputs stay
        // enabled explicitly until a caller disables them.
        axes[i]->setAutoEnable(false);
        axes[i]->setSpeedInHz(1000);
        axes[i]->setAcceleration(500);
        axes[i]->disableOutputs();
    }
}

FastAccelStepper* axis(uint8_t index) {
    if (index >= board::kDriveCount) {
        return nullptr;
    }
    return axes[index];
}

void setAxisEnabled(uint8_t index, bool enabled) {
    FastAccelStepper* stepper = axis(index);
    if (stepper == nullptr) {
        return;
    }
    if (enabled) {
        stepper->enableOutputs();
    } else {
        stepper->disableOutputs();
    }
    s_enabled[index] = enabled;
}

bool isAxisEnabled(uint8_t index) {
    return index < board::kDriveCount && s_enabled[index];
}

bool isRunning(uint8_t index) {
    FastAccelStepper* stepper = axis(index);
    return stepper != nullptr && stepper->isRunning();
}

void setEnabled(bool enabled) {
    for (uint8_t i = 0; i < board::kDriveCount; i++) {
        setAxisEnabled(i, enabled);
    }
}

void setSpeed(uint8_t index, uint32_t speed_hz) {
    FastAccelStepper* stepper = axis(index);
    if (stepper != nullptr) {
        stepper->setSpeedInHz(speed_hz);
    }
}

void setAcceleration(uint8_t index, uint32_t accel_steps_s2) {
    FastAccelStepper* stepper = axis(index);
    if (stepper != nullptr) {
        stepper->setAcceleration((int32_t)accel_steps_s2);
    }
}

bool forceStopAndSetPosition(uint8_t index, int32_t position) {
    FastAccelStepper* stepper = axis(index);
    if (stepper == nullptr) {
        return false;
    }
    stepper->forceStopAndNewPosition(position);
    return true;
}

bool move(uint8_t index, int32_t steps) {
    FastAccelStepper* stepper = axis(index);
    if (stepper == nullptr) {
        return false;
    }
    return stepper->move(steps) == MOVE_OK;
}

bool run(uint8_t index, bool forward) {
    FastAccelStepper* stepper = axis(index);
    if (stepper == nullptr) {
        return false;
    }
    MoveResultCode rc = forward ? stepper->runForward() : stepper->runBackward();
    return rc == MOVE_OK;
}

void stop(uint8_t index) {
    FastAccelStepper* stepper = axis(index);
    if (stepper != nullptr) {
        stepper->stopMove();
    }
}

void stopAll() {
    for (uint8_t i = 0; i < board::kDriveCount; i++) {
        stop(i);
    }
}

void abortAll() {
    for (uint8_t i = 0; i < board::kDriveCount; i++) {
        FastAccelStepper* stepper = axis(i);
        if (stepper == nullptr) {
            continue;
        }
        stepper->forceStop();
        stepper->resumeCommands();
    }
}

int timedChunk(uint8_t index, int16_t steps, uint16_t ticks) {
    FastAccelStepper* stepper = axis(index);
    if (stepper == nullptr || ticks == 0) {
        return -1;
    }
    uint32_t actual = 0;
    for (int attempt = 0; attempt < 4; ++attempt) {
        const MoveTimedResultCode rc =
            stepper->moveTimed(steps, (uint32_t)ticks, &actual, true);
        if (rc == MoveTimedResultCode::OK ||
            rc == MoveTimedResultCode::MoveEmpty) {
            return 0;
        }
        if (rc == MoveTimedResultCode::DirChangePauseInjected ||
            rc == MoveTimedResultCode::DirPin2msPauseAdded) {
            continue;
        }
        return (int)rc;
    }
    return -100;
}

bool moveTo(uint8_t index, int32_t position) {
    FastAccelStepper* stepper = axis(index);
    if (stepper == nullptr) {
        return false;
    }
    return stepper->moveTo(position) == MOVE_OK;
}

}  // namespace stepper_axes
