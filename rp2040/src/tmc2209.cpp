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
constexpr uint8_t kOtpRead = 0x05;
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

// 200 kBaud. A bit is 5 us, an exact number of time_us_64 ticks, and the
// absolute per-bit deadline keeps our edges on that grid. The reply is
// sampled on the 1 us tick (five samples per bit) so the decoder can fit the
// period instead of trusting a short per-reply estimate.
constexpr uint32_t kProbeBitUs = 5;
constexpr uint32_t kSampleUs = 1;
constexpr uint32_t kCaptureBits = 128;
constexpr uint32_t kCaptureSamples = kCaptureBits * kProbeBitUs / kSampleUs;
constexpr uint32_t kNominalPeriodQ8 = kProbeBitUs * 256u / kSampleUs;

uint8_t s_samples[(kCaptureSamples + 7u) / 8u];
uint32_t s_irq = 0;
bool s_irq_held = false;

inline void wait_until_us(uint64_t target_us) {
    while (time_us_64() < target_us) {
        tight_loop_contents();
    }
}

void bb_release_irq() {
    if (s_irq_held) {
        restore_interrupts(s_irq);
        s_irq_held = false;
    }
}

void pin_init() {
    gpio_init(board::kTmcUartPin);
    gpio_set_dir(board::kTmcUartPin, GPIO_IN);
    gpio_set_input_enabled(board::kTmcUartPin, true);
    gpio_pull_up(board::kTmcUartPin);
}

// Drive one datagram as a continuous bit stream, then sample the released
// line at 1 us. Interrupts stay masked across both halves: restoring them
// between the request and the reply lets USB run while the TMC is driving.
void bb_exchange(const uint8_t* data, size_t len) {
    uint8_t bits[8 * 10 + 8];
    size_t nbits = 0;
    for (size_t n = 0; n < len && n < 8; ++n) {
        bits[nbits++] = 0;  // start
        for (uint32_t i = 0; i < 8; ++i) {
            bits[nbits++] = (data[n] >> i) & 1u;
        }
        bits[nbits++] = 1;  // stop
    }
    // Four extra high bits keep the line idle-high; the TMC starts its reply
    // after the fixed reply delay, so this does not collide with it.
    for (uint32_t i = 0; i < 4; ++i) {
        bits[nbits++] = 1;
    }

    gpio_set_function(board::kTmcUartPin, GPIO_FUNC_SIO);
    gpio_set_input_enabled(board::kTmcUartPin, true);
    gpio_put(board::kTmcUartPin, 1);
    gpio_set_dir(board::kTmcUartPin, GPIO_OUT);
    // The pull-downs hold a break between probes. Idle high has to be longer
    // than a break recovery at the slowest UART baud.
    wait_until_us(time_us_64() + 5000);

    s_irq = save_and_disable_interrupts();
    s_irq_held = true;
    const uint64_t start = time_us_64();
    for (size_t k = 0; k < nbits; ++k) {
        gpio_put(board::kTmcUartPin, bits[k]);
        wait_until_us(start + (uint64_t)(k + 1) * kProbeBitUs);
    }
    gpio_set_dir(board::kTmcUartPin, GPIO_IN);

    memset(s_samples, 0, sizeof(s_samples));
    const uint64_t t0 = time_us_64();
    for (uint32_t i = 0; i < kCaptureSamples; ++i) {
        wait_until_us(t0 + (uint64_t)i * kSampleUs);
        if (gpio_get(board::kTmcUartPin)) {
            s_samples[i >> 3] |= (uint8_t)(1u << (i & 7u));
        }
    }
    bb_release_irq();
}

bool bb_decode(uint8_t reply[tmc_uart_bits::kReplyBytes]) {
    tmc_uart_bits::Frame frame;
    if (!tmc_uart_bits::decode_reply(s_samples, kCaptureSamples,
                                     kNominalPeriodQ8, &frame)) {
        return false;
    }
    memcpy(reply, frame.bytes, tmc_uart_bits::kReplyBytes);
    return true;
}

