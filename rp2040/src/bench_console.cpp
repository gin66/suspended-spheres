#include "bench_console.h"

#include <Arduino.h>
#include <stdlib.h>
#include <string.h>

#include "board_pins.h"
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
    Serial.println("  reinit                     stop, re-run TMC bring-up; axes stay disabled");
    Serial.println("  quiet [on|off]             silence periodic CAN stats and command logs");
    Serial.println("  selftest                   bit-banged TX pad readback");
    Serial.println("  scan                       read GCONF at UART addresses 0..3");
    Serial.println("  bbprobe [addr]             pure bit-banged GCONF read + raw samples");
    Serial.println("  line [us]                  release GPIO9 and report bus level/edges");
    Serial.println("  idle [bits]                show/set idle high bits after request (0..12)");
    Serial.println("  senddelay <axis> <0..15>   SLAVECONF SENDDELAY reply gap");
    Serial.println("  cur <axis> <run_ma> [hold_ma]");
    Serial.println("  sg <axis> <0..255>         SGTHRS");
    Serial.println("  tcool <axis> <value>       TCOOLTHRS (20 bit)");
    Serial.println("  micro <axis> <2..256> [intpol 0|1]");
    Serial.println("  chop <axis> <hex>          raw CHOPCONF, e.g. 0x14020054");
    Serial.println("  pwm <axis> <hex>           raw PWMCONF, e.g. 0xC10D0024");
    Serial.println("  raw <axis> <reg> [value]   register access, hex e.g. 0x6C");
    Serial.println("  move <axis> <steps> <hz>   finite move (requires enable)");
    Serial.println("  run <axis> <hz> [0|1]      continuous, 1 = forward (requires enable)");
    Serial.println("  stop [axis]");
    Serial.println("  enable <axis> <0|1>");
    Serial.println("  diag");
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

void command_sg(int argc, char** argv) {
    if (argc < 3) {
        Serial.println("usage: sg <axis> <0..255>");
        return;
    }
    uint8_t axis = 0;
    uint32_t value = 0;
    if (!parse_axis(argv[1], &axis) || !parse_u32(argv[2], &value) ||
        value > 255) {
        Serial.println("bad argument");
        return;
    }
    print_result(tmc2209::setStallGuard(axis, (uint8_t)value));
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

void command_diag() {
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
    } else if (strcmp(command, "cur") == 0) {
        command_current(argc, argv);
    } else if (strcmp(command, "sg") == 0) {
        command_sg(argc, argv);
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
        command_diag();
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
