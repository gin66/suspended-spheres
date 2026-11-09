/* main.cpp - Fly-Micro4 node firmware entry point.
 *
 * Core split (RP2040):
 *   core 1  vanilla can2040 on PIO1: init, IRQ handler and all receive work
 *   core 0  FastAccelStepper on PIO0: one state machine per drive
 *
 * PIO1 and its interrupt stay on core 1 so the CAN parser is not delayed by
 * the stepper task, the USB stack or the status printing on core 0.
 *
 * Motion payloads are not decoded yet. The CAN callback only counts frames
 * and queues them so the transport can be validated on the bench before the
 * protocol layer from doc/Fly_Micro4_addressing.md is wired in.
 */

#include <Arduino.h>
#include <pico/stdlib.h>

#include "board_pins.h"
#include "can_node.h"
#include "motion_core.h"
#include "stepper_axes.h"

void setup() {
    Serial.begin(115200);
    (void)mc_get_ticks_per_s();

    // can_node::begin() claims PIO1. Wait for that claim so the stepper
    // engine can only select PIO0.
    while (!can_node::ready()) {
        delay(1);
    }

    stepper_axes::begin();
    pinMode(board::kStatusLedPin, OUTPUT);

    uint8_t axes_ready = 0;
    for (uint8_t i = 0; i < board::kDriveCount; i++) {
        if (stepper_axes::axis(i) != nullptr) {
            axes_ready++;
        }
    }
    Serial.print("Fly-Micro4: CAN core=1, stepper axes on PIO0=");
    Serial.println(axes_ready);
}

void setup1() {
    can_node::begin();
}

void loop() {
    can_node::Frame frame;
    while (can_node::poll(&frame)) {
        digitalWrite(board::kStatusLedPin, HIGH);
    }

    static uint32_t next_report_ms = 0;
    uint32_t now = millis();
    if ((int32_t)(now - next_report_ms) >= 0) {
        next_report_ms = now + 1000;
        can_node::Stats s = can_node::stats();
        Serial.print("can rx=");
        Serial.print(s.rx);
        Serial.print(" tx=");
        Serial.print(s.tx);
        Serial.print(" err=");
        Serial.print(s.error);
        Serial.print(" drop=");
        Serial.println(s.dropped);
    }

    digitalWrite(board::kStatusLedPin, LOW);
    delay(10);
}

void loop1() {
    tight_loop_contents();
}
