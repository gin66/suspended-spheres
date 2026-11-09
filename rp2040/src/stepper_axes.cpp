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

}  // namespace

void begin() {
    // FastAccelStepper claims the first PIO block with free state machines.
    // PIO1 is already claimed by can2040, so the engine loads its program on
    // PIO0 and takes all four state machines there.
    engine.init();

    for (uint8_t i = 0; i < board::kDriveCount; i++) {
        const board::DrivePins& pins = board::kDrives[i];
        pinMode(pins.diag, INPUT);

        axes[i] = engine.stepperConnectToPin(pins.step);
        if (axes[i] == nullptr) {
            continue;
        }
        axes[i]->setDirectionPin(pins.dir);
        axes[i]->setEnablePin(pins.en, board::kEnableActiveLow);
        axes[i]->setAutoEnable(true);
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

void setEnabled(bool enabled) {
    for (uint8_t i = 0; i < board::kDriveCount; i++) {
        if (axes[i] == nullptr) {
            continue;
        }
        if (enabled) {
            axes[i]->enableOutputs();
        } else {
            axes[i]->disableOutputs();
        }
    }
}

bool moveTo(uint8_t index, int32_t position) {
    FastAccelStepper* stepper = axis(index);
    if (stepper == nullptr) {
        return false;
    }
    return stepper->moveTo(position) == MOVE_OK;
}

}  // namespace stepper_axes
