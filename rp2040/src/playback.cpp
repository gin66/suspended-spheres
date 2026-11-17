#include "playback.h"

#include <Arduino.h>
#include <pico/stdlib.h>

#include "can_node.h"
#include "show_core/show_core.h"
#include "stepper_axes.h"

namespace playback {
namespace {

show_node_t s_node;
bool s_active = false;
bool s_faulted = false;
uint8_t s_fault = 0;
uint8_t s_depth = 0;
uint8_t s_seq = 0;

void fail(void) {
    stepper_axes::abortAll();
    s_fault = s_node.fault != 0 ? s_node.fault : MC_STATUS_FAULT_UNDERRUN;
    s_faulted = true;
    s_active = false;
}

void refresh_depth(void) {
    int n = 0;
    for (int i = 0; i < MC_AXES_PER_NODE; ++i) n += s_node.pending_count[i];
    s_depth = (uint8_t)(n > 255 ? 255 : n);
    s_seq = s_node.sync_seq;
}

}  // namespace

void begin() {
    show_node_init(&s_node);
    s_active = false;
    s_faulted = false;
    s_fault = 0;
    s_depth = 0;
    s_seq = 0;
}

void stop() {
    stepper_axes::abortAll();
    show_node_reset(&s_node);
    s_active = false;
    s_faulted = false;
    s_fault = 0;
    s_depth = 0;
}

void on_sync(const mc_sync_t& sync) {
    if (s_faulted) return;
    if (show_node_sync(&s_node, &sync, time_us_64()) != 0) fail();
}

void on_motion(int field, const mc_segment_t& seg) {
    if (s_faulted) return;
    /* The IRQ filter already resolved the position field to a local motor
     * with its POSMAP copy; use that resolution here too. */
    const int motor = can_node::motor_for_field(field);
    if (motor < 0) return;
    if (!stepper_axes::isAxisEnabled((uint8_t)motor)) {
        stepper_axes::setAxisEnabled((uint8_t)motor, true);
    }
    if (show_node_motion_motor(&s_node, motor, &seg, time_us_64()) != 0) fail();
}

void poll() {
    if (s_faulted) return;
    /* The queue is filled to SHOW_LOOKAHEAD_US; each call emits one chunk,
     * so a late loop iteration catches up without a gap. */
    for (;;) {
        show_chunk_t chunk;
        const int rc = show_node_tick(&s_node, time_us_64(), &chunk);
        if (rc < 0) {
            fail();
            return;
        }
        if (rc == 0) break;
        for (int i = 0; i < MC_AXES_PER_NODE; ++i) {
            if ((chunk.mask & (uint8_t)(1u << i)) == 0) continue;
            if (!stepper_axes::isAxisEnabled((uint8_t)i)) {
                stepper_axes::setAxisEnabled((uint8_t)i, true);
            }
            if (stepper_axes::timedChunk((uint8_t)i, chunk.steps[i],
                                         chunk.ticks) != 0) {
                fail();
                return;
            }
        }
        s_active = true;
        refresh_depth();
    }
    refresh_depth();
}

bool active() { return s_active && !s_faulted; }
uint8_t fault_flags() { return s_faulted ? s_fault : 0; }
uint8_t buffer_depth() { return s_depth; }
uint8_t last_seq() { return s_seq; }

}  // namespace playback
