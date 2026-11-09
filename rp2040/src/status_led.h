#pragma once

#include <stdint.h>

namespace status_led {

// Patterns from doc/motion_streaming_whitepaper.md §5.8.1.
enum class State : uint8_t {
    Boot,
    Unidentified,
    Idle,
    Primed,
    Homing,
    Playing,
    Holding,
    Fault,
};

void init();
void set(State state);
void set_mc_state(uint8_t mc_state);
void clear_fault();
void tick(uint32_t now_ms);

}  // namespace status_led
