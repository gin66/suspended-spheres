#include "tmc2209.h"

#include <Arduino.h>
#include <hardware/clocks.h>
#include <hardware/gpio.h>
#include <hardware/structs/systick.h>
#include <hardware/sync.h>
#include <pico/stdlib.h>
#include <string.h>

#include "board_pins.h"
#include "stepper_axes.h"
#include "tmc_uart_bits.h"

namespace tmc2209 {
namespace {

static_assert(kDriverCount == board::kDriveCount,
              "the TMC2209 table must follow the drive table");

// The four PDN_UART pins share GPIO9. GPIO9 is UART1 RX but not a UART1 TX
// pin, and both PIO blocks are fully allocated (can2040 and four
// FastAccelStepper state machines). The whole datagram and the reply are
// therefore driven and sampled through SIO; no UART peripheral or TMC library
// is involved.

// Register addresses (doc/motion_streaming_whitepaper.md §5.6).
constexpr uint8_t kGconf = 0x00;
constexpr uint8_t kGstat = 0x01;
constexpr uint8_t kIfcnt = 0x02;
constexpr uint8_t kSlaveconf = 0x03;
constexpr uint8_t kOtpProg = 0x04;
constexpr uint8_t kOtpRead = 0x05;
constexpr uint8_t kIoin = 0x06;
constexpr uint8_t kFactoryConf = 0x07;
constexpr uint8_t kIholdIrun = 0x10;
constexpr uint8_t kTpowerdown = 0x11;
constexpr uint8_t kTstep = 0x12;
constexpr uint8_t kTpwmthrs = 0x13;
constexpr uint8_t kTcoolthrs = 0x14;
constexpr uint8_t kVactual = 0x22;
constexpr uint8_t kSgthrs = 0x40;
constexpr uint8_t kSgResult = 0x41;
constexpr uint8_t kMsCnt = 0x6A;
constexpr uint8_t kMsCurAct = 0x6B;
constexpr uint8_t kChopconf = 0x6C;
constexpr uint8_t kDrvStatus = 0x6F;
constexpr uint8_t kPwmconf = 0x70;
constexpr uint8_t kPwmScale = 0x71;
constexpr uint8_t kPwmAuto = 0x72;

constexpr uint16_t kDefaultRunMa = 300;
constexpr uint16_t kDefaultHoldMa = 300;
constexpr uint16_t kMaxRunMa = 800;  // bench cap; the reference uses 800 mA
constexpr uint8_t kDefaultMicrosteps = 16;
constexpr uint8_t kDefaultSgthrs = 40;
constexpr uint32_t kDefaultTcoolthrs = 0xFFFFF;
constexpr uint32_t kDefaultPwmconf = 0xC10D0024;
constexpr uint32_t kDefaultTpowerdown = 20;

// bit 6 = 1 pdn_disable, bit 7 = 1 mstep_reg_select, bit 8 = 1 multistep_filt,
// bits 0/1 = 0 keep external current sensing and the digital current scale.
constexpr uint32_t kInitGconf = (1u << 6) | (1u << 7) | (1u << 8);

// Measured 2026-11-10: SpreadCycle holds ~4x the StealthChop step rate
// (M0 74k vs 18k, M2 >=94k vs 21k at 300 mA). Motion therefore boots in
// SpreadCycle; homing switches to StealthChop at standstill.
constexpr bool kMotionSpreadCycle = true;
constexpr uint32_t initGconfValue() {
    return kInitGconf | (kMotionSpreadCycle ? (1u << 2) : 0u);
}

// SENDDELAY is written before any read so the reply always has a clean gap
// after our last stop bit. The bench can override it per axis.
constexpr uint8_t kDefaultSendDelay = 2;

// CHOPCONF/PWMCONF tuning is still queued; the SpreadCycle numbers in
// doc/tmc2209_characterization.md §8 Phase 4a use this bring-up chopper.
// CHOPCONF reset value used by the TMC2208/2209: TOFF=3, HSTRT=5, HEND=0,
// TBL=0, MRES=0, INTPOL=1. The bring-up then sets TOFF=4 and MRES=4.
constexpr uint32_t kChopconfBase = 0x10000053;
constexpr uint32_t kChopconfMres = 4;  // 16 microsteps
constexpr uint32_t kChopconfToff = 4;
constexpr uint32_t kChopconfIntpol = 1;

// The reply is decoded at exactly 4 samples per bit. The sample period is
// the 1 MHz tick expressed in clk_sys cycles (clk_sys / 1 000 000, exact at
// an integer-MHz clock) and one bit is exactly 4 samples, so every deadline
// and the decoder's nominal period are integers of one grid — no rounding
// at all. At 125 MHz: 1 sample = 125 cycles, 1 bit = 500 cycles = 4 us
// (fixed 250 kBaud); the TMC auto-baud locks on at any other integer-MHz
// clk_sys, so the exact 4-us figure is not load-bearing.
constexpr uint32_t kSamplesPerBit = 4;
constexpr uint32_t kCaptureBits = 128;
constexpr uint32_t kCaptureSamplesMax = kCaptureBits * kSamplesPerBit;
// Bit length in sample steps times 256: the decoder fits the reply period
// against this nominal value.
constexpr uint32_t kBitPeriodQ8 = kSamplesPerBit * 256u;
// Idle high bits between the last stop bit and the reply capture. The TMC
// starts its reply about 7 bit times after the stop bit. Holding 4 releases
// the line early; the board pull-down then holds it low for a few bits,
// which can disturb the TMC. The bench can sweep this with `idle`.
uint32_t s_idle_bits = 4;

// Datasheet failure recovery: the line must be idle high for at least 12 bit
// times before the first start bit after a glitch or long idle. A 1 us prelude
// is below the TMC's 16-clock glitch filter, so the first datagram after idle
// can be discarded. This wait runs with interrupts enabled.
constexpr uint32_t kPreIdleBits = 12;

// Sample-grid timing, computed once in begin() and then left alone.
uint32_t s_sample_cycles = 0;  // one sample period, in clk_sys cycles
uint16_t s_bit_cycles = 0;     // kSamplesPerBit * s_sample_cycles

uint8_t s_samples[(kCaptureSamplesMax + 7u) / 8u];
tmc_uart_bits::CaptureReport s_capture = {};
uint32_t s_capture_elapsed_us = 0;
uint32_t s_capture_expected_us = 0;

uint32_t s_irq = 0;
bool s_irq_held = false;

// One counter set per UART address (the TMC2209 address is 0..3).
constexpr uint8_t kAddressCount = 4;
UartStats s_uart_stats[kAddressCount];

UartStats& statsFor(uint8_t address) {
    return s_uart_stats[address & 0x03u];
}

// Round-robin IFCNT check, one ready driver per second.
uint32_t s_ifcnt_check_ms = 0;
uint8_t s_ifcnt_check_index = 0;

// Scheduling runs on the cycle clock: it reads the low 16 bits of SysTick's
// 24-bit down counter and XORs them with 0xFFFF, which turns the down count
// into an up count. Plain 16-bit arithmetic wraps naturally, so no mask is
// needed. The up-count wraps every 65,536 cycles (~524 us at 125 MHz). All
// deadlines sit on one absolute grid of uint16_t sums, which are exact
// modulo 2^16, and every wait call is decoded by the signed difference to a
// target only one bit (or one sample) away — far inside the ±32,768-cycle
// decode window of half the wrap period. Telemetry µs still come from the
// 1 MHz timer in a separate 16-bit field (us_raw16).
inline uint16_t cycles_now16() {
    return (uint16_t)(systick_hw->cvr ^ 0xFFFFu);
}

// Sleep until the up-count passes `target`. `target` must be less than
// 32,768 cycles ahead of the current count, which every caller maintains by
// advancing its deadline incrementally.
inline void wait_until_cycles16(uint16_t target) {
    while ((int16_t)(cycles_now16() - target) < 0) {
        tight_loop_contents();
    }
}

// Telemetry only: the low half of the 1 MHz timer, in 16-bit µs.
inline uint16_t us_raw16() {
    return (uint16_t)time_us_32();
}

void bb_release_irq() {
    if (s_irq_held) {
        restore_interrupts(s_irq);
        s_irq_held = false;
    }
}

// The pin is an SIO output driving 1 at all times, except while the reply is
// sampled. That keeps the line idle-high without injecting any extra bit and
// removes the break-recovery wait between datagrams.
void uart_line_high() {
    gpio_put(board::kTmcUartPin, 1);
    gpio_set_dir(board::kTmcUartPin, GPIO_OUT);
}

void pin_init() {
    gpio_init(board::kTmcUartPin);
    gpio_set_function(board::kTmcUartPin, GPIO_FUNC_SIO);
    gpio_set_input_enabled(board::kTmcUartPin, true);
    gpio_pull_up(board::kTmcUartPin);
    uart_line_high();
}

// Build the datagram bit stream (start/data/stop per byte, no trailing idle).
size_t build_bits(const uint8_t* data, size_t len, uint8_t bits[8 * 10]) {
    size_t nbits = 0;
    for (size_t n = 0; n < len && n < 8; ++n) {
        bits[nbits++] = 0;  // start
        for (uint32_t i = 0; i < 8; ++i) {
            bits[nbits++] = (data[n] >> i) & 1u;
        }
        bits[nbits++] = 1;  // stop
    }
    return nbits;
}

// Transmit a datagram, hold the line high for s_idle_bits, then release it and
// sample the reply. Bit edges and the 1 us samples are scheduled on the CPU
// cycle counter, so edges are placed to cycle resolution instead of the 1 us
// system-timer tick. Interrupts stay masked through the hold and the capture;
// the line is driven high again as soon as the read is done. The pin is
// already an SIO output driving 1, so the first bit is the start bit.
void bb_tx_read(const uint8_t* data, size_t len) {
    uint8_t bits[8 * 10];
    const size_t nbits = build_bits(data, len, bits);

    // init the buffers outside IRQ disabling
    memset(s_samples, 0, sizeof(s_samples));
    memset(&s_capture, 0, sizeof(s_capture));

    uart_line_high();
    wait_until_cycles16(
        cycles_now16() + (uint16_t)(kPreIdleBits * s_bit_cycles));
    s_irq = save_and_disable_interrupts();
    s_irq_held = true;
    // One start instant, one absolute grid: bit k ends at
    // start + (k+1)*bit_cycles. uint16_t sums are exact modulo 2^16 and the
    // signed decode inside wait_until_cycles16 only ever looks one bit
    // ahead, so a datagram spanning more than one 65,536-cycle wrap still
    // sits on the same absolute grid. No tick alignment is needed: cycle
    // deadlines place edges without shortening the start bit.
    // One start instant, one absolute grid: bit k ends at
    // bit_start + k*s_bit_cycles. uint16_t sums are exact modulo 2^16 and
    // the signed decode inside wait_until_cycles16 only ever looks one bit
    // ahead, so a datagram spanning more than one 65,536-cycle wrap still
    // sits on the same absolute grid. No tick alignment is needed: cycle
    // deadlines place edges without shortening the start bit.
    uint16_t bit_start = cycles_now16();
    for (size_t k = 0; k < nbits; ++k) {
        gpio_put(board::kTmcUartPin, bits[k]);
        bit_start += s_bit_cycles;
        wait_until_cycles16(bit_start);
    }

    // The last datagram bit is the stop bit, already high. Hold it for
    // s_idle_bits so the master is off the line during the reply delay.
    gpio_put(board::kTmcUartPin, 1);
    bit_start += s_idle_bits * s_bit_cycles;
    wait_until_cycles16(bit_start);

    gpio_set_dir(board::kTmcUartPin, GPIO_IN);

    // The reply-end analysis lives in tmc_uart_bits so it stays host-testable.
    // Feed it one sample at a time; it returns true on the sample where the
    // stop bit is in and the line is still high, so we can take the line back
    // before the TMC releases it.
    tmc_uart_bits::CaptureTracker tracker(kBitPeriodQ8);
    // Telemetry stays on the 1 MHz timer: the capture can span more than
    // one 16-bit cycle window, but the µs difference is far inside its own
    // 65,536 µs half-window decode.
    const uint16_t t0_us = us_raw16();
    const uint16_t t0 = cycles_now16();
    for (uint32_t i = 0; i < kCaptureSamplesMax; ++i) {
        wait_until_cycles16(t0 + (uint16_t)(i * s_sample_cycles));
        const bool level = gpio_get(board::kTmcUartPin);
        if (level) {
            s_samples[i >> 3] |= (uint8_t)(1u << (i & 7u));
        }
        if (tracker.feed(level)) {
            gpio_put(board::kTmcUartPin, 1);
            gpio_set_dir(board::kTmcUartPin, GPIO_OUT);
            break;
        }
    }
    tracker.finish();
    s_capture = tracker.report();
    // One sample is exactly the 1 MHz tick, so expected µs = sample index.
    s_capture_expected_us = s_capture.end + 1u;
    s_capture_elapsed_us = (uint16_t)(us_raw16() - t0_us);

    // After the read, drive the line high again instead of leaving it released.
    gpio_put(board::kTmcUartPin, 1);
    gpio_set_dir(board::kTmcUartPin, GPIO_OUT);
    bb_release_irq();
}

// Transmit a datagram write-only. Enable interrupts before the s_idle_bits high
// hold (the stop bit already drives the line high), do not append those idle
// bits to the buffer, and keep the output driving high afterwards.
void bb_tx_only(const uint8_t* data, size_t len) {
    uint8_t bits[8 * 10];
    const size_t nbits = build_bits(data, len, bits);

    uart_line_high();
    wait_until_cycles16(
        cycles_now16() + (uint16_t)(kPreIdleBits * s_bit_cycles));
    s_irq = save_and_disable_interrupts();
    s_irq_held = true;
    // The same incremental deadline as bb_tx_read: edges are placed on
    // cycle deadlines, one bit at a time.
    uint16_t bit_start = cycles_now16();
    for (size_t k = 0; k < nbits; ++k) {
        gpio_put(board::kTmcUartPin, bits[k]);
        bit_start += s_bit_cycles;
        wait_until_cycles16(bit_start);
    }

    bb_release_irq();
    gpio_put(board::kTmcUartPin, 1);
    bit_start += s_idle_bits * s_bit_cycles;
    wait_until_cycles16(bit_start);
    // Leave the pin as an output driving high.
}

bool bb_decode(uint8_t reply[tmc_uart_bits::kReplyBytes],
               tmc_uart_bits::Frame* out = nullptr) {
    tmc_uart_bits::Frame frame;
    if (!tmc_uart_bits::decode_reply(s_samples, kCaptureSamplesMax,
                                     kBitPeriodQ8, &frame)) {
        return false;
    }
    memcpy(reply, frame.bytes, tmc_uart_bits::kReplyBytes);
    if (out != nullptr) {
        *out = frame;
    }
    return true;
}

void bb_dump_samples() {
    constexpr uint32_t kLine = 128;
    const uint32_t count =
        (s_capture.end + 1u < kCaptureSamplesMax) ? s_capture.end + 1u
                                                  : kCaptureSamplesMax;
    Serial.print("bbprobe: oversample ");
    Serial.print(kCaptureSamplesMax);
    Serial.print(" samples, ");
    Serial.print(kSamplesPerBit);
    Serial.print("/bit, sample ");
    Serial.print(s_sample_cycles);
    Serial.print(" cyc, recorded ");
    Serial.println(count);
    for (uint32_t start = 0; start < count; start += kLine) {
        const uint32_t stop = (start + kLine < count) ? start + kLine : count;
        Serial.print("bbprobe: os ");
        Serial.print(start);
        Serial.print(": ");
        for (uint32_t i = start; i < stop; ++i) {
            Serial.print(tmc_uart_bits::level(s_samples, i) ? '1' : '0');
        }
        Serial.println();
    }
}

uint8_t driverAddress(uint8_t index) {
    return board::kDrives[index].uart_address;
}

// Send a read request and decode the 8-byte reply. A decode miss is a lost
// frame (framing/CRC/echo), not a bad value, so retry the read a few times
// before reporting failure. Every attempt is counted per address.
bool readTransact(uint8_t address, uint8_t reg,
                  uint8_t reply[tmc_uart_bits::kReplyBytes]) {
    UartStats& stats = statsFor(address);
    uint8_t request[tmc_uart_bits::kReplyBytes];
    const uint32_t len = tmc_uart_bits::encode(address, reg, 0, false, request);
    for (uint32_t attempt = 0; attempt < 3; ++attempt) {
        stats.read_attempts++;
        bb_tx_read(request, len);
        if (bb_decode(reply) && reply[2] == reg) {
            stats.reads++;
            return true;
        }
    }
    stats.read_failures++;
    return false;
}

bool readAddressReg(uint8_t address, uint8_t reg, uint32_t* value) {
    uint8_t reply[tmc_uart_bits::kReplyBytes];
    if (!readTransact(address, reg, reply)) {
        return false;
    }
    *value = ((uint32_t)reply[3] << 24) |
             ((uint32_t)reply[4] << 16) |
             ((uint32_t)reply[5] << 8) | reply[6];
    return true;
}

bool writeAddressReg(uint8_t address, uint8_t reg, uint32_t value) {
    UartStats& stats = statsFor(address);
    stats.writes++;
    uint8_t request[tmc_uart_bits::kReplyBytes];
    const uint32_t len =
        tmc_uart_bits::encode(address, reg, value, true, request);
    bb_tx_only(request, len);
    return true;
}

// A write has no reply, so a lost frame is invisible. IFCNT is the driver's
// count of accepted writes: read it before and after, and repeat the write
// when the count did not advance by one. The surrounding reads also give the
// driver a clean gap between datagrams.
bool writeAddressRegVerified(uint8_t address, uint8_t reg, uint32_t value) {
    UartStats& stats = statsFor(address);
    constexpr uint8_t kAttempts = 3;
    for (uint8_t attempt = 0; attempt < kAttempts; ++attempt) {
        uint32_t before = 0;
        const bool have_before = readAddressReg(address, kIfcnt, &before);
        writeAddressReg(address, reg, value);
        uint32_t after = 0;
        if (have_before && readAddressReg(address, kIfcnt, &after) &&
            (uint8_t)(after - before) == 1u) {
            stats.write_verified++;
            stats.writes_since_check++;
            return true;
        }
    }
    stats.write_failures++;
    return false;
}

bool readDriverReg(uint8_t index, uint8_t reg, uint32_t* value) {
    return index < kDriverCount &&
           readAddressReg(driverAddress(index), reg, value);
}

bool writeDriverReg(uint8_t index, uint8_t reg, uint32_t value) {
    return index < kDriverCount &&
           writeAddressRegVerified(driverAddress(index), reg, value);
}

// SLAVECONF sets the reply delay, so the first write cannot be verified with
// a read yet.
bool writeDriverRegRaw(uint8_t index, uint8_t reg, uint32_t value) {
    return index < kDriverCount &&
           writeAddressReg(driverAddress(index), reg, value);
}

// I_rms = (CS+1)/32 * V_fs/(R_sense+0.02) / sqrt(2), with R_sense = 0.110.
// CS+1 = 588313 * mA / (1e8 * V_fs), V_fs = 0.325 V or 0.180 V.
uint32_t csFor(uint32_t ma, bool vsense) {
    const uint64_t numerator = 588313ull * ma;
    const uint32_t denominator = vsense ? 18000000u : 32500000u;
    const uint32_t cs_plus_one = (uint32_t)(numerator / denominator);
    return cs_plus_one == 0 ? 0 : cs_plus_one - 1;
}

// Pick the V_fs range the way the reference driver does: if the low-current
// range is selected, switch to high sensitivity and recompute.
void currentScale(uint16_t run_ma, bool* vsense, uint32_t* irun) {
    uint32_t cs = csFor(run_ma, false);
    bool high = false;
    if (cs < 16) {
        high = true;
        cs = csFor(run_ma, true);
    }
    if (cs > 31) {
        cs = 31;
    }
    *vsense = high;
    *irun = cs;
}

uint32_t iholdFor(uint32_t irun, uint16_t run_ma, uint16_t hold_ma) {
    if (run_ma == 0) {
        return 0;
    }
    uint32_t ihold = (irun * hold_ma) / run_ma;
    return ihold > 31 ? 31 : ihold;
}

uint32_t chopconfValue(bool vsense) {
    uint32_t value = kChopconfBase;
    value = (value & ~0xFu) | kChopconfToff;
    value = (value & ~(0xFu << 24)) | (kChopconfMres << 24);
    value |= kChopconfIntpol << 28;
    value = (value & ~(1u << 17)) | (vsense ? (1u << 17) : 0);
    return value;
}

uint8_t mresFor(uint16_t microsteps) {
    switch (microsteps) {
        case 256: return 0;
        case 128: return 1;
        case 64:  return 2;
        case 32:  return 3;
        case 16:  return 4;
        case 8:   return 5;
        case 4:   return 6;
        case 2:   return 7;
        default:  return 0xFF;
    }
}

Config s_config[kDriverCount];
DriverStatus s_status[kDriverCount];
uint8_t s_ready_mask = 0;
uint8_t s_seen_mask = 0;
uint8_t s_last_diag = 0;
bool s_diag_valid = false;

void initPrefix(uint8_t index) {
    Serial.print("tmc");
    Serial.print(index);
    Serial.print("@");
    Serial.print(driverAddress(index));
    Serial.print(": init ");
}

bool configureDriver(uint8_t index) {
    // SENDDELAY first, so every reply already has the clean gap after our
    // last stop bit. It is write-only; the log is the record.
    uart_line_high();
    const uint32_t slaveconf = (uint32_t)kDefaultSendDelay << 8;
    writeDriverRegRaw(index, kSlaveconf, slaveconf);
    uart_line_high();
    initPrefix(index);
    Serial.print("SENDDELAY=");
    Serial.print(kDefaultSendDelay);
    Serial.print(" SLAVECONF=0x");
    Serial.print(slaveconf, HEX);
    Serial.println(" (UART line high)");

    // The GCONF reset default is not trusted; write every bit we care about
    // explicitly. Each write is verified through IFCNT, so a lost frame
    // disables the axis instead of silently keeping the reset default.
    // bit 0 = 0 I_scale_analog, bit 1 = 0 external sense resistors,
    // bit 2 = 1 en_spreadcycle (motion; homing switches to StealthChop),
    // bit 6 = 1 pdn_disable, bit 7 = 1 mstep_reg_select,
    // bit 8 = 1 multistep_filt.
    const uint32_t gconf = initGconfValue();
    bool ok = true;
    ok = writeDriverReg(index, kGconf, gconf) && ok;
    s_status[index].gconf = gconf;
    initPrefix(index);
    Serial.print("GCONF=0x");
    Serial.print(gconf, HEX);
    Serial.print(" external sense, pdn_disable=1, mstep_reg_select=1, ");
    Serial.print(kMotionSpreadCycle ? "SpreadCycle" : "StealthChop");
    Serial.println(" written");

    bool vsense = false;
    uint32_t irun = 0;
    currentScale(kDefaultRunMa, &vsense, &irun);
    const uint32_t ihold = iholdFor(irun, kDefaultRunMa, kDefaultHoldMa);
    const uint32_t ihold_irun =
        (ihold & 0x1Fu) | ((irun & 0x1Fu) << 8) | (1u << 16);
    const uint32_t chopconf = chopconfValue(vsense);

    ok = writeDriverReg(index, kChopconf, chopconf) && ok;
    ok = writeDriverReg(index, kIholdIrun, ihold_irun) && ok;
    ok = writeDriverReg(index, kPwmconf, kDefaultPwmconf) && ok;
    ok = writeDriverReg(index, kTpowerdown, kDefaultTpowerdown) && ok;
    ok = writeDriverReg(index, kTpwmthrs, 0) && ok;
    ok = writeDriverReg(index, kTcoolthrs, kDefaultTcoolthrs) && ok;
    ok = writeDriverReg(index, kSgthrs, kDefaultSgthrs) && ok;

    initPrefix(index);
    Serial.print("CHOPCONF=0x");
    Serial.print(chopconf, HEX);
    Serial.print(" VSENSE=");
    Serial.print(vsense ? 1 : 0);
    Serial.print(" TOFF=");
    Serial.print(kChopconfToff);
    Serial.print(" MRES=");
    Serial.print(1u << kChopconfMres);
    Serial.println(" microsteps");

    initPrefix(index);
    Serial.print("IHOLD_IRUN=0x");
    Serial.print(ihold_irun, HEX);
    Serial.print(" run=");
    Serial.print(kDefaultRunMa);
    Serial.print("mA hold=");
    Serial.print(kDefaultHoldMa);
    Serial.print("mA IRUN=");
    Serial.print(irun);
    Serial.print(" IHOLD=");
    Serial.println(ihold);

    initPrefix(index);
    Serial.print("PWMCONF=0x");
    Serial.println(kDefaultPwmconf, HEX);

    initPrefix(index);
    Serial.print("TPOWERDOWN=");
    Serial.println(kDefaultTpowerdown);

    initPrefix(index);
    Serial.println("TPWMTHRS=0");

    initPrefix(index);
    Serial.print("TCOOLTHRS=0x");
    Serial.println(kDefaultTcoolthrs, HEX);

    initPrefix(index);
    Serial.print("SGTHRS=");
    Serial.println(kDefaultSgthrs);

    // DRV_STATUS is telemetry, not part of the enable safety check. A missing
    // motor supply can make this read fail; configuration is still valid and
    // the DIAG path retries later. valid_mask bit 3 stays clear until then.
    uint32_t drv_status = 0;
    if (readDriverReg(index, kDrvStatus, &drv_status)) {
        s_status[index].drv_status = drv_status;
        s_seen_mask |= (uint8_t)(1u << index);
        initPrefix(index);
        Serial.print("DRV_STATUS=0x");
        Serial.println(drv_status, HEX);
    } else {
        Serial.print("tmc");
        Serial.print(index);
        Serial.print("@");
        Serial.print(driverAddress(index));
        Serial.println(": DRV_STATUS read failed; telemetry bit stays clear");
    }

    s_status[index].vsense = vsense;
    s_status[index].irun = (uint8_t)irun;
    s_status[index].ihold = (uint8_t)ihold;
    s_config[index].sgthrs = kDefaultSgthrs;
    s_config[index].tcoolthrs = kDefaultTcoolthrs;
    s_status[index].sgthrs = kDefaultSgthrs;
    s_status[index].tcoolthrs = kDefaultTcoolthrs;
    s_status[index].ready = ok;
    s_status[index].online = true;
    uart_line_high();
    initPrefix(index);
    if (ok) {
        Serial.println("complete, UART line high");
    } else {
        Serial.println("write verification failed; axis stays disabled");
    }
    return ok;
}

// Drive one byte through SIO and sample the pad at every bit centre. This
// proves the pin function, output-enable and pad path, not the external wiring.
bool tx_pad_test(uint8_t value) {
    gpio_set_function(board::kTmcUartPin, GPIO_FUNC_SIO);
    gpio_put(board::kTmcUartPin, 1);
    gpio_set_dir(board::kTmcUartPin, GPIO_IN);

    const uint32_t save = save_and_disable_interrupts();
    gpio_put(board::kTmcUartPin, 0);
    gpio_set_dir(board::kTmcUartPin, GPIO_OUT);
    const uint16_t t0 = cycles_now16();
    bool ok = true;
    for (uint32_t bit = 0; bit < 10; ++bit) {
        wait_until_cycles16(
            t0 + (uint16_t)((2u * bit + 1u) * s_bit_cycles / 2u));
        const int expected = (bit == 0) ? 0
                             : (bit == 9) ? 1
                                          : ((value >> (bit - 1u)) & 1u);
        if ((gpio_get(board::kTmcUartPin) ? 1 : 0) != expected) {
            ok = false;
        }
        if (bit < 9) {
            wait_until_cycles16(
                t0 + (uint16_t)((bit + 1u) * s_bit_cycles));
            const int next =
                (bit + 1u == 9u) ? 1 : ((value >> bit) & 1u);
            gpio_put(board::kTmcUartPin, next);
        }
    }
    wait_until_cycles16(t0 + (uint16_t)(10u * s_bit_cycles));
    gpio_put(board::kTmcUartPin, 1);
    gpio_set_dir(board::kTmcUartPin, GPIO_OUT);
    restore_interrupts(save);
    return ok;
}

// DIAG is active high and StallGuard only pulses it, so the level poll can
// miss a stall. Latch the asserting edge in the GPIO IRQ and let the main loop
// do the UART read; no register access happens in interrupt context.
volatile uint8_t s_diag_events = 0;
bool s_diag_irq_ready = false;

void diagIrq(uint gpio, uint32_t) {
    for (uint8_t i = 0; i < kDriverCount; ++i) {
        if (board::kDrives[i].diag == gpio) {
            s_diag_events |= (uint8_t)(1u << i);
            return;
        }
    }
}

void attachDiagInterrupts() {
    if (s_diag_irq_ready) {
        return;
    }
    for (uint8_t i = 0; i < kDriverCount; ++i) {
        gpio_set_irq_enabled_with_callback(board::kDrives[i].diag,
                                           GPIO_IRQ_EDGE_RISE, true, &diagIrq);
    }
    s_diag_irq_ready = true;
}

}  // namespace

bool setIdleBits(uint32_t bits) {
    if (bits > 12u) {
        return false;
    }
    s_idle_bits = bits;
    return true;
}

uint32_t idleBits() {
    return s_idle_bits;
}

uint8_t begin() {
    // One-time sample-grid setup: one sample is the 1 MHz tick in clk_sys
    // cycles (exact at an integer-MHz clock), one bit is 4 samples. All
    // later deadlines are integers of this grid. No bit-time override: the
    // old bench sweep of 2..10 us is gone because the decoder expects
    // exactly 4 samples per bit.
    s_sample_cycles = clock_get_hz(clk_sys) / 1000000u;
    s_bit_cycles = (uint16_t)(kSamplesPerBit * s_sample_cycles);

    // All outputs off before the first UART access.
    stepper_axes::setEnabled(false);
    pin_init();
    attachDiagInterrupts();

    s_ready_mask = 0;
    s_seen_mask = 0;
    s_last_diag = 0;
    s_diag_valid = false;

    Serial.print("tmc: init GPIO");
    Serial.print(board::kTmcUartPin);
    Serial.print(" SIO output high, sample ");
    Serial.print(s_sample_cycles);
    Serial.print(" cyc (");
    Serial.print(kSamplesPerBit);
    Serial.print("/bit = ");
    Serial.print(clock_get_hz(clk_sys) / s_bit_cycles);
    Serial.print(" Baud, ");
    Serial.print(kDriverCount);
    Serial.println(" drivers");

    for (uint8_t i = 0; i < kDriverCount; ++i) {
        s_config[i] = Config{kDefaultRunMa, kDefaultHoldMa, kDefaultSgthrs,
                             kDefaultTcoolthrs};
        s_status[i] = DriverStatus{};

        if (configureDriver(i)) {
            s_ready_mask |= (uint8_t)(1u << i);
            Serial.print("tmc");
            Serial.print(i);
            Serial.print("@");
            Serial.print(driverAddress(i));
            Serial.println(": ready");
        } else {
            Serial.print("tmc");
            Serial.print(i);
            Serial.print("@");
            Serial.print(driverAddress(i));
            Serial.println(": not configured, outputs stay disabled");
        }
    }
    Serial.print("tmc: init done ready=0x");
    Serial.print(s_ready_mask, HEX);
    Serial.print("/0x");
    Serial.println((1u << kDriverCount) - 1u, HEX);
    return s_ready_mask;
}

bool ready(uint8_t index) {
    return index < kDriverCount && s_status[index].ready;
}

const Config& config(uint8_t index) {
    static const Config empty = {};
    return index < kDriverCount ? s_config[index] : empty;
}

const DriverStatus& status(uint8_t index) {
    static const DriverStatus empty = {};
    return index < kDriverCount ? s_status[index] : empty;
}

const UartStats& uartStats(uint8_t index) {
    static const UartStats empty = {};
    return index < kDriverCount ? statsFor(driverAddress(index)) : empty;
}

bool checkIfcnt(uint8_t index) {
    if (index >= kDriverCount) {
        return false;
    }
    const uint8_t address = driverAddress(index);
    UartStats& stats = statsFor(address);
    uint32_t value = 0;
    if (!readAddressReg(address, kIfcnt, &value)) {
        return false;
    }
    const uint8_t now = (uint8_t)value;
    stats.ifcnt_checks++;
    if (!stats.ifcnt_valid) {
        stats.ifcnt_valid = true;
        stats.ifcnt_last = now;
        stats.writes_since_check = 0;
        return true;
    }
    // Only a smaller advance means a write we verified did not reach the
    // driver. A larger advance is a retried frame that landed twice; the
    // register already holds the value, so it is not a lost configuration.
    const uint8_t delta = (uint8_t)(now - stats.ifcnt_last);
    if (delta < stats.writes_since_check) {
        stats.ifcnt_mismatches++;
    }
    stats.ifcnt_last = now;
    stats.writes_since_check = 0;
    return true;
}

void printStats(uint8_t index) {
    if (index >= kDriverCount) {
        return;
    }
    const UartStats& s = uartStats(index);
    Serial.print("tmc");
    Serial.print(index);
    Serial.print("@");
    Serial.print(driverAddress(index));
    Serial.print(": ready=");
    Serial.print(ready(index) ? 1 : 0);
    Serial.print(" rd_ok=");
    Serial.print(s.reads);
    Serial.print(" rd_att=");
    Serial.print(s.read_attempts);
    Serial.print(" rd_fail=");
    Serial.print(s.read_failures);
    Serial.print(" wr=");
    Serial.print(s.writes);
    Serial.print(" wr_ok=");
    Serial.print(s.write_verified);
    Serial.print(" wr_fail=");
    Serial.print(s.write_failures);
    Serial.print(" ifcnt=");
    if (s.ifcnt_valid) {
        Serial.print("0x");
        Serial.print(s.ifcnt_last, HEX);
    } else {
        Serial.print("--");
    }
    Serial.print(" checks=");
    Serial.print(s.ifcnt_checks);
    Serial.print(" mism=");
    Serial.print(s.ifcnt_mismatches);
    Serial.print(" pending=");
    Serial.println(s.writes_since_check);
}

void printStats() {
    for (uint8_t i = 0; i < kDriverCount; ++i) {
        printStats(i);
    }
}

void clearDiagEvents() {
    const uint32_t save = save_and_disable_interrupts();
    s_diag_events = 0;
    restore_interrupts(save);
    for (uint8_t i = 0; i < kDriverCount; ++i) {
        s_status[i].diag_event = false;
    }
}

bool setCurrents(uint8_t index, uint16_t run_ma, uint16_t hold_ma) {
    if (!ready(index) || run_ma == 0) {
        return false;
    }
    if (run_ma > kMaxRunMa) {
        run_ma = kMaxRunMa;
    }
    if (hold_ma > run_ma) {
        hold_ma = run_ma;
    }

    bool vsense = false;
    uint32_t irun = 0;
    currentScale(run_ma, &vsense, &irun);
    const uint32_t ihold = iholdFor(irun, run_ma, hold_ma);
    const uint32_t ihold_irun =
        (ihold & 0x1Fu) | ((irun & 0x1Fu) << 8) | (1u << 16);

    uint32_t chopconf = 0;
    if (!readDriverReg(index, kChopconf, &chopconf)) {
        return false;
    }
    chopconf = (chopconf & ~(1u << 17)) | (vsense ? (1u << 17) : 0);
    if (!writeDriverReg(index, kChopconf, chopconf) ||
        !writeDriverReg(index, kIholdIrun, ihold_irun)) {
        return false;
    }

    s_config[index].run_ma = run_ma;
    s_config[index].hold_ma = hold_ma;
    s_status[index].irun = (uint8_t)irun;
    s_status[index].ihold = (uint8_t)ihold;
    s_status[index].vsense = vsense;
    return true;
}

bool setStallGuard(uint8_t index, uint8_t sgthrs) {
    if (!ready(index)) {
        return false;
    }
    if (!writeDriverReg(index, kSgthrs, sgthrs)) {
        return false;
    }
    s_config[index].sgthrs = sgthrs;
    s_status[index].sgthrs = sgthrs;
    return true;
}

bool setTcoolthrs(uint8_t index, uint32_t value) {
    if (!ready(index)) {
        return false;
    }
    value &= 0xFFFFFu;
    if (!writeDriverReg(index, kTcoolthrs, value)) {
        return false;
    }
    s_config[index].tcoolthrs = value;
    s_status[index].tcoolthrs = value;
    return true;
}

bool setMicrosteps(uint8_t index, uint16_t microsteps, bool interpolate) {
    if (!ready(index)) {
        return false;
    }
    const uint8_t mres = mresFor(microsteps);
    if (mres == 0xFF) {
        return false;
    }
    uint32_t chopconf = 0;
    if (!readDriverReg(index, kChopconf, &chopconf)) {
        return false;
    }
    chopconf = (chopconf & ~(0xFu << 24)) | ((uint32_t)mres << 24);
    chopconf = (chopconf & ~(1u << 28)) |
               ((interpolate ? 1u : 0u) << 28);
    return writeDriverReg(index, kChopconf, chopconf);
}

bool setChopper(uint8_t index, uint32_t chopconf) {
    if (!ready(index)) {
        return false;
    }
    // VSENSE belongs to the current setting; keep the raw experiment from
    // changing the current scale behind setCurrents().
    chopconf = (chopconf & ~(1u << 17)) |
               (s_status[index].vsense ? (1u << 17) : 0);
    return writeDriverReg(index, kChopconf, chopconf);
}

bool setPwmConf(uint8_t index, uint32_t pwmconf) {
    if (!ready(index)) {
        return false;
    }
    return writeDriverReg(index, kPwmconf, pwmconf);
}

bool setSpreadCycle(uint8_t index, bool enable) {
    if (!ready(index)) {
        return false;
    }
    const uint32_t gconf = kInitGconf | (enable ? (1u << 2) : 0u);
    if (!writeDriverReg(index, kGconf, gconf)) {
        return false;
    }
    s_status[index].gconf = gconf;
    return true;
}

bool setSendDelay(uint8_t index, uint8_t delay) {
    if (!ready(index) || delay > 15) {
        return false;
    }
    const uint32_t value = (uint32_t)delay << 8;
    return writeDriverReg(index, kSlaveconf, value);
}

bool readRegister(uint8_t index, uint8_t address, uint32_t* value) {
    return readDriverReg(index, address, value);
}

bool writeRegister(uint8_t index, uint8_t address, uint32_t value) {
    if (index >= kDriverCount) {
        return false;
    }
    // OTP is irreversible; VACTUAL would move the motor outside the step
    // generator; IHOLD_IRUN bypasses the current cap in setCurrents().
    if (address == kOtpProg || address == kVactual ||
        address == kIholdIrun) {
        return false;
    }
    if (address == kGconf) {
        // Keep external current sensing, the digital current scale and UART
        // control enabled.
        if ((value & ((1u << 0) | (1u << 1))) != 0) {
            return false;
        }
        if ((value & (1u << 6)) == 0) {
            return false;
        }
        // Microstep resolution must stay under register control; MS1/MS2 are
        // the UART address straps on this board.
        if ((value & (1u << 7)) == 0) {
            return false;
        }
    }
    if (address == kChopconf) {
        // VSENSE belongs to the current setting.
        value = (value & ~(1u << 17)) |
                (s_status[index].vsense ? (1u << 17) : 0);
    }
    return writeDriverReg(index, address, value);
}

void updateMotionFlags(uint8_t index);

// Bench monitor. Off by default: a UART read blocks the CPU for several ms,
// so this must never run implicitly on the motion loop. The DIAG path below
// stays the production event path.
uint8_t s_monitor_index = kDriverCount;
uint32_t s_monitor_until_ms = 0;
uint32_t s_monitor_next_ms = 0;
uint32_t s_monitor_interval_ms = 100;

void printMonitorLine(uint8_t index, uint32_t now_ms) {
    const DriverStatus& s = s_status[index];
    Serial.print("mon tmc");
    Serial.print(index);
    Serial.print("@");
    Serial.print(driverAddress(index));
    Serial.print(" t=");
    Serial.print(now_ms);
    Serial.print(" sg=");
    Serial.print(s.sg_result);
    Serial.print("/");
    Serial.print((uint16_t)(2u * s.sgthrs));
    Serial.print(" cs=");
    Serial.print(s.cs_actual);
    Serial.print("/");
    Serial.print(s.irun);
    Serial.print(" pwm=");
    Serial.print(s.pwm_scale_sum);
    Serial.print("/");
    Serial.print(s.pwm_scale_auto);
    Serial.print(" auto=");
    Serial.print(s.pwm_ofs_auto);
    Serial.print("/");
    Serial.print(s.pwm_grad_auto);
    Serial.print(" tstep=");
    Serial.print(s.tstep);
    if (s.stall) {
        Serial.print(" STALL");
    }
    if (s.current_limited) {
        Serial.print(" CURRENT_LIMITED");
    }
    Serial.println();
}

bool startMonitor(uint8_t index, uint32_t duration_ms, uint32_t interval_ms) {
    if (index >= kDriverCount) {
        return false;
    }
    if (duration_ms == 0) {
        s_monitor_index = kDriverCount;
        Serial.println("monitor: stopped");
        return true;
    }
    if (!s_status[index].ready) {
        return false;
    }
    if (interval_ms < 20u) {
        interval_ms = 20u;
    } else if (interval_ms > 2000u) {
        interval_ms = 2000u;
    }
    s_monitor_index = index;
    s_monitor_interval_ms = interval_ms;
    s_monitor_until_ms = millis() + duration_ms;
    s_monitor_next_ms = millis();
    Serial.print("monitor: tmc");
    Serial.print(index);
    Serial.print(" for ");
    Serial.print(duration_ms);
    Serial.print(" ms every ");
    Serial.print(interval_ms);
    Serial.println(" ms");
    return true;
}

void poll(uint32_t now_ms) {
    if (s_monitor_index < kDriverCount) {
        if ((int32_t)(now_ms - s_monitor_until_ms) >= 0) {
            s_monitor_index = kDriverCount;
            Serial.println("monitor: done");
        } else if ((int32_t)(now_ms - s_monitor_next_ms) >= 0) {
            s_monitor_next_ms = now_ms + s_monitor_interval_ms;
            refresh(s_monitor_index);
            printMonitorLine(s_monitor_index, now_ms);
        }
    }

    if (s_ready_mask == 0) {
        return;
    }

    // One IFCNT comparison per second, round-robin over the ready drivers.
    if ((int32_t)(now_ms - s_ifcnt_check_ms) >= 0) {
        s_ifcnt_check_ms = now_ms + 1000;
        for (uint8_t tries = 0; tries < kDriverCount; ++tries) {
            const uint8_t i = s_ifcnt_check_index;
            s_ifcnt_check_index =
                (uint8_t)((s_ifcnt_check_index + 1u) % kDriverCount);
            if (s_ready_mask & (uint8_t)(1u << i)) {
                checkIfcnt(i);
                break;
            }
        }
    }

    // Take the latched edges from the GPIO ISR. A short StallGuard pulse is
    // gone before this loop runs, so it must be remembered in the ISR.
    uint32_t save = save_and_disable_interrupts();
    const uint8_t events = s_diag_events;
    s_diag_events = 0;
    restore_interrupts(save);

    // DIAG is the event path; UART stays off the motion loop. A changed DIAG
    // pin (stall, pre-warning, short, open load) triggers one DRV_STATUS and
    // SG_RESULT read, which the 1 Hz health frame and the status flags use.
    uint8_t diag = 0;
    for (uint8_t i = 0; i < kDriverCount; ++i) {
        if (digitalRead(board::kDrives[i].diag)) {
            diag |= (uint8_t)(1u << i);
        }
    }
    if (!s_diag_valid) {
        s_diag_valid = true;
        s_last_diag = diag;
        return;
    }
    const uint8_t changed = diag ^ s_last_diag;
    s_last_diag = diag;
    const uint8_t pending = (uint8_t)(changed | events);
    if (pending == 0) {
        return;
    }

    for (uint8_t i = 0; i < kDriverCount; ++i) {
        const uint8_t bit = (uint8_t)(1u << i);
        if ((pending & bit) == 0 || (s_ready_mask & bit) == 0) {
            continue;
        }
        const bool edge = (events & bit) != 0;
        if (edge) {
            s_status[i].diag_event = true;
        }
        uint32_t drv_status = 0;
        const bool ok = readDriverReg(i, kDrvStatus, &drv_status);
        s_status[i].online = ok;
        if (ok) {
            s_status[i].drv_status = drv_status;
            s_status[i].cs_actual = (uint8_t)((drv_status >> 16) & 0x1Fu);
            s_seen_mask |= bit;
        }
        uint32_t sg_result = 0;
        if (readDriverReg(i, kSgResult, &sg_result)) {
            s_status[i].sg_result = (uint16_t)sg_result;
            s_seen_mask |= bit;
        }
        if (!ok) {
            continue;
        }
        updateMotionFlags(i);
        const bool moving = (s_status[i].drv_status & (1u << 31)) == 0;
        const bool fault = (s_status[i].drv_status & 0xFu) != 0;
        if (edge && (moving || fault)) {
            Serial.print("tmc");
            Serial.print(i);
            Serial.print("@");
            Serial.print(driverAddress(i));
            Serial.print(": DIAG");
            if (fault) {
                Serial.print(" fault=0x");
                Serial.print(s_status[i].drv_status & 0xFu, HEX);
            }
            if (s_status[i].stall) {
                Serial.print(" STALL");
            }
            Serial.print(" sg=");
            Serial.print(s_status[i].sg_result);
            Serial.print("/");
            Serial.print((uint16_t)(2u * s_status[i].sgthrs));
            Serial.print(" cs=");
            Serial.print(s_status[i].cs_actual);
            Serial.print("/");
            Serial.print(s_status[i].irun);
            Serial.print(" pwm=");
            Serial.println(s_status[i].pwm_scale_sum);
        }
    }
}

bool stallArmed(uint8_t index) {
    if (index >= kDriverCount) {
        return false;
    }
    const DriverStatus& s = s_status[index];
    const bool moving = (s.drv_status & (1u << 31)) == 0;
    const bool stealth = (s.drv_status & (1u << 30)) != 0;
    // Measured 2026-11-10: SGTHRS = 0 is not "off"; it sets the threshold to
    // 0 and the DIAG pin still pulses when SG_RESULT reaches 0. Require a
    // nonzero threshold and the TCOOLTHRS velocity window (TPWMTHRS is 0, so
    // the window is TSTEP <= TCOOLTHRS).
    return moving && stealth && s.sgthrs != 0 && s.tstep <= s.tcoolthrs;
}

void updateMotionFlags(uint8_t index) {
    DriverStatus& s = s_status[index];
    const bool moving = (s.drv_status & (1u << 31)) == 0;
    const uint16_t threshold = (uint16_t)(2u * s.sgthrs);
    s.stall = stallArmed(index) && s.sg_result <= threshold;
    // CS_ACTUAL follows IHOLD/IRUN. A lower value while moving means the
    // driver is not delivering the set current; PWM_SCALE at 250+ means the
    // voltage-mode regulator has run out of headroom. Neither is a stall by
    // itself, but both explain lost torque at speed.
    s.current_limited =
        moving && ((s.cs_actual < s.irun) || (s.pwm_scale_sum >= 250));
}

void refresh(uint8_t index) {
    if (index >= kDriverCount) {
        return;
    }
    // Read registers independently: with the motor supply off, DRV_STATUS may
    // fail while the digital configuration registers still read back.
    uint32_t value = 0;
    bool any_ok = false;
    bool drv_ok = false;

    if (readDriverReg(index, kDrvStatus, &value)) {
        s_status[index].drv_status = value;
        s_status[index].cs_actual = (uint8_t)((value >> 16) & 0x1Fu);
        s_seen_mask |= (uint8_t)(1u << index);
        any_ok = true;
        drv_ok = true;
    }
    if (readDriverReg(index, kSgResult, &value)) {
        s_status[index].sg_result = (uint16_t)value;
        any_ok = true;
    }
    if (readDriverReg(index, kTstep, &value)) {
        s_status[index].tstep = value & 0xFFFFFu;
        any_ok = true;
    }
    // IHOLD_IRUN, SGTHRS and TCOOLTHRS are write-only on the TMC2209; their
    // values come from what setCurrents()/setStallGuard()/setTcoolthrs()
    // wrote. Only CHOPCONF (VSENSE) is readable here.
    if (readDriverReg(index, kChopconf, &value)) {
        s_status[index].vsense = ((value >> 17) & 1u) != 0;
        any_ok = true;
    }
    if (readDriverReg(index, kPwmScale, &value)) {
        s_status[index].pwm_scale_sum = (uint8_t)(value & 0xFFu);
        int32_t scale_auto = (int32_t)((value >> 16) & 0x1FFu);
        if (scale_auto & 0x100) {
            scale_auto -= 0x200;
        }
        s_status[index].pwm_scale_auto = (int16_t)scale_auto;
        any_ok = true;
    }
    if (readDriverReg(index, kPwmAuto, &value)) {
        s_status[index].pwm_ofs_auto = (uint8_t)(value & 0xFFu);
        s_status[index].pwm_grad_auto = (uint8_t)((value >> 16) & 0xFFu);
        any_ok = true;
    }
    s_status[index].online = any_ok;
    if (drv_ok) {
        updateMotionFlags(index);
    }
}

uint8_t otpwMask() {
    uint8_t mask = 0;
    for (uint8_t i = 0; i < kDriverCount; ++i) {
        if ((s_ready_mask & (uint8_t)(1u << i)) &&
            (s_status[i].drv_status & (1u << 0))) {
            mask |= (uint8_t)(1u << i);
        }
    }
    return mask;
}

uint8_t otMask() {
    uint8_t mask = 0;
    for (uint8_t i = 0; i < kDriverCount; ++i) {
        if ((s_ready_mask & (uint8_t)(1u << i)) &&
            (s_status[i].drv_status & (1u << 1))) {
            mask |= (uint8_t)(1u << i);
        }
    }
    return mask;
}

bool statusValid() {
    return s_ready_mask != 0 && (s_seen_mask & s_ready_mask) == s_ready_mask;
}

void selftest() {
    static uint8_t s_seq = 0;
    const uint8_t pattern = (uint8_t)(0xA5u ^ (uint8_t)(s_seq++ * 0x5Bu));

    const bool tx_ok = tx_pad_test(pattern);
    Serial.print("selftest: tx pad readback ");
    Serial.println(tx_ok ? "ok" : "FAILED");
    Serial.print("selftest: pin func=");
    Serial.print((int)gpio_get_function(board::kTmcUartPin));
    Serial.print(" dir=");
    Serial.print(gpio_get_dir(board::kTmcUartPin) ? "out" : "in");
    Serial.print(" pue=");
    Serial.print(gpio_is_pulled_up(board::kTmcUartPin) ? 1 : 0);
    Serial.print(" pde=");
    Serial.print(gpio_is_pulled_down(board::kTmcUartPin) ? 1 : 0);
    Serial.print(" idle=");
    Serial.println(gpio_get(board::kTmcUartPin) ? 1 : 0);
    if (tx_ok) {
        Serial.println("selftest: SIO TX path drives GPIO9; a remaining "
                       "reply failure is external (TMC/power/PDN)");
    }
}

void scanAddresses() {
    Serial.println("scan: GCONF read on addresses 0..3");
    for (uint8_t address = 0; address < 4; ++address) {
        uint32_t value = 0;
        Serial.print("  addr ");
        Serial.print(address);
        if (!readAddressReg(address, kGconf, &value)) {
            Serial.println(": no reply");
        } else {
            Serial.print(": GCONF=0x");
            Serial.println(value, HEX);
        }
    }
}

void bbProbe(uint8_t address) {
    uint8_t request[tmc_uart_bits::kReplyBytes];
    const uint32_t len =
        tmc_uart_bits::encode(address, kGconf, 0, false, request);

    Serial.print("bbprobe addr ");
    Serial.print(address);
    Serial.print(" tx");
    for (uint32_t i = 0; i < len; ++i) {
        Serial.print(" 0x");
        if (request[i] < 0x10) {
            Serial.print('0');
        }
        Serial.print(request[i], HEX);
    }
    Serial.println();

    bb_tx_read(request, len);
    bb_dump_samples();

    if (s_capture.start != 0 || s_capture.switched) {
        const uint32_t period_x100 = (s_capture.period_q8 * 100u) / 256u;
        Serial.print("bbprobe: end start=");
        Serial.print(s_capture.start);
        Serial.print(" end=");
        Serial.print(s_capture.end);
        Serial.print(" period=");
        Serial.print(period_x100 / 100u);
        Serial.print('.');
        if (period_x100 % 100u < 10u) {
            Serial.print('0');
        }
        Serial.print(period_x100 % 100u);
        Serial.print("us high_run=");
        Serial.print(s_capture.ones_run);
        Serial.print(" low_run=");
        Serial.print(s_capture.zeros_run);
        Serial.print(" ones=");
        Serial.print(s_capture.total_ones);
        Serial.print(" zeros=");
        Serial.print(s_capture.total_zeros);
        Serial.print(" switched=");
        Serial.println(s_capture.switched ? 1 : 0);
        Serial.print("bbprobe: capture ");
        Serial.print(s_capture_elapsed_us);
        Serial.print("us expected ");
        Serial.print(s_capture_expected_us);
        Serial.print("us delta ");
        Serial.println((int32_t)(s_capture_elapsed_us -
                                 s_capture_expected_us));
    }

    uint8_t reply[tmc_uart_bits::kReplyBytes];
    tmc_uart_bits::Frame frame;
    const bool got = bb_decode(reply, &frame) && reply[2] == kGconf;
    if (!got) {
        Serial.println("bbprobe: no reply");
        return;
    }

    Serial.print("bbprobe: rx");
    for (uint32_t i = 0; i < tmc_uart_bits::kReplyBytes; ++i) {
        Serial.print(" 0x");
        if (reply[i] < 0x10) {
            Serial.print('0');
        }
        Serial.print(reply[i], HEX);
    }
    Serial.println();

    const uint8_t expected =
        tmc_uart_bits::crc8(reply, tmc_uart_bits::kReplyBytes - 1);
    Serial.print("bbprobe: crc ");
    Serial.print(expected == reply[tmc_uart_bits::kReplyBytes - 1] ? "ok"
                                                                   : "BAD");
    Serial.print(" (calc 0x");
    Serial.print(expected, HEX);
    Serial.print(" got 0x");
    Serial.print(reply[tmc_uart_bits::kReplyBytes - 1], HEX);
    Serial.println(")");

    const uint32_t period_x100 = (frame.period_q8 * 100u) / 256u;
    Serial.print("bbprobe: frame start=");
    Serial.print(frame.start);
    Serial.print(" samples, period=");
    Serial.print(period_x100 / 100u);
    Serial.print('.');
    if (period_x100 % 100u < 10u) {
        Serial.print('0');
    }
    Serial.print(period_x100 % 100u);
    Serial.print("us (Q8 ");
    Serial.print(frame.period_q8);
    Serial.println(")");
}

void bbProbeAll() {
    for (uint8_t address = 0; address < 4; ++address) {
        bbProbe(address);
    }
}

void sampleLine(uint32_t us) {
    if (us == 0 || us > kCaptureSamplesMax) {
        us = kCaptureSamplesMax;
    }

    // Release the line with the internal pulls off, so the result reflects the
    // TMC and the external network rather than our own drive.
    const uint32_t save = save_and_disable_interrupts();
    gpio_set_dir(board::kTmcUartPin, GPIO_IN);
    gpio_disable_pulls(board::kTmcUartPin);
    memset(s_samples, 0, sizeof(s_samples));
    const uint16_t t0 = cycles_now16();
    for (uint32_t i = 0; i < us; ++i) {
        wait_until_cycles16(t0 + (uint16_t)(i * s_sample_cycles));
        if (gpio_get(board::kTmcUartPin)) {
            s_samples[i >> 3] |= (uint8_t)(1u << (i & 7u));
        }
    }
    gpio_put(board::kTmcUartPin, 1);
    gpio_set_dir(board::kTmcUartPin, GPIO_OUT);
    gpio_pull_up(board::kTmcUartPin);
    restore_interrupts(save);

    uint32_t high = 0;
    uint32_t edges = 0;
    bool prev = tmc_uart_bits::level(s_samples, 0);
    for (uint32_t i = 0; i < us; ++i) {
        const bool current = tmc_uart_bits::level(s_samples, i);
        if (current) {
            ++high;
        }
        if (i > 0 && current != prev) {
            ++edges;
        }
        prev = current;
    }

    Serial.print("line: ");
    Serial.print(us);
    Serial.print("us first=");
    Serial.print(tmc_uart_bits::level(s_samples, 0) ? 1 : 0);
    Serial.print(" last=");
    Serial.print(tmc_uart_bits::level(s_samples, us - 1) ? 1 : 0);
    Serial.print(" high=");
    Serial.print(high);
    Serial.print("/");
    Serial.print(us);
    Serial.print(" edges=");
    Serial.print(edges);
    Serial.print(" idle=");
    Serial.println(tmc_uart_bits::level(s_samples, us - 1) ? "high" : "low");
}

void printHex32(uint32_t value) {
    char text[9];
    for (int i = 7; i >= 0; --i) {
        text[i] = "0123456789ABCDEF"[value & 0xFu];
        value >>= 4;
    }
    text[8] = '\0';
    Serial.print(text);
}

uint32_t expectedIholdIrun(uint8_t index) {
    const Config& c = s_config[index];
    bool vsense = false;
    uint32_t irun = 0;
    currentScale(c.run_ma, &vsense, &irun);
    const uint32_t ihold = iholdFor(irun, c.run_ma, c.hold_ma);
    return (ihold & 0x1Fu) | ((irun & 0x1Fu) << 8) | (1u << 16);
}

void printRegisterLine(const char* name, bool have, uint32_t value,
                       bool have_init, uint32_t init) {
    Serial.print("  ");
    Serial.print(name);
    for (size_t i = strlen(name); i < 12; ++i) {
        Serial.print(' ');
    }
    if (!have) {
        Serial.println("read FAILED");
        return;
    }
    Serial.print("0x");
    printHex32(value);
    if (have_init) {
        Serial.print("  init 0x");
        printHex32(init);
        if (value != init) {
            Serial.print("  MISMATCH");
        }
    }
    Serial.println();
}

void printWriteOnlyLine(const char* name, uint32_t value) {
    printRegisterLine(name, true, value, true, value);
}

void printRegisters(uint8_t index) {
    if (index >= kDriverCount) {
        return;
    }

    // Readable registers, in address order. The value is what the driver
    // returns now; init is what the firmware wrote (or expects) at bring-up.
    struct Entry {
        uint8_t address;
        const char* name;
        bool has_init;
    };
    static const Entry kEntries[] = {
        {kGconf, "GCONF", true},
        {kGstat, "GSTAT", false},
        {kIfcnt, "IFCNT", false},
        {kOtpRead, "OTP_READ", false},
        {kIoin, "IOIN", false},
        {kFactoryConf, "FACTORY_CONF", false},
        {kTstep, "TSTEP", false},
        {kSgResult, "SG_RESULT", false},
        {kMsCnt, "MSCNT", false},
        {kMsCurAct, "MSCURACT", false},
        {kChopconf, "CHOPCONF", true},
        {kDrvStatus, "DRV_STATUS", false},
        {kPwmconf, "PWMCONF", true},
        {kPwmScale, "PWM_SCALE", false},
        {kPwmAuto, "PWM_AUTO", false},
    };

    Serial.print("tmc");
    Serial.print(index);
    Serial.print("@");
    Serial.print(driverAddress(index));
    Serial.println(": register readback");
    Serial.println("  register    value       init");

    uint32_t drv_status = 0;
    bool have_drv_status = false;

    for (const Entry& entry : kEntries) {
        uint32_t value = 0;
        const bool ok = readDriverReg(index, entry.address, &value);

        uint32_t init = 0;
        bool has_init = entry.has_init;
        if (entry.address == kGconf) {
            init = initGconfValue();
        } else if (entry.address == kChopconf) {
            init = chopconfValue(s_status[index].vsense);
        } else if (entry.address == kPwmconf) {
            init = kDefaultPwmconf;
        }

        printRegisterLine(entry.name, ok, value, has_init, init);

        if (entry.address == kDrvStatus && ok) {
            drv_status = value;
            have_drv_status = true;
        }
    }

    if (have_drv_status) {
        Serial.print("  DRV flags: otpw=");
        Serial.print((drv_status & (1u << 0)) ? 1 : 0);
        Serial.print(" ot=");
        Serial.print((drv_status & (1u << 1)) ? 1 : 0);
        Serial.print(" s2g=");
        Serial.print(((drv_status & (1u << 2)) ||
                      (drv_status & (1u << 3)))
                         ? 1
                         : 0);
        Serial.print(" ola=");
        Serial.print((drv_status & (1u << 6)) ? 1 : 0);
        Serial.print(" olb=");
        Serial.print((drv_status & (1u << 7)) ? 1 : 0);
        Serial.print(" stst=");
        Serial.print((drv_status & (1u << 31)) ? 1 : 0);
        Serial.print(" stealth=");
        Serial.print((drv_status & (1u << 30)) ? 1 : 0);
        Serial.print(" cs=");
        Serial.println((drv_status >> 16) & 0x1F);
    }

    // Write-only registers cannot be read back; print the value the firmware
    // wrote (or the boot default) so the bench can still compare it.
    Serial.println("  write-only (firmware shadow):");
    printWriteOnlyLine("SLAVECONF", (uint32_t)kDefaultSendDelay << 8);
    const uint32_t ihold_irun = expectedIholdIrun(index);
    printWriteOnlyLine("IHOLD_IRUN", ihold_irun);
    Serial.print("    run_ma=");
    Serial.print(s_config[index].run_ma);
    Serial.print(" hold_ma=");
    Serial.print(s_config[index].hold_ma);
    Serial.print(" IRUN=");
    Serial.print((ihold_irun >> 8) & 0x1Fu);
    Serial.print(" IHOLD=");
    Serial.print(ihold_irun & 0x1Fu);
    Serial.print(" IHOLDDELAY=");
    Serial.println((ihold_irun >> 16) & 0xFu);
    printWriteOnlyLine("TPOWERDOWN", kDefaultTpowerdown);
    printWriteOnlyLine("TPWMTHRS", 0);
    printWriteOnlyLine("TCOOLTHRS", s_config[index].tcoolthrs);
    printWriteOnlyLine("SGTHRS", s_config[index].sgthrs);
    printWriteOnlyLine("VACTUAL", 0);
}

void printDriver(uint8_t index) {
    if (index >= kDriverCount) {
        return;
    }
    const DriverStatus& s = s_status[index];
    const Config& c = s_config[index];

    Serial.print("tmc");
    Serial.print(index);
    Serial.print("@");
    Serial.print(driverAddress(index));
    Serial.print(s.ready ? " ready" : " NOT_READY");
    Serial.print(s.online ? " online" : " offline");
    Serial.print(" run_ma=");
    Serial.print(c.run_ma);
    Serial.print(" hold_ma=");
    Serial.print(c.hold_ma);
    Serial.print(" irun=");
    Serial.print(s.irun);
    Serial.print(" ihold=");
    Serial.print(s.ihold);
    Serial.print(" vsense=");
    Serial.print(s.vsense ? 1 : 0);
    Serial.print(" sgthrs=");
    Serial.print(s.sgthrs);
    Serial.print(" tcoolthrs=0x");
    Serial.print(s.tcoolthrs, HEX);
    Serial.print(" drv=0x");
    Serial.print(s.drv_status, HEX);
    Serial.print(" otpw=");
    Serial.print((s.drv_status & (1u << 0)) ? 1 : 0);
    Serial.print(" ot=");
    Serial.print((s.drv_status & (1u << 1)) ? 1 : 0);
    Serial.print(" s2g=");
    Serial.print(((s.drv_status & (1u << 2)) || (s.drv_status & (1u << 3))) ? 1 : 0);
    Serial.print(" ola=");
    Serial.print((s.drv_status & (1u << 6)) ? 1 : 0);
    Serial.print(" olb=");
    Serial.print((s.drv_status & (1u << 7)) ? 1 : 0);
    Serial.print(" stst=");
    Serial.print((s.drv_status & (1u << 31)) ? 1 : 0);
    Serial.print(" stealth=");
    Serial.print((s.drv_status & (1u << 30)) ? 1 : 0);
    Serial.print(" cs=");
    Serial.print(s.cs_actual);
    Serial.print("/");
    Serial.print(s.irun);
    Serial.print(" sg_result=");
    Serial.print(s.sg_result);
    Serial.print("/");
    Serial.print((uint16_t)(2u * s.sgthrs));
    Serial.print(" tstep=");
    Serial.print(s.tstep);
    Serial.print(" pwm=");
    Serial.print(s.pwm_scale_sum);
    Serial.print("/");
    Serial.print(s.pwm_scale_auto);
    Serial.print(" auto=");
    Serial.print(s.pwm_ofs_auto);
    Serial.print("/");
    Serial.print(s.pwm_grad_auto);
    if (s.stall) {
        Serial.print(" STALL");
    }
    if (s.current_limited) {
        Serial.print(" CURRENT_LIMITED");
    }
    if (s.diag_event) {
        Serial.print(" DIAG_EVENT");
    }
    Serial.print(" diag=");
    Serial.print(digitalRead(board::kDrives[index].diag) ? 1 : 0);
    Serial.println();
}

}  // namespace tmc2209
