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
#include <math.h>
#include <pico/stdlib.h>

#include "board_pins.h"
#include "can_node.h"
#include "motion_core.h"
#include "status_led.h"
#include "stepper_axes.h"

namespace {

uint8_t s_node_state = MC_STATUS_IDLE;
bool s_boot_done = false;
uint32_t s_boot_start_ms = 0;
uint32_t s_next_status_ms = 0;
int s_status_board_id = -1;
uint32_t s_next_health_ms = 0;
int s_health_board_id = -1;
uint8_t s_health_seq = 0;

void send_status(int board_id) {
    mc_status_t st = {};
    st.state = s_node_state;
    st.fault_flags = 0;
    st.buffer_depth = 0;
    st.last_seq = 0;

    uint8_t payload[8];
    if (mc_pack_status(&st, payload) != 0) {
        return;
    }
    int id = mc_id_make(board_id, can_node::kStatus);
    if (id >= 0) {
        can_node::send((uint16_t)id, payload, 8);
    }
}

void send_health(int board_id) {
    mc_health_t h = {};
    float t = analogReadTemp();
    if (t < -128.0f) t = -128.0f;
    if (t > 127.0f) t = 127.0f;
    h.mcu_temp_c = (int8_t)lroundf(t);
    h.valid_mask = MC_HEALTH_VALID_MCU;

    for (uint8_t i = 0; i < board::kDriveCount; i++) {
        if (digitalRead(board::kDrives[i].diag)) {
            h.diag_mask |= (uint8_t)(1u << i);
        }
    }
    h.seq = s_health_seq++;

    uint8_t payload[8];
    if (mc_pack_health(&h, payload) != 0) {
        return;
    }
    int id = mc_id_make(board_id, MC_TYPE_HEALTH);
    if (id >= 0) {
        can_node::send((uint16_t)id, payload, 8);
    }
}

}  // namespace

void setup() {
    Serial.begin(115200);
    (void)mc_get_ticks_per_s();

    // can_node::begin() claims PIO1. Wait for that claim so the stepper
    // engine can only select PIO0.
    while (!can_node::ready()) {
        delay(1);
    }

    stepper_axes::begin();

    status_led::init();
    status_led::set(status_led::State::Boot);
    s_boot_start_ms = millis();

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
    can_node::tick();

    can_node::Frame frame;
    while (can_node::poll(&frame)) {
    }

    uint32_t now = millis();
    const int board_id = can_node::assigned_board_id();
    if (board_id >= 0) {
        if (board_id != s_status_board_id) {
            s_status_board_id = board_id;
            s_next_status_ms = now;
        }
        if ((int32_t)(now - s_next_status_ms) >= 0) {
            send_status(board_id);
            s_next_status_ms += 100u;
            if ((int32_t)(now - s_next_status_ms) >= 0) {
                s_next_status_ms = now + 100u;
            }
        }

        if (board_id != s_health_board_id) {
            s_health_board_id = board_id;
            s_next_health_ms = now + 1000u + (uint32_t)board_id * 33u;
        }
        if ((int32_t)(now - s_next_health_ms) >= 0) {
            send_health(board_id);
            s_next_health_ms += 1000u;
            if ((int32_t)(now - s_next_health_ms) >= 0) {
                s_next_health_ms = now + 1000u;
            }
        }
    } else {
        s_status_board_id = -1;
        s_health_board_id = -1;
    }

    if (!s_boot_done && (int32_t)(now - s_boot_start_ms) >= 500) {
        s_boot_done = true;
    }
    if (s_boot_done) {
        if (board_id < 0) {
            status_led::set(status_led::State::Unidentified);
        } else {
            status_led::set_mc_state(s_node_state);
        }
    }
    status_led::tick(millis());

    static uint32_t next_report_ms = 0;
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

    delay(1);
}

void loop1() {
    tight_loop_contents();
}
