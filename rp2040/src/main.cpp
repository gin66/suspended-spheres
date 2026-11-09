/* main.cpp - Fly-Micro4 node firmware entry point.
 *
 * Core split (RP2040):
 *   core 1  vanilla can2040 on PIO1: init, IRQ handler and all receive work
 *   core 0  FastAccelStepper on PIO0: one state machine per drive
 *
 * PIO1 and its interrupt stay on core 1 so the CAN parser is not delayed by
 * the stepper task, the USB stack or the status printing on core 0.
 *
 * Core 0 decodes COMMAND (type 4) and HOME (type 5) frames. Commands drive
 * the four axes through stepper_axes; a broadcast REIDENTIFY drops the RAM
 * board ID so a rebooted master can find the board again. StallGuard homing
 * is still a stub because it needs the TMC2209 UART configuration.
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

constexpr int32_t kJogAccelStepsS2 = 2000;
constexpr uint16_t kDefaultJogMm = 10;
constexpr uint16_t kDefaultSpeedMmS = 20;

uint32_t mm_s_to_steps_s(uint16_t mm_s) {
    const uint64_t q16 =
        (uint64_t)mm_s * 1000u * (uint32_t)MC_STEPS_PER_MM_Q16;
    const uint32_t hz = (uint32_t)(q16 >> 16);
    return hz < 32u ? 32u : hz;
}

void stop_axes(uint8_t mask) {
    if (mask == 0) {
        mask = MC_AXIS_MASK_ALL;
    }
    for (uint8_t i = 0; i < board::kDriveCount; i++) {
        if (mask & (uint8_t)(1u << i)) {
            stepper_axes::stop(i);
        }
    }
}

void jog(bool up, uint8_t mask, uint16_t distance_mm, uint16_t speed_mm_s,
         uint8_t seq) {
    if (mask == 0) {
        mask = MC_AXIS_MASK_ALL;
    }
    if (distance_mm == 0) {
        distance_mm = kDefaultJogMm;
    }
    if (speed_mm_s == 0) {
        speed_mm_s = kDefaultSpeedMmS;
    }
    /* mc_um_to_steps uses the shared provisional 40 mm drum / 1/16 value
     * until the mechanics fix the real steps/mm. */
    int32_t steps = mc_um_to_steps((int32_t)distance_mm * 1000);
    if (steps <= 0) {
        steps = 1;
    }
    const uint32_t hz = mm_s_to_steps_s(speed_mm_s);
    for (uint8_t i = 0; i < board::kDriveCount; i++) {
        if ((mask & (uint8_t)(1u << i)) == 0) {
            continue;
        }
        stepper_axes::setSpeed(i, hz);
        stepper_axes::setAcceleration(i, kJogAccelStepsS2);
        stepper_axes::move(i, up ? steps : -steps);
    }

    Serial.print("command: ");
    Serial.print(up ? "UP" : "DOWN");
    Serial.print(" mask=0x");
    Serial.print((unsigned)mask, HEX);
    Serial.print(" steps=");
    Serial.print(steps);
    Serial.print(" hz=");
    Serial.print(hz);
    Serial.print(" seq=");
    Serial.println((unsigned)seq);
}

void handle_frame(const can_node::Frame& frame) {
    const int id = frame.id;
    if (mc_id_valid(id) <= 0) {
        return;
    }
    const int type = mc_id_type(id);
    const int board = can_node::assigned_board_id();

    if (type == can_node::kCommand) {
        mc_command_t cmd;
        if (mc_unpack_command(frame.data, frame.dlc, &cmd) != 0) {
            return;
        }
        if (board < 0 || mc_board_accepts_id(board, id) == 0) {
            return;
        }
        if (cmd.action == MC_CMD_STOP) {
            stop_axes(cmd.axis_mask);
            s_node_state = MC_STATUS_IDLE;
            Serial.println("command: STOP");
        } else if (cmd.action == MC_CMD_UP) {
            jog(true, cmd.axis_mask, cmd.distance_mm, cmd.speed_mm_s, cmd.seq);
        } else if (cmd.action == MC_CMD_DOWN) {
            jog(false, cmd.axis_mask, cmd.distance_mm, cmd.speed_mm_s, cmd.seq);
        }
    } else if (type == can_node::kHome) {
        uint8_t mask = 0;
        if (mc_unpack_home(frame.data, frame.dlc, &mask) != 0) {
            return;
        }
        if (board < 0 || mc_board_accepts_id(board, id) == 0) {
            return;
        }
        Serial.print("home: ignored, StallGuard homing is not implemented (mask=0x");
        Serial.print((unsigned)mask, HEX);
        Serial.println(")");
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
        handle_frame(frame);
    }

    if (can_node::take_reidentify_request()) {
        /* can_node dropped the RAM ID in the receive path. Leave the
         * outputs enabled so a partly wound ball is held, not dropped. */
        stepper_axes::stopAll();
        s_node_state = MC_STATUS_IDLE;
        Serial.println("command: re-identify; axes stopped");
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
