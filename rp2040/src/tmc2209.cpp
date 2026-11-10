#include "tmc2209.h"

#include <Arduino.h>
#include <hardware/gpio.h>
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
constexpr uint8_t kSlaveconf = 0x03;
constexpr uint8_t kOtpProg = 0x04;
constexpr uint8_t kIholdIrun = 0x10;
constexpr uint8_t kTpowerdown = 0x11;
constexpr uint8_t kTstep = 0x12;
constexpr uint8_t kTpwmthrs = 0x13;
constexpr uint8_t kTcoolthrs = 0x14;
constexpr uint8_t kVactual = 0x22;
constexpr uint8_t kSgthrs = 0x40;
constexpr uint8_t kSgResult = 0x41;
constexpr uint8_t kChopconf = 0x6C;
constexpr uint8_t kDrvStatus = 0x6F;
constexpr uint8_t kPwmconf = 0x70;

constexpr uint16_t kDefaultRunMa = 300;
constexpr uint16_t kDefaultHoldMa = 300;
constexpr uint16_t kMaxRunMa = 800;  // bench cap; the reference uses 800 mA
constexpr uint8_t kDefaultMicrosteps = 16;
constexpr uint8_t kDefaultSgthrs = 40;
constexpr uint32_t kDefaultTcoolthrs = 0xFFFFF;
constexpr uint32_t kDefaultPwmconf = 0xC10D0024;
constexpr uint32_t kDefaultTpowerdown = 20;

// SENDDELAY is written before any read so the reply always has a clean gap
// after our last stop bit. The bench can override it per axis.
constexpr uint8_t kDefaultSendDelay = 2;

// CHOPCONF reset value used by the TMC2208/2209: TOFF=3, HSTRT=5, HEND=0,
// TBL=0, MRES=0, INTPOL=1. The bring-up then sets TOFF=4 and MRES=4.
constexpr uint32_t kChopconfBase = 0x10000053;
constexpr uint32_t kChopconfMres = 4;  // 16 microsteps
constexpr uint32_t kChopconfToff = 4;
constexpr uint32_t kChopconfIntpol = 1;

// Default 250 kBaud (4 us per bit). The bit time is switchable on the bench
// over 2..10 us (500..100 kBaud): the absolute per-bit deadline keeps our
// edges on that grid, and the reply is sampled on the 1 us tick (four samples
// per bit at 250 kBaud) so the decoder can fit the period instead of trusting
// a short per-reply estimate.
constexpr uint32_t kSampleUs = 1;
constexpr uint32_t kCaptureBits = 128;
constexpr uint32_t kMinProbeBitUs = 2;
constexpr uint32_t kMaxProbeBitUs = 10;
constexpr uint32_t kDefaultProbeBitUs = 4;
constexpr uint32_t kCaptureSamplesMax =
    kCaptureBits * kMaxProbeBitUs / kSampleUs;
// Idle high bits between the last stop bit and the reply capture. The TMC
// starts its reply about 7 bit times after the stop bit. Holding 4 releases
// the line early; the board pull-down then holds it low for a few bits, which
// can disturb the TMC. The bench can sweep this with `idle`.
uint32_t s_idle_bits = 4;

// Datasheet failure recovery: the line must be idle high for at least 12 bit
// times before the first start bit after a glitch or long idle. A 1 us prelude
// is below the TMC's 16-clock glitch filter, so the first datagram after idle
// can be discarded. This wait runs with interrupts enabled.
constexpr uint32_t kPreIdleBits = 12;

uint32_t s_probe_bit_us = kDefaultProbeBitUs;
uint8_t s_samples[(kCaptureSamplesMax + 7u) / 8u];
tmc_uart_bits::CaptureReport s_capture = {};
uint32_t s_capture_elapsed_us = 0;
uint32_t s_capture_expected_us = 0;

uint32_t captureSamples() {
    return kCaptureBits * s_probe_bit_us / kSampleUs;
}

uint32_t nominalPeriodQ8() {
    return s_probe_bit_us * 256u / kSampleUs;
}
uint32_t s_irq = 0;
bool s_irq_held = false;

inline void wait_until_us(uint64_t target_us) {
    while (time_us_64() < target_us) {
        tight_loop_contents();
    }
}

