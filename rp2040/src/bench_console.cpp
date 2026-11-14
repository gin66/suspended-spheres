#include "bench_console.h"

#include <Arduino.h>
#include <stdlib.h>
#include <string.h>

#include "board_pins.h"
#include "can_node.h"
#include "pico/can2040_lite_pico.h"
#include "stepper_axes.h"
#include "tmc2209.h"

namespace bench_console {
namespace {

constexpr uint8_t kMaxArgs = 8;
constexpr size_t kLineLength = 96;
constexpr int32_t kBenchAccel = 2000;

char s_line[kLineLength];
size_t s_length = 0;
bool s_discard = false;
bool s_quiet = false;
bool s_reinit_requested = false;

bool parse_u32(const char* text, uint32_t* value) {
    if (text == nullptr || *text == '\0' || *text == '-') {
        return false;
    }
    char* end = nullptr;
    const unsigned long parsed = strtoul(text, &end, 0);
    if (end == text || *end != '\0') {
        return false;
    }
    *value = (uint32_t)parsed;
    return true;
}

bool parse_hex(const char* text, uint32_t* value) {
    if (text == nullptr || *text == '\0' || *text == '-') {
        return false;
    }
    char* end = nullptr;
    const unsigned long parsed = strtoul(text, &end, 16);
    if (end == text || *end != '\0') {
        return false;
    }
    *value = (uint32_t)parsed;
    return true;
}

bool parse_i32(const char* text, int32_t* value) {
    if (text == nullptr || *text == '\0') {
        return false;
    }
    char* end = nullptr;
    const long parsed = strtol(text, &end, 0);
    if (end == text || *end != '\0') {
        return false;
    }
    *value = (int32_t)parsed;
    return true;
}

bool parse_axis(const char* text, uint8_t* axis) {
    uint32_t value = 0;
    if (!parse_u32(text, &value) || value >= tmc2209::kDriverCount) {
        return false;
    }
    *axis = (uint8_t)value;
    return true;
}

void print_result(bool ok) {
    Serial.println(ok ? "ok" : "FAILED");
}

void print_help() {
    Serial.println("bench commands:");
    Serial.println("  status [axis]              read DRV_STATUS, SG_RESULT, TSTEP");
    Serial.println("  read <axis>                read all registers + expected init values");
    Serial.println("  stats [axis]               UART read/write/IFCNT health counters");
    Serial.println("  reinit                     stop, re-run TMC bring-up; axes stay disabled");
    Serial.println("  quiet [on|off]             silence periodic CAN stats and command logs");
    Serial.println("  selftest                   bit-banged TX pad readback");
    Serial.println("  scan                       read GCONF at UART addresses 0..3");
    Serial.println("  bbprobe [addr]             pure bit-banged GCONF read + raw samples");
    Serial.println("  line [us]                  release GPIO9 and report bus level/edges");
    Serial.println("  idle [bits]                show/set idle high bits after request (0..12)");
    Serial.println("  senddelay <axis> <0..15>   SLAVECONF SENDDELAY reply gap");
    Serial.println("  monitor <axis> <s> [ms]    SG/PWM/current samples; 0 s = stop");
    Serial.println("  timeuart <axis> [n]        time one SG read and one refresh, us");
    Serial.println("  sample [cycles]            UART sample period, 60..250 clk cycles");
    Serial.println("  cur <axis> <run_ma> [hold_ma]");
    Serial.println("  sg <axis> <0..255|off>     SGTHRS; off closes TCOOLTHRS");
    Serial.println("  mode <axis> <stealth|spread>  chopper mode, axis stopped");
    Serial.println("  tcool <axis> <value>       TCOOLTHRS (20 bit)");
    Serial.println("  micro <axis> <2..256> [intpol 0|1]");
    Serial.println("  chop <axis> <hex>          raw CHOPCONF, e.g. 0x14020054");
    Serial.println("  pwm <axis> <hex>           raw PWMCONF, e.g. 0xC10D0024");
    Serial.println("  raw <axis> <reg> [value]   register access, hex e.g. 0x6C");
    Serial.println("  move <axis> <steps> <hz>   finite move (requires enable)");
    Serial.println("  run <axis> <hz> [0|1]      continuous, 1 = forward (requires enable)");
    Serial.println("  stop [axis]");
    Serial.println("  enable <axis> <0|1>");
    Serial.println("  diag [clear]");
    Serial.println("  can stats                  driver + app counters");
    Serial.println("  can send <id> [hex]        raw standard frame, up to 8 data bytes");
    Serial.println("  can pins [ms]              sample CAN RX/TX pad levels and edges");
    Serial.println("  can arm                    clean RX re-arm (flush FIFO/parser)");
    Serial.println("  can raw [ms]               raw RX FIFO words for PC-side analysis");
    Serial.println("  can dbg [clear]            PIO IRQ count and FDEBUG (RXSTALL etc.)");
    Serial.println("  can rec [clear]            dump/clear the RX edge-pair record");
    Serial.println("  can ack [n] [id]           ACK-slot tolerance from the RX record");
}

void command_status(int argc, char** argv) {
    if (argc >= 2) {
        uint8_t axis = 0;
        if (!parse_axis(argv[1], &axis)) {
            Serial.println("bad axis");
            return;
        }
        tmc2209::refresh(axis);
        tmc2209::printDriver(axis);
        return;
    }
    for (uint8_t i = 0; i < tmc2209::kDriverCount; ++i) {
        tmc2209::refresh(i);
        tmc2209::printDriver(i);
    }
}

void command_read(int argc, char** argv) {
    if (argc < 2) {
        Serial.println("usage: read <axis>");
        return;
    }
    uint8_t axis = 0;
    if (!parse_axis(argv[1], &axis)) {
        Serial.println("bad axis");
        return;
    }
    tmc2209::printRegisters(axis);
}

void command_current(int argc, char** argv) {
    if (argc < 3) {
        Serial.println("usage: cur <axis> <run_ma> [hold_ma]");
        return;
    }
    uint8_t axis = 0;
    uint32_t run_ma = 0;
    uint32_t hold_ma = 0;
    if (!parse_axis(argv[1], &axis) || !parse_u32(argv[2], &run_ma)) {
        Serial.println("bad argument");
        return;
    }
    hold_ma = run_ma;
    if (argc >= 4 && !parse_u32(argv[3], &hold_ma)) {
        Serial.println("bad hold_ma");
        return;
    }
    const bool ok = tmc2209::setCurrents(axis, (uint16_t)run_ma,
                                         (uint16_t)hold_ma);
    if (!ok) {
        print_result(false);
        return;
    }
    tmc2209::refresh(axis);
    tmc2209::printDriver(axis);
}

void command_stats(int argc, char** argv) {
    if (argc < 2) {
        tmc2209::printStats();
        return;
    }
    uint8_t axis = 0;
    if (!parse_axis(argv[1], &axis)) {
        Serial.println("bad axis");
        return;
    }
    tmc2209::checkIfcnt(axis);
    tmc2209::printStats(axis);
}

void command_sg(int argc, char** argv) {
    if (argc < 3) {
        Serial.println("usage: sg <axis> <0..255|off>");
        return;
    }
    uint8_t axis = 0;
    if (!parse_axis(argv[1], &axis)) {
        Serial.println("bad axis");
        return;
    }
    if (strcmp(argv[2], "off") == 0) {
        // SGTHRS = 0 is not "off"; it sets the threshold to 0. Close the
        // velocity window instead so the StallGuard result cannot pulse DIAG.
        print_result(tmc2209::setTcoolthrs(axis, 0));
        return;
    }
    uint32_t value = 0;
    if (!parse_u32(argv[2], &value) || value > 255) {
        Serial.println("bad argument");
        return;
    }
    print_result(tmc2209::setStallGuard(axis, (uint8_t)value));
}

void command_mode(int argc, char** argv) {
    if (argc < 3) {
        Serial.println("usage: mode <axis> <stealth|spread>");
        return;
    }
    uint8_t axis = 0;
    if (!parse_axis(argv[1], &axis)) {
        Serial.println("bad axis");
        return;
    }
    bool spread = false;
    if (strcmp(argv[2], "spread") == 0) {
        spread = true;
    } else if (strcmp(argv[2], "stealth") != 0) {
        Serial.println("mode: stealth|spread");
        return;
    }
    if (stepper_axes::isRunning(axis)) {
        Serial.println("stop the axis before changing mode");
        return;
    }
    print_result(tmc2209::setSpreadCycle(axis, spread));
}

void command_tcool(int argc, char** argv) {
    if (argc < 3) {
        Serial.println("usage: tcool <axis> <value>");
        return;
    }
    uint8_t axis = 0;
    uint32_t value = 0;
    if (!parse_axis(argv[1], &axis) || !parse_u32(argv[2], &value) ||
        value > 0xFFFFF) {
        Serial.println("bad argument");
        return;
    }
    print_result(tmc2209::setTcoolthrs(axis, value));
}

void command_micro(int argc, char** argv) {
    if (argc < 3) {
        Serial.println("usage: micro <axis> <2..256> [intpol 0|1]");
        return;
    }
    uint8_t axis = 0;
    uint32_t microsteps = 0;
    uint32_t intpol = 1;
    if (!parse_axis(argv[1], &axis) || !parse_u32(argv[2], &microsteps)) {
        Serial.println("bad argument");
        return;
    }
    if (argc >= 4 && !parse_u32(argv[3], &intpol)) {
        Serial.println("bad intpol");
        return;
    }
    print_result(tmc2209::setMicrosteps(axis, (uint16_t)microsteps,
                                        intpol != 0));
}

void command_chop(int argc, char** argv) {
    if (argc < 3) {
        Serial.println("usage: chop <axis> <hex>");
        return;
    }
    uint8_t axis = 0;
    uint32_t value = 0;
    if (!parse_axis(argv[1], &axis) || !parse_hex(argv[2], &value)) {
        Serial.println("bad argument");
        return;
    }
    print_result(tmc2209::setChopper(axis, value));
}

void command_senddelay(int argc, char** argv) {
    if (argc < 3) {
        Serial.println("usage: senddelay <axis> <0..15>");
        return;
    }
    uint8_t axis = 0;
    uint32_t value = 0;
    if (!parse_axis(argv[1], &axis) || !parse_u32(argv[2], &value) ||
        value > 15) {
        Serial.println("bad argument");
        return;
    }
    print_result(tmc2209::setSendDelay(axis, (uint8_t)value));
}

void command_monitor(int argc, char** argv) {
    if (argc < 3) {
        Serial.println("usage: monitor <axis> <seconds> [interval_ms]");
        return;
    }
    uint8_t axis = 0;
    uint32_t seconds = 0;
    uint32_t interval_ms = 100;
    if (!parse_axis(argv[1], &axis) || !parse_u32(argv[2], &seconds)) {
        Serial.println("bad argument");
        return;
    }
    if (argc >= 4 && !parse_u32(argv[3], &interval_ms)) {
        Serial.println("bad interval");
        return;
    }
    if (seconds > 3600u) {
        seconds = 3600u;
    }
    print_result(tmc2209::startMonitor(axis, seconds * 1000u, interval_ms));
}

void command_timeuart(int argc, char** argv) {
    if (argc < 2) {
        Serial.println("usage: timeuart <axis> [n]");
        return;
    }
    uint8_t axis = 0;
    uint32_t n = 20;
    if (!parse_axis(argv[1], &axis)) {
        Serial.println("bad axis");
        return;
    }
    if (argc >= 3 && (!parse_u32(argv[2], &n) || n == 0 || n > 1000)) {
        Serial.println("bad n");
        return;
    }
    tmc2209::timeUart(axis, (uint16_t)n);
}

void command_sample(int argc, char** argv) {
    if (argc < 2) {
        Serial.print("sample ");
        Serial.print(tmc2209::sampleCycles());
        Serial.print(" cyc, ");
        Serial.print(tmc2209::uartBaud());
        Serial.println(" Baud");
        return;
    }
    uint32_t cycles = 0;
    if (!parse_u32(argv[1], &cycles)) {
        Serial.println("usage: sample <60..250>");
        return;
    }
    for (uint8_t i = 0; i < board::kDriveCount; ++i) {
        if (stepper_axes::isRunning(i)) {
            Serial.println("stop all axes before changing the sample rate");
            return;
        }
    }
    if (!tmc2209::setSampleCycles(cycles)) {
        Serial.println("usage: sample <60..250>");
        return;
    }
    Serial.print("sample ");
    Serial.print(tmc2209::sampleCycles());
    Serial.print(" cyc, ");
    Serial.print(tmc2209::uartBaud());
    Serial.println(" Baud");
}

void command_line(int argc, char** argv) {
    uint32_t us = 0;
    if (argc >= 2 && !parse_u32(argv[1], &us)) {
        Serial.println("bad argument");
        return;
    }
    tmc2209::sampleLine(us);
}

void command_idle(int argc, char** argv) {
    if (argc >= 2) {
        uint32_t bits = 0;
        if (!parse_u32(argv[1], &bits) || !tmc2209::setIdleBits(bits)) {
            Serial.println("usage: idle <0..12> bits");
            return;
        }
    }
    Serial.print("idle: ");
    Serial.print(tmc2209::idleBits());
    Serial.println(" bits");
}

void command_pwm(int argc, char** argv) {
    if (argc < 3) {
        Serial.println("usage: pwm <axis> <hex>");
        return;
    }
    uint8_t axis = 0;
    uint32_t value = 0;
    if (!parse_axis(argv[1], &axis) || !parse_hex(argv[2], &value)) {
        Serial.println("bad argument");
        return;
    }
    print_result(tmc2209::setPwmConf(axis, value));
}

void command_raw(int argc, char** argv) {
    if (argc < 3) {
        Serial.println("usage: raw <axis> <reg> [value]");
        return;
    }
    uint8_t axis = 0;
    uint32_t address = 0;
    if (!parse_axis(argv[1], &axis) || !parse_hex(argv[2], &address) ||
        address > 0x7F) {
        Serial.println("bad argument");
        return;
    }
    if (argc >= 4) {
        uint32_t value = 0;
        if (!parse_hex(argv[3], &value)) {
            Serial.println("bad value");
            return;
        }
        // OTP is irreversible. The driver layer also refuses VACTUAL,
        // IHOLD_IRUN and unsafe GCONF bits.
        if (address == 0x04) {
            Serial.println("refusing OTP_PROG writes");
            return;
        }
        if (!tmc2209::writeRegister(axis, (uint8_t)address, value)) {
            Serial.println("write refused");
            return;
        }
    }
    uint32_t readback = 0;
    if (!tmc2209::readRegister(axis, (uint8_t)address, &readback)) {
        Serial.println("read FAILED");
        return;
    }
    Serial.print("0x");
    Serial.println(readback, HEX);
}

void command_move(int argc, char** argv) {
    if (argc < 4) {
        Serial.println("usage: move <axis> <steps> <hz>");
        return;
    }
    uint8_t axis = 0;
    int32_t steps = 0;
    uint32_t hz = 0;
    if (!parse_axis(argv[1], &axis) || !parse_i32(argv[2], &steps) ||
        !parse_u32(argv[3], &hz) || hz == 0) {
        Serial.println("bad argument");
        return;
    }
    if (!tmc2209::ready(axis)) {
        Serial.println("driver not ready");
        return;
    }
    if (!stepper_axes::isAxisEnabled(axis)) {
        Serial.print("axis ");
        Serial.print(axis);
        Serial.print(" disabled; use enable ");
        Serial.print(axis);
        Serial.println(" 1");
        return;
    }
    stepper_axes::setSpeed(axis, hz);
    stepper_axes::setAcceleration(axis, kBenchAccel);
    const bool ok = stepper_axes::move(axis, steps);
    if (ok) {
        Serial.print("queued ");
        Serial.print(steps);
        Serial.print(" steps at ");
        Serial.print(hz);
        Serial.println(" Hz");
    } else {
        print_result(false);
    }
}

void command_run(int argc, char** argv) {
    if (argc < 3) {
        Serial.println("usage: run <axis> <hz> [0|1]");
        return;
    }
    uint8_t axis = 0;
    uint32_t hz = 0;
    uint32_t forward = 1;
    if (!parse_axis(argv[1], &axis) || !parse_u32(argv[2], &hz) || hz == 0) {
        Serial.println("bad argument");
        return;
    }
    if (argc >= 4 && !parse_u32(argv[3], &forward)) {
        Serial.println("bad direction");
        return;
    }
    if (!tmc2209::ready(axis)) {
        Serial.println("driver not ready");
        return;
    }
    if (!stepper_axes::isAxisEnabled(axis)) {
        Serial.print("axis ");
        Serial.print(axis);
        Serial.print(" disabled; use enable ");
        Serial.print(axis);
        Serial.println(" 1");
        return;
    }
    stepper_axes::setSpeed(axis, hz);
    stepper_axes::setAcceleration(axis, kBenchAccel);
    print_result(stepper_axes::run(axis, forward != 0));
}

void command_stop(int argc, char** argv) {
    if (argc < 2) {
        stepper_axes::stopAll();
        Serial.println("stopped all");
        return;
    }
    uint8_t axis = 0;
    if (!parse_axis(argv[1], &axis)) {
        Serial.println("bad axis");
        return;
    }
    stepper_axes::stop(axis);
    Serial.println("stopped");
}

void command_enable(int argc, char** argv) {
    if (argc < 3) {
        Serial.println("usage: enable <axis> <0|1>");
        return;
    }
    uint8_t axis = 0;
    uint32_t enabled = 0;
    if (!parse_axis(argv[1], &axis) || !parse_u32(argv[2], &enabled)) {
        Serial.println("bad argument");
        return;
    }
    if (enabled != 0 && !tmc2209::ready(axis)) {
        Serial.println("driver not ready");
        return;
    }
    stepper_axes::setAxisEnabled(axis, enabled != 0);
    print_result(true);
}

void command_diag(int argc, char** argv) {
    if (argc >= 2 && strcmp(argv[1], "clear") == 0) {
        tmc2209::clearDiagEvents();
        Serial.println("diag events cleared");
        return;
    }
    Serial.print("diag=");
    for (uint8_t i = 0; i < board::kDriveCount; ++i) {
        Serial.print(digitalRead(board::kDrives[i].diag) ? 1 : 0);
    }
    Serial.println();
}

void command_quiet(int argc, char** argv) {
    if (argc < 2) {
        s_quiet = !s_quiet;
    } else if (strcmp(argv[1], "on") == 0 || strcmp(argv[1], "1") == 0) {
        s_quiet = true;
    } else if (strcmp(argv[1], "off") == 0 || strcmp(argv[1], "0") == 0) {
        s_quiet = false;
    } else {
        Serial.println("usage: quiet [on|off]");
        return;
    }
    Serial.print("logs ");
    Serial.println(s_quiet ? "off" : "on");
}

void command_reinit() {
    s_reinit_requested = true;
    Serial.println("reinit requested");
}

int hex_nibble(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

bool parse_can_data(const char* text, uint8_t data[8], uint8_t* dlc) {
    const size_t len = strlen(text);
    if (len > 16u || (len & 1u) != 0u) {
        return false;
    }
    for (size_t i = 0; i < len / 2u; i++) {
        const int hi = hex_nibble(text[i * 2u]);
        const int lo = hex_nibble(text[i * 2u + 1u]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        data[i] = (uint8_t)((hi << 4) | lo);
    }
    *dlc = (uint8_t)(len / 2u);
    return true;
}

int32_t permille(int32_t value16, uint32_t bit_ticks) {
    const int64_t t16 = (int64_t)bit_ticks * 16;
    if (t16 == 0) {
        return 0;
    }
    return (int32_t)(((int64_t)value16 * 1000) / t16);
}

void print_ack_sample(int index, const can_node::AckSample& s) {
    if (!s.valid) {
        Serial.print(index);
        Serial.println(": no measurable frame (timeout/filter)");
        return;
    }
    Serial.print(index);
    Serial.print(": id=0x");
    Serial.print(s.rx_id, HEX);
    Serial.print(" stuffed=");
    Serial.print(s.stuffed_bits);
    if (!s.acknowledged) {
        Serial.println(" NACK (slot recessive)");
        return;
    }
    const uint32_t t = s.bit_ticks;
    Serial.print(" T=");
    Serial.print(t);
    Serial.print(" err=");
    Serial.print(permille(s.phase_16, t));
    Serial.print("m width=");
    Serial.print(permille(s.width_16, t));
    Serial.print("m start_margin=");
    Serial.print(permille(s.start_margin_16, t));
    Serial.print("m end_margin=");
    Serial.print(permille(s.end_margin_16, t));
    Serial.println("m");
}

void command_can(int argc, char** argv) {
    if (argc < 2 || strcmp(argv[1], "stats") == 0) {
        can_node::print_stats();
        return;
    }
    if (strcmp(argv[1], "send") == 0) {
        uint32_t id = 0;
        uint8_t data[8] = {};
        uint8_t dlc = 0;
        if (argc < 3 || !parse_u32(argv[2], &id) || id > 0x7FFu) {
            Serial.println("usage: can send <id> [hex-data]");
            return;
        }
        if (argc >= 4 && !parse_can_data(argv[3], data, &dlc)) {
            Serial.println("bad hex data (even length, <= 8 bytes)");
            return;
        }
        print_result(can_node::send((uint16_t)id, data, dlc));
        return;
    }
    if (strcmp(argv[1], "raw") == 0) {
        Serial.println("raw capture removed in this can2040-lite version");
        return;
    }
    if (strcmp(argv[1], "arm") == 0) {
        can_node::request_rx_arm();
        delay(10);
        Serial.println("rx arm requested");
        return;
    }
    if (strcmp(argv[1], "dbg") == 0) {
        if (argc >= 3 && strcmp(argv[2], "clear") == 0) {
            can_node::pio_fdebug_clear();
            Serial.println("fdebug cleared");
            return;
        }
        Serial.print("irq=");
        Serial.print(can_node::irq_count());
        Serial.print(" arms=");
        Serial.print(can_node::arm_count());
        Serial.print(" us_max=");
        Serial.print(can_node::irq_us_max());
        Serial.print(" us_sum=");
        Serial.print(can_node::irq_us_sum());
        Serial.print(" fdebug=0x");
        Serial.println(can_node::pio_fdebug(), HEX);
        return;
    }
    if (strcmp(argv[1], "pins") == 0) {
        uint32_t ms = 200;
        if (argc >= 3 && (!parse_u32(argv[2], &ms) || ms > 5000u)) {
            Serial.println("usage: can pins [ms]");
            return;
        }
        const uint32_t edges = can_node::sample_rx_edges(ms);
        Serial.print("CAN rx(GPIO4)=");
        Serial.print(can_node::rx_level());
        Serial.print(" tx(GPIO5)=");
        Serial.print(can_node::tx_level());
        Serial.print(" edges/");
        Serial.print(ms);
        Serial.print("ms=");
        Serial.println(edges);
        return;
    }
    if (strcmp(argv[1], "rec") == 0) {
        if (argc >= 3 && strcmp(argv[2], "clear") == 0) {
            can_node::record_clear();
            Serial.println("record cleared");
        } else {
            can_node::record_print();
        }
        return;
    }
    if (strcmp(argv[1], "ack") == 0) {
        uint32_t samples = 5;
        uint32_t id = 0x00Bu;  // ASK broadcast from the show controller
        if (argc >= 3 && !parse_u32(argv[2], &samples)) {
            Serial.println("usage: can ack [samples] [id]");
            return;
        }
        if (argc >= 4 && !parse_u32(argv[3], &id)) {
            Serial.println("usage: can ack [samples] [id]");
            return;
        }
        if (!can_node::recording_enabled()) {
            Serial.println("recording disabled; build fly_micro4_record");
            return;
        }
        if (samples == 0 || samples > 50) {
            Serial.println("samples 1..50");
            return;
        }
        int32_t min_err = 0, max_err = 0, sum_err = 0;
        int32_t min_start = 0, min_end = 0;
        int32_t min_width = 0, max_width = 0;
        uint32_t acked = 0, nacked = 0, missed = 0;
        for (uint32_t i = 0; i < samples; i++) {
            can_node::AckSample s;
            const bool ok = can_node::capture_ack((uint16_t)(id & 0x7FFu),
                                                  6000u, &s);
            print_ack_sample((int)i, s);
            if (!ok) {
                missed++;
                continue;
            }
            if (!s.acknowledged) {
                nacked++;
                continue;
            }
            if (acked == 0) {
                min_err = max_err = s.phase_16;
                min_start = s.start_margin_16;
                min_end = s.end_margin_16;
                min_width = max_width = s.width_16;
            } else {
                if (s.phase_16 < min_err) min_err = s.phase_16;
                if (s.phase_16 > max_err) max_err = s.phase_16;
                if (s.start_margin_16 < min_start) min_start = s.start_margin_16;
                if (s.end_margin_16 < min_end) min_end = s.end_margin_16;
                if (s.width_16 < min_width) min_width = s.width_16;
                if (s.width_16 > max_width) max_width = s.width_16;
            }
            sum_err += s.phase_16;
            acked++;
        }
        Serial.print("ack summary: ok=");
        Serial.print(acked);
        Serial.print(" nack=");
        Serial.print(nacked);
        Serial.print(" miss=");
        Serial.println(missed);
        if (acked > 0) {
            const uint32_t bit_ticks = can_node::bit_ticks();
            Serial.print("phase err min/max/mean = ");
            Serial.print(permille(min_err, bit_ticks));
            Serial.print("/");
            Serial.print(permille(max_err, bit_ticks));
            Serial.print("/");
            Serial.print(permille(sum_err / (int32_t)acked, bit_ticks));
            Serial.println(" milli-bit");
            Serial.print("worst sample margins: start=");
            Serial.print(permille(min_start, bit_ticks));
            Serial.print("m end=");
            Serial.print(permille(min_end, bit_ticks));
            Serial.println("m");
            Serial.print("ACK width min/max = ");
            Serial.print(permille(min_width, bit_ticks));
            Serial.print("/");
            Serial.print(permille(max_width, bit_ticks));
            Serial.println(" milli-bit");
        }
        return;
    }
    Serial.println("usage: can stats|send|pins|arm|raw|dbg|rec [clear]|ack [n] [id]");
}

void execute(char* line) {
    char* argv[kMaxArgs];
    uint8_t argc = 0;
    char* cursor = line;
    while (*cursor != '\0' && argc < kMaxArgs) {
        while (*cursor == ' ') {
            ++cursor;
        }
        if (*cursor == '\0') {
            break;
        }
        argv[argc++] = cursor;
        while (*cursor != '\0' && *cursor != ' ') {
            ++cursor;
        }
        if (*cursor != '\0') {
            *cursor++ = '\0';
        }
    }
    if (argc == 0) {
        return;
    }

    const char* command = argv[0];
    if (strcmp(command, "help") == 0 || strcmp(command, "?") == 0) {
        print_help();
    } else if (strcmp(command, "status") == 0) {
        command_status(argc, argv);
    } else if (strcmp(command, "read") == 0) {
        command_read(argc, argv);
    } else if (strcmp(command, "stats") == 0) {
        command_stats(argc, argv);
    } else if (strcmp(command, "cur") == 0) {
        command_current(argc, argv);
    } else if (strcmp(command, "sg") == 0) {
        command_sg(argc, argv);
    } else if (strcmp(command, "mode") == 0) {
        command_mode(argc, argv);
    } else if (strcmp(command, "tcool") == 0) {
        command_tcool(argc, argv);
    } else if (strcmp(command, "micro") == 0) {
        command_micro(argc, argv);
    } else if (strcmp(command, "chop") == 0) {
        command_chop(argc, argv);
    } else if (strcmp(command, "pwm") == 0) {
        command_pwm(argc, argv);
    } else if (strcmp(command, "senddelay") == 0) {
        command_senddelay(argc, argv);
    } else if (strcmp(command, "monitor") == 0) {
        command_monitor(argc, argv);
    } else if (strcmp(command, "timeuart") == 0) {
        command_timeuart(argc, argv);
    } else if (strcmp(command, "sample") == 0) {
        command_sample(argc, argv);
    } else if (strcmp(command, "line") == 0) {
        command_line(argc, argv);
    } else if (strcmp(command, "idle") == 0) {
        command_idle(argc, argv);
    } else if (strcmp(command, "raw") == 0) {
        command_raw(argc, argv);
    } else if (strcmp(command, "move") == 0) {
        command_move(argc, argv);
    } else if (strcmp(command, "run") == 0) {
        command_run(argc, argv);
    } else if (strcmp(command, "stop") == 0) {
        command_stop(argc, argv);
    } else if (strcmp(command, "enable") == 0) {
        command_enable(argc, argv);
    } else if (strcmp(command, "quiet") == 0) {
        command_quiet(argc, argv);
    } else if (strcmp(command, "reinit") == 0) {
        command_reinit();
    } else if (strcmp(command, "selftest") == 0) {
        tmc2209::selftest();
    } else if (strcmp(command, "scan") == 0) {
        tmc2209::scanAddresses();
    } else if (strcmp(command, "bbprobe") == 0) {
        const int address = argc > 1 ? atoi(argv[1]) : -1;
        if (address >= 0 && address < 4) {
            tmc2209::bbProbe((uint8_t)address);
        } else {
            tmc2209::bbProbeAll();
        }
    } else if (strcmp(command, "diag") == 0) {
        command_diag(argc, argv);
    } else if (strcmp(command, "can") == 0) {
        command_can(argc, argv);
    } else {
        Serial.print("unknown command: ");
        Serial.println(command);
        print_help();
    }
}

}  // namespace

void begin() {
    s_length = 0;
    s_discard = false;
    s_quiet = false;
    s_reinit_requested = false;
    Serial.println("bench console: type help");
}

bool take_reinit_request() {
    const bool requested = s_reinit_requested;
    s_reinit_requested = false;
    return requested;
}

bool logs_enabled() {
    return !s_quiet;
}

void poll() {
    while (Serial.available() > 0) {
        const int c = Serial.read();
        if (c < 0) {
            continue;
        }
        // Accept CR, LF and CRLF. A CRLF produces an empty second line, which
        // is ignored because only non-empty lines execute.
        if (c == '\r' || c == '\n') {
            if (!s_discard && s_length > 0) {
                s_line[s_length] = '\0';
                execute(s_line);
            }
            s_length = 0;
            s_discard = false;
            continue;
        }
        if (s_discard) {
            continue;
        }
        if (s_length + 1 < kLineLength) {
            s_line[s_length++] = (char)c;
        } else {
            // Drop an overlong line instead of splitting it into commands.
            s_length = 0;
            s_discard = true;
        }
    }
}

}  // namespace bench_console