void bb_dump_samples() {
    constexpr uint32_t kLine = 128;
    Serial.print("bbprobe: oversample ");
    Serial.print(kCaptureSamples);
    Serial.print(" samples, ");
    Serial.print(kProbeBitUs / kSampleUs);
    Serial.print("/bit, bit ");
    Serial.print(kProbeBitUs);
    Serial.println("us");
    for (uint32_t start = 0; start < kCaptureSamples; start += kLine) {
        const uint32_t stop = (start + kLine < kCaptureSamples)
                                  ? start + kLine
                                  : kCaptureSamples;
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

// Build a datagram for `address`, send it, and decode the 8-byte reply.
bool transact(uint8_t address, uint8_t reg, uint32_t value, bool write,
              uint8_t reply[tmc_uart_bits::kReplyBytes]) {
    uint8_t request[tmc_uart_bits::kReplyBytes];
    const uint32_t len = tmc_uart_bits::encode(address, reg, value, write,
                                               request);
    bb_exchange(request, len);
    return bb_decode(reply) && reply[2] == reg;
}

bool readAddressReg(uint8_t address, uint8_t reg, uint32_t* value) {
    uint8_t reply[tmc_uart_bits::kReplyBytes];
    for (uint8_t attempt = 0; attempt < 2; ++attempt) {
        if (transact(address, reg, 0, false, reply)) {
            *value = ((uint32_t)reply[3] << 24) |
                     ((uint32_t)reply[4] << 16) |
                     ((uint32_t)reply[5] << 8) | reply[6];
            return true;
        }
        delayMicroseconds(200);
    }
    return false;
}

bool writeAddressReg(uint8_t address, uint8_t reg, uint32_t value) {
    uint8_t reply[tmc_uart_bits::kReplyBytes];
    for (uint8_t attempt = 0; attempt < 2; ++attempt) {
        if (transact(address, reg, value, true, reply)) {
            return true;
        }
        delayMicroseconds(200);
    }
    return false;
}

bool readDriverReg(uint8_t index, uint8_t reg, uint32_t* value) {
    return index < kDriverCount &&
           readAddressReg(driverAddress(index), reg, value);
}

bool writeDriverReg(uint8_t index, uint8_t reg, uint32_t value) {
    return index < kDriverCount &&
           writeAddressReg(driverAddress(index), reg, value);
}

bool verifyDriverReg(uint8_t index, uint8_t reg, uint32_t expected,
                     uint32_t mask = 0xFFFFFFFFu) {
    uint32_t value = 0;
    return readDriverReg(index, reg, &value) &&
           ((value & mask) == (expected & mask));
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

bool failDriver(uint8_t index, const char* step) {
    Serial.print("tmc");
    Serial.print(index);
    Serial.print("@");
    Serial.print(driverAddress(index));
    Serial.print(": ");
    Serial.println(step);
    return false;
}

bool configureDriver(uint8_t index) {
    uint32_t gconf = 0;
    uint32_t otp = 0;
    if (!readDriverReg(index, kGconf, &gconf)) {
        return failDriver(index, "GCONF read failed");
    }
    if (!readDriverReg(index, kOtpRead, &otp)) {
        return failDriver(index, "OTP_READ read failed");
    }

    // OTP0.6 selects internal RDSon sensing. These boards fit external sense
    // resistors, and OTP can only be set, never cleared. Refuse the driver.
    if (otp & (1u << 6)) {
        return failDriver(index, "OTP internal_Rsense is set; refusing to enable");
    }

    // Force external current sensing first and require the read-back before
    // changing any other bit (doc/controller_analysis.md §6.1).
    uint32_t want = gconf & 0x3FFu;
    want &= ~(1u << 1);
    if (!writeDriverReg(index, kGconf, want) ||
        !verifyDriverReg(index, kGconf, want, 0x3FFu)) {
        return failDriver(index, "GCONF force-write verify failed");
    }

    // Digital current scale, StealthChop, register microstepping.
    want &= ~(1u << 0);  // I_scale_analog = 0
    want &= ~(1u << 2);  // en_spreadcycle = 0
    want |= (1u << 6);   // pdn_disable
    want |= (1u << 7);   // mstep_reg_select
    want |= (1u << 8);   // multistep_filt
    if (!writeDriverReg(index, kGconf, want) ||
        !verifyDriverReg(index, kGconf, want, 0x3FFu)) {
        return failDriver(index, "GCONF config verify failed");
    }
    s_status[index].gconf = want;

    // SENDDELAY must be configured before the reply timing is relied on.
    const uint32_t slaveconf = (uint32_t)kDefaultSendDelay << 8;
    if (!writeDriverReg(index, kSlaveconf, slaveconf) ||
        !verifyDriverReg(index, kSlaveconf, slaveconf, 0xF00u)) {
        return failDriver(index, "SLAVECONF SENDDELAY verify failed");
    }

    bool vsense = false;
    uint32_t irun = 0;
    currentScale(kDefaultRunMa, &vsense, &irun);
    const uint32_t ihold = iholdFor(irun, kDefaultRunMa, kDefaultHoldMa);
    const uint32_t ihold_irun =
        (ihold & 0x1Fu) | ((irun & 0x1Fu) << 8) | (1u << 16);
    const uint32_t chopconf = chopconfValue(vsense);

    if (!writeDriverReg(index, kChopconf, chopconf) ||
        !verifyDriverReg(index, kChopconf, chopconf)) {
        return failDriver(index, "CHOPCONF verify failed");
    }
    if (!writeDriverReg(index, kIholdIrun, ihold_irun) ||
        !verifyDriverReg(index, kIholdIrun, ihold_irun)) {
        return failDriver(index, "IHOLD_IRUN read-back mismatch");
    }
    if (!writeDriverReg(index, kPwmconf, kDefaultPwmconf) ||
        !verifyDriverReg(index, kPwmconf, kDefaultPwmconf)) {
        return failDriver(index, "PWMCONF verify failed");
    }
    if (!writeDriverReg(index, kTpowerdown, kDefaultTpowerdown) ||
        !verifyDriverReg(index, kTpowerdown, kDefaultTpowerdown)) {
        return failDriver(index, "TPOWERDOWN verify failed");
    }
    if (!writeDriverReg(index, kTpwmthrs, 0) ||
        !verifyDriverReg(index, kTpwmthrs, 0)) {
        return failDriver(index, "TPWMTHRS verify failed");
    }
    if (!writeDriverReg(index, kTcoolthrs, kDefaultTcoolthrs) ||
        !verifyDriverReg(index, kTcoolthrs, kDefaultTcoolthrs)) {
        return failDriver(index, "TCOOLTHRS verify failed");
    }
    if (!writeDriverReg(index, kSgthrs, kDefaultSgthrs) ||
        !verifyDriverReg(index, kSgthrs, kDefaultSgthrs)) {
        return failDriver(index, "SGTHRS verify failed");
    }

    // DRV_STATUS is telemetry, not part of the enable safety check. A missing
    // motor supply can make this read fail; configuration is still valid and
    // the DIAG path retries later. valid_mask bit 3 stays clear until then.
    uint32_t drv_status = 0;
    if (readDriverReg(index, kDrvStatus, &drv_status)) {
        s_status[index].drv_status = drv_status;
        s_seen_mask |= (uint8_t)(1u << index);
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
        wait_until_us(t0 + (2u * bit + 1u) * kProbeBitUs / 2u);
        const int expected = (bit == 0) ? 0
                             : (bit == 9) ? 1
                                          : ((value >> (bit - 1u)) & 1u);
        if ((gpio_get(board::kTmcUartPin) ? 1 : 0) != expected) {
            ok = false;
        }
        if (bit < 9) {
            wait_until_us(t0 + (bit + 1u) * kProbeBitUs);
            const int next =
                (bit + 1u == 9u) ? 1 : ((value >> bit) & 1u);
            gpio_put(board::kTmcUartPin, next);
        }
    }
    wait_until_us(t0 + 10u * kProbeBitUs);
    gpio_set_dir(board::kTmcUartPin, GPIO_IN);
    restore_interrupts(save);
    return ok;
}

}  // namespace

uint8_t begin() {
    // All outputs off before the first UART access.
    stepper_axes::setEnabled(false);
    pin_init();

    s_ready_mask = 0;
    s_seen_mask = 0;
    s_last_diag = 0;
    s_diag_valid = false;

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
    if (!writeDriverReg(index, kChopconf, chopconf)) {
        return false;
    }
    if (!writeDriverReg(index, kIholdIrun, ihold_irun)) {
        return false;
    }
    if (!verifyDriverReg(index, kIholdIrun, ihold_irun)) {
        return false;
    }
    if (!readDriverReg(index, kChopconf, &chopconf)) {
        return false;
    }

    s_config[index].run_ma = run_ma;
    s_config[index].hold_ma = hold_ma;
    s_status[index].irun = (uint8_t)irun;
    s_status[index].ihold = (uint8_t)ihold;
    s_status[index].vsense = ((chopconf >> 17) & 1u) != 0;
    return true;
}

bool setStallGuard(uint8_t index, uint8_t sgthrs) {
    if (!ready(index)) {
        return false;
    }
    if (!writeDriverReg(index, kSgthrs, sgthrs) ||
        !verifyDriverReg(index, kSgthrs, sgthrs)) {
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
    if (!writeDriverReg(index, kTcoolthrs, value) ||
        !verifyDriverReg(index, kTcoolthrs, value)) {
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
    if (!writeDriverReg(index, kChopconf, chopconf)) {
        return false;
    }
    uint32_t readback = 0;
    if (!readDriverReg(index, kChopconf, &readback)) {
        return false;
    }
    return ((readback >> 24) & 0xFu) == mres &&
           (((readback >> 28) & 0x1u) != 0) == interpolate;
}

bool setChopper(uint8_t index, uint32_t chopconf) {
    if (!ready(index)) {
        return false;
    }
    // VSENSE belongs to the current setting; keep the raw experiment from
    // changing the current scale behind setCurrents().
    chopconf = (chopconf & ~(1u << 17)) |
               (s_status[index].vsense ? (1u << 17) : 0);
    return writeDriverReg(index, kChopconf, chopconf) &&
           verifyDriverReg(index, kChopconf, chopconf);
}

bool setPwmConf(uint8_t index, uint32_t pwmconf) {
    if (!ready(index)) {
        return false;
    }
    return writeDriverReg(index, kPwmconf, pwmconf) &&
           verifyDriverReg(index, kPwmconf, pwmconf);
}

bool setSendDelay(uint8_t index, uint8_t delay) {
    if (!ready(index) || delay > 15) {
        return false;
    }
    const uint32_t value = (uint32_t)delay << 8;
    return writeDriverReg(index, kSlaveconf, value) &&
           verifyDriverReg(index, kSlaveconf, value, 0xF00u);
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
    if (readDriverReg(index, kIholdIrun, &value)) {
        s_status[index].irun = (uint8_t)((value >> 8) & 0x1Fu);
        s_status[index].ihold = (uint8_t)(value & 0x1Fu);
        any_ok = true;
    }
    if (readDriverReg(index, kChopconf, &value)) {
        s_status[index].vsense = ((value >> 17) & 1u) != 0;
        any_ok = true;
    }
    if (readDriverReg(index, kSgthrs, &value)) {
        s_status[index].sgthrs = (uint8_t)value;
        s_config[index].sgthrs = (uint8_t)value;
        any_ok = true;
    }
    if (readDriverReg(index, kTcoolthrs, &value)) {
        s_status[index].tcoolthrs = value & 0xFFFFFu;
        s_config[index].tcoolthrs = value & 0xFFFFFu;
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

    bb_exchange(request, len);
    bb_dump_samples();

    uint8_t reply[tmc_uart_bits::kReplyBytes];
    const bool got = bb_decode(reply) && reply[2] == kGconf;
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
}

void bbProbeAll() {
    for (uint8_t address = 0; address < 4; ++address) {
        bbProbe(address);
    }
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
