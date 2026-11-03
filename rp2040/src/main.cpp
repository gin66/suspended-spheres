/* main.cpp — Fly-Micro4 node firmware entry point.
 *
 * Scaffold only: the motion path (§3–§9 of doc/motion_streaming_tdd.md) is
 * not wired to hardware yet. This file exists so the PlatformIO environment
 * builds the shared motion_core library for rpipico.
 */

#include <Arduino.h>

#include "motion_core.h"

void setup() {
    Serial.begin(115200);
}

void loop() {
    /* Touch the core so the linker keeps the library in the firmware. */
    static int32_t ticks_per_s = mc_get_ticks_per_s();
    (void)ticks_per_s;
    delay(1000);
}
