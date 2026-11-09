#include "status_led.h"

#include <Arduino.h>

#include "board_pins.h"
#include "motion_core.h"

namespace status_led {
namespace {

struct Segment {
    uint16_t ms;
    bool on;
};

struct Pattern {
    const Segment* segments;
    uint8_t count;
    bool repeat;
};

const Segment kBoot[] = {
    {500, true},
    {1, false},
};

const Segment kUnidentified[] = {
    {100, true}, {200, false},
    {100, true}, {200, false},
    {1400, false},
};

const Segment kIdle[] = {
    {50, true},
    {1950, false},
};

const Segment kHoming[] = {
    {250, true},
    {250, false},
};

const Segment kPlaying[] = {
    {50, true},
    {9950, false},
};

const Segment kHolding[] = {
    {100, true}, {200, false},
    {100, true}, {200, false},
    {100, true}, {200, false},
    {1100, false},
};

const Segment kFault[] = {
    {100, true},
    {100, false},
};

const Pattern kPatterns[] = {
    {kBoot, 2, false},
    {kUnidentified, 5, true},
    {kIdle, 2, true},
    {kIdle, 2, true},
    {kHoming, 2, true},
    {kPlaying, 2, true},
    {kHolding, 7, true},
    {kFault, 2, true},
};

State s_state = State::Boot;
bool s_fault_latched = false;
bool s_started = false;
uint8_t s_index = 0;
uint32_t s_segment_start_ms = 0;

const Pattern& pattern_for(State state) {
    return kPatterns[static_cast<uint8_t>(state)];
}

void apply(bool on) {
    digitalWrite(board::kStatusLedPin, on ? HIGH : LOW);
}

void start(State state, uint32_t now_ms) {
    s_state = state;
    s_index = 0;
    s_segment_start_ms = now_ms;
    s_started = true;
    apply(pattern_for(state).segments[0].on);
}

}  // namespace

void init() {
    pinMode(board::kStatusLedPin, OUTPUT);
    digitalWrite(board::kStatusLedPin, LOW);
    s_fault_latched = false;
    s_started = false;
}

void set(State state) {
    if (s_fault_latched && state != State::Fault) {
        return;
    }
    if (state == State::Fault) {
        s_fault_latched = true;
    }
    if (s_started && state == s_state) {
        return;
    }
    start(state, millis());
}

void clear_fault() {
    s_fault_latched = false;
}

void set_mc_state(uint8_t mc_state) {
    switch (mc_state) {
        case MC_STATUS_HOMING:
            set(State::Homing);
            break;
        case MC_STATUS_PRIMED:
            set(State::Primed);
            break;
        case MC_STATUS_PLAYING:
            set(State::Playing);
            break;
        case MC_STATUS_HOLDING:
            set(State::Holding);
            break;
        case MC_STATUS_FAULT:
            set(State::Fault);
            break;
        case MC_STATUS_IDLE:
        default:
            set(State::Idle);
            break;
    }
}

void tick(uint32_t now_ms) {
    if (!s_started) {
        return;
    }

    const Pattern& pattern = pattern_for(s_state);
    for (;;) {
        const Segment& segment = pattern.segments[s_index];
        const uint32_t elapsed = now_ms - s_segment_start_ms;
        if ((int32_t)elapsed < 0 || elapsed < segment.ms) {
            return;
        }

        if (!pattern.repeat && s_index + 1 >= pattern.count) {
            apply(segment.on);
            s_started = false;
            return;
        }

        s_segment_start_ms += segment.ms;
        s_index++;
        if (s_index >= pattern.count) {
            s_index = 0;
        }
        apply(pattern.segments[s_index].on);
    }
}

}  // namespace status_led