// 32-bit version for the short capture window. time_us_32() is an inline
// register read; time_us_64() is a call with a latch loop. The signed
// difference keeps it correct across the 32-bit wrap.
inline void wait_until_us32(uint32_t target_us) {
    while ((int32_t)(time_us_32() - target_us) < 0) {
        tight_loop_contents();
    }
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
// sample the reply on the 1 us tick. Interrupts stay masked through the hold
// and the capture; the line is driven high again as soon as the read is done.
// The pin is already an SIO output driving 1, so the first bit is the start
// bit with no extra preamble.
void bb_tx_read(const uint8_t* data, size_t len) {
    uint8_t bits[8 * 10];
    const size_t nbits = build_bits(data, len, bits);

    // init the buffers outside IRQ disabling
    memset(s_samples, 0, sizeof(s_samples));
    memset(&s_capture, 0, sizeof(s_capture));

    uart_line_high();
    wait_until_us(time_us_64() + (uint64_t)kPreIdleBits * s_probe_bit_us);
    s_irq = save_and_disable_interrupts();
    s_irq_held = true;
    // Align the start bit to the 1 MHz tick. Without this, the fractional
    // offset between time_us_64() and the first gpio_put shortens the start
    // bit by up to 1 us; the TMC auto-baud then locks onto a rate that is
    // wrong by up to 25 % and rejects the request (no reply, all-zero).
    const uint64_t start = time_us_64() + 1u;
    wait_until_us(start);
    for (size_t k = 0; k < nbits; ++k) {
        gpio_put(board::kTmcUartPin, bits[k]);
        wait_until_us(start + (uint64_t)(k + 1) * s_probe_bit_us);
    }

    // The last datagram bit is the stop bit, already high. Hold it for
    // s_idle_bits so the master is off the line during the reply delay.
    gpio_put(board::kTmcUartPin, 1);
    wait_until_us(start + (uint64_t)(nbits + s_idle_bits) * s_probe_bit_us);

    gpio_set_dir(board::kTmcUartPin, GPIO_IN);

    // The reply-end analysis lives in tmc_uart_bits so it stays host-testable.
    // Feed it one sample at a time; it returns true on the sample where the
    // stop bit is in and the line is still high, so we can take the line back
    // before the TMC releases it.
    tmc_uart_bits::CaptureTracker tracker(nominalPeriodQ8());
    const uint32_t t0 = time_us_32();
    const uint32_t samples = captureSamples();
    for (uint32_t i = 0; i < samples; ++i) {
        wait_until_us32(t0 + i * kSampleUs);
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
    s_capture_expected_us = (s_capture.end + 1u) * kSampleUs;
    s_capture_elapsed_us = time_us_32() - t0;

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
    wait_until_us(time_us_64() + (uint64_t)kPreIdleBits * s_probe_bit_us);
    s_irq = save_and_disable_interrupts();
    s_irq_held = true;
    // Align the start bit to the 1 MHz tick for the same reason as
    // bb_tx_read: the first bit must not be shortened by the fractional tick.
    const uint64_t start = time_us_64() + 1u;
    wait_until_us(start);
    for (size_t k = 0; k < nbits; ++k) {
        gpio_put(board::kTmcUartPin, bits[k]);
        wait_until_us(start + (uint64_t)(k + 1) * s_probe_bit_us);
    }

    bb_release_irq();
    gpio_put(board::kTmcUartPin, 1);
    wait_until_us(start + (uint64_t)(nbits + s_idle_bits) * s_probe_bit_us);
    // Leave the pin as an output driving high.
}

bool bb_decode(uint8_t reply[tmc_uart_bits::kReplyBytes],
               tmc_uart_bits::Frame* out = nullptr) {
    tmc_uart_bits::Frame frame;
    if (!tmc_uart_bits::decode_reply(s_samples, captureSamples(),
                                     nominalPeriodQ8(), &frame)) {
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
    const uint32_t captured = captureSamples();
    const uint32_t count =
        (s_capture.end + 1u < captured) ? s_capture.end + 1u : captured;
    Serial.print("bbprobe: oversample ");
    Serial.print(captured);
    Serial.print(" samples, ");
    Serial.print(s_probe_bit_us / kSampleUs);
    Serial.print("/bit, bit ");
    Serial.print(s_probe_bit_us);
    Serial.print("us, recorded ");
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

// Send a read request and decode the 8-byte reply. Writes go through
// writeAddressReg: they are write-only and their acknowledgement is not
// decoded, so a missing or differently framed write echo cannot fail a write.
bool readTransact(uint8_t address, uint8_t reg,
                  uint8_t reply[tmc_uart_bits::kReplyBytes]) {
    uint8_t request[tmc_uart_bits::kReplyBytes];
    const uint32_t len = tmc_uart_bits::encode(address, reg, 0, false, request);
    bb_tx_read(request, len);
    return bb_decode(reply) && reply[2] == reg;
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
    uint8_t request[tmc_uart_bits::kReplyBytes];
    const uint32_t len =
        tmc_uart_bits::encode(address, reg, value, true, request);
    bb_tx_only(request, len);
    return true;
}

bool readDriverReg(uint8_t index, uint8_t reg, uint32_t* value) {
    return index < kDriverCount &&
           readAddressReg(driverAddress(index), reg, value);
}

bool writeDriverReg(uint8_t index, uint8_t reg, uint32_t value) {
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
    writeDriverReg(index, kSlaveconf, slaveconf);
    uart_line_high();
    initPrefix(index);
    Serial.print("SENDDELAY=");
    Serial.print(kDefaultSendDelay);
    Serial.print(" SLAVECONF=0x");
    Serial.print(slaveconf, HEX);
    Serial.println(" (UART line high)");

    // Init is write-only: no register reads. The GCONF reset default is not
    // trusted; write every bit we care about explicitly.
    // bit 0 = 0 I_scale_analog, bit 1 = 0 external sense resistors,
    // bit 2 = 0 en_spreadcycle, bit 6 = 1 pdn_disable,
    // bit 7 = 1 mstep_reg_select, bit 8 = 1 multistep_filt.
    const uint32_t gconf = (1u << 6) | (1u << 7) | (1u << 8);
    writeDriverReg(index, kGconf, gconf);
    s_status[index].gconf = gconf;
    initPrefix(index);
    Serial.print("GCONF=0x");
    Serial.print(gconf, HEX);
    Serial.println(" external sense, pdn_disable=1, mstep_reg_select=1 written");

    bool vsense = false;
    uint32_t irun = 0;
    currentScale(kDefaultRunMa, &vsense, &irun);
    const uint32_t ihold = iholdFor(irun, kDefaultRunMa, kDefaultHoldMa);
    const uint32_t ihold_irun =
        (ihold & 0x1Fu) | ((irun & 0x1Fu) << 8) | (1u << 16);
    const uint32_t chopconf = chopconfValue(vsense);

    writeDriverReg(index, kChopconf, chopconf);
    writeDriverReg(index, kIholdIrun, ihold_irun);
    writeDriverReg(index, kPwmconf, kDefaultPwmconf);
    writeDriverReg(index, kTpowerdown, kDefaultTpowerdown);
    writeDriverReg(index, kTpwmthrs, 0);
    writeDriverReg(index, kTcoolthrs, kDefaultTcoolthrs);
    writeDriverReg(index, kSgthrs, kDefaultSgthrs);

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
    s_status[index].ready = true;
    s_status[index].online = true;
    uart_line_high();
    initPrefix(index);
    Serial.println("complete, UART line high");
    return true;
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
    const uint64_t t0 = time_us_64();
    bool ok = true;
    for (uint32_t bit = 0; bit < 10; ++bit) {
        wait_until_us(t0 + (2u * bit + 1u) * s_probe_bit_us / 2u);
        const int expected = (bit == 0) ? 0
                             : (bit == 9) ? 1
                                          : ((value >> (bit - 1u)) & 1u);
        if ((gpio_get(board::kTmcUartPin) ? 1 : 0) != expected) {
            ok = false;
        }
        if (bit < 9) {
            wait_until_us(t0 + (bit + 1u) * s_probe_bit_us);
            const int next =
                (bit + 1u == 9u) ? 1 : ((value >> bit) & 1u);
            gpio_put(board::kTmcUartPin, next);
        }
    }
    wait_until_us(t0 + 10u * s_probe_bit_us);
    gpio_put(board::kTmcUartPin, 1);
    gpio_set_dir(board::kTmcUartPin, GPIO_OUT);
    restore_interrupts(save);
    return ok;
}

}  // namespace

bool setBitTimeUs(uint32_t us) {
    if (us < kMinProbeBitUs || us > kMaxProbeBitUs) {
        return false;
    }
    s_probe_bit_us = us;
    return true;
}

uint32_t bitTimeUs() {
    return s_probe_bit_us;
}

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
    // All outputs off before the first UART access.
    stepper_axes::setEnabled(false);
    pin_init();

    s_ready_mask = 0;
    s_seen_mask = 0;
    s_last_diag = 0;
    s_diag_valid = false;

    Serial.print("tmc: init GPIO");
    Serial.print(board::kTmcUartPin);
    Serial.print(" SIO output high, ");
    Serial.print(1000000u / s_probe_bit_us);
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
    writeDriverReg(index, kChopconf, chopconf);
    writeDriverReg(index, kIholdIrun, ihold_irun);

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
    writeDriverReg(index, kSgthrs, sgthrs);
    s_config[index].sgthrs = sgthrs;
    s_status[index].sgthrs = sgthrs;
    return true;
}

bool setTcoolthrs(uint8_t index, uint32_t value) {
    if (!ready(index)) {
        return false;
    }
    value &= 0xFFFFFu;
    writeDriverReg(index, kTcoolthrs, value);
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
    writeDriverReg(index, kChopconf, chopconf);
    return true;
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

void poll(uint32_t) {
    if (s_ready_mask == 0) {
        return;
    }

    // DIAG is the event path; UART stays off the motion loop. A changed DIAG
    // pin (stall, pre-warning, short, open load) triggers one DRV_STATUS read,
    // which the 1 Hz health frame then reports.
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
    if (changed == 0) {
        return;
    }

    for (uint8_t i = 0; i < kDriverCount; ++i) {
        if ((changed & (uint8_t)(1u << i)) == 0 ||
            (s_ready_mask & (uint8_t)(1u << i)) == 0) {
            continue;
        }
        uint32_t drv_status = 0;
        const bool ok = readDriverReg(i, kDrvStatus, &drv_status);
        s_status[i].online = ok;
        if (ok) {
            s_status[i].drv_status = drv_status;
            s_seen_mask |= (uint8_t)(1u << i);
        }
    }
}

void refresh(uint8_t index) {
    if (index >= kDriverCount) {
        return;
    }
    // Read registers independently: with the motor supply off, DRV_STATUS may
    // fail while the digital configuration registers still read back.
    uint32_t value = 0;
    bool any_ok = false;

    if (readDriverReg(index, kDrvStatus, &value)) {
        s_status[index].drv_status = value;
        s_seen_mask |= (uint8_t)(1u << index);
        any_ok = true;
    }
    if (readDriverReg(index, kSgResult, &value)) {
        s_status[index].sg_result = (uint16_t)value;
        any_ok = true;
    }
    if (readDriverReg(index, kTstep, &value)) {
        s_status[index].tstep = (uint16_t)value;
        any_ok = true;
    }
    // IHOLD_IRUN, SGTHRS and TCOOLTHRS are write-only on the TMC2209; their
    // values come from what setCurrents()/setStallGuard()/setTcoolthrs()
    // wrote. Only CHOPCONF (VSENSE) is readable here.
    if (readDriverReg(index, kChopconf, &value)) {
        s_status[index].vsense = ((value >> 17) & 1u) != 0;
        any_ok = true;
    }
    s_status[index].online = any_ok;
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
    const uint64_t t0 = time_us_64();
    for (uint32_t i = 0; i < us; ++i) {
        wait_until_us(t0 + (uint64_t)i * kSampleUs);
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
    Serial.print((s.drv_status >> 16) & 0x1F);
    Serial.print(" sg_result=");
    Serial.print(s.sg_result);
    Serial.print(" tstep=");
    Serial.print(s.tstep);
    Serial.print(" diag=");
    Serial.print(digitalRead(board::kDrives[index].diag) ? 1 : 0);
    Serial.println();
}

}  // namespace tmc2209
