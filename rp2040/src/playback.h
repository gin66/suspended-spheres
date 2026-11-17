#pragma once

#include <stdint.h>

#include "motion_core.h"

namespace playback {

void begin();

// Type-2 sync and type-0 motion from the master.  on_motion ignores frames
// for positions this board does not drive (the IRQ filter already drops them).
void on_sync(const mc_sync_t& sync);
void on_motion(int field, const mc_segment_t& seg);

// Called from the main loop: emits at most a few 2 ms chunks when due.
void poll();

// Operational stop: cancel the timeline and stop the four axes.  Called for
// COMMAND STOP, home, a jog and re-identify.
void stop();

bool active();
uint8_t fault_flags();
uint8_t buffer_depth();
uint8_t last_seq();

}  // namespace playback
