#include "tmc2209.h"

#include <Arduino.h>
#include <TMCStepper.h>
#include <hardware/clocks.h>
#include <hardware/gpio.h>
#include <hardware/sync.h>
#include <hardware/uart.h>
#include <pico/stdlib.h>

#include "board_pins.h"
#include "stepper_axes.h"

namespace tmc2209 {
namespace {

static_assert(kDriverCount == board::kDriveCount,
              "the TMC2209 table must follow the drive table");

// The four PDN_UART pins share GPIO9. GPIO9 is UART1 RX but not a UART1 TX
// pin, and both PIO blocks are fully allocated (can2040 and four
// FastAccelStepper state machines), so the core's PIO-based SoftwareSerial
// cannot be used. This Stream bit-bangs TX through SIO and hands the pad to
// the hardware UART1 receiver (with its FIFO) for the reply. The pin mux is
// switched per datagram; RXE is off while TX drives the line.
constexpr uint32_t kBitNs = 8681;  // 115200 baud, 8N1

constexpr uint8_t kGconf = 0x00;
constexpr uint8_t kOtpRead = 0x05;
constexpr uint8_t kIholdIrun = 0x10;
constexpr uint8_t kTpowerdown = 0x11;
constexpr uint8_t kTstep = 0x12;
constexpr uint8_t kTpwmthrs = 0x13;
constexpr uint8_t kTcoolthrs = 0x14;
constexpr uint8_t kSgthrs = 0x40;
constexpr uint8_t kSgResult = 0x41;
constexpr uint8_t kChopconf = 0x6C;
constexpr uint8_t kDrvStatus = 0x6F;
constexpr uint8_t kPwmconf = 0x70;

constexpr uint16_t kDefaultRunMa = 300;
constexpr uint16_t kDefaultHoldMa = 300;
constexpr uint16_t kMaxRunMa = 800;  // bench cap; the reference uses 800 mA
constexpr uint16_t kDefaultMicrosteps = 16;
constexpr uint8_t kDefaultSgthrs = 40;
constexpr uint32_t kDefaultTcoolthrs = 0xFFFFF;
constexpr float kRsense = 0.110f;
constexpr uint32_t kPwmconfDefault = 0xC10D0024;

inline void wait_until_us(uint64_t target_us) {
    while (time_us_64() < target_us) {
        tight_loop_contents();
    }
}

inline void uart_rx_enable(bool enabled) {
    if (enabled) {
        uart_get_hw(uart1)->cr |= UART_UARTCR_RXE_BITS;
    } else {
        uart_get_hw(uart1)->cr &= ~UART_UARTCR_RXE_BITS;
    }
}

void uart_rx_drain() {
    while (uart_is_readable(uart1)) {
        (void)uart_get_hw(uart1)->dr;
    }
    uart_get_hw(uart1)->rsr = 0;  // clear break/framing/overrun/parity flags
}

void tmc_uart_init() {
    gpio_init(board::kTmcUartPin);
    gpio_set_dir(board::kTmcUartPin, GPIO_IN);
    gpio_set_input_enabled(board::kTmcUartPin, true);
    gpio_pull_up(board::kTmcUartPin);

    // GPIO9 is UART1 RX. TX is impossible on this pin, so it is bit-banged
    // through SIO; the UART handles only the receive path.
    uart_init(uart1, 115200);
    // Receive-only: the transmit side is bit-banged through SIO, and GPIO9 is
    // not a UART1 TX pin.
    uart_get_hw(uart1)->cr &= ~UART_UARTCR_TXE_BITS;
    gpio_set_function(board::kTmcUartPin, GPIO_FUNC_UART);
    uart_rx_enable(false);
    uart_rx_drain();
}

void uart_tx_byte(uint8_t value) {
    // Load the low level before driving, so the start edge has no high glitch.
    gpio_put(board::kTmcUartPin, 0);
    gpio_set_dir(board::kTmcUartPin, GPIO_OUT);
    const uint64_t start = time_us_64();
    for (uint32_t i = 0; i < 8; ++i) {
        wait_until_us(start + (uint64_t)(i + 1) * kBitNs / 1000u);
        gpio_put(board::kTmcUartPin, (value >> i) & 1u);
    }
    wait_until_us(start + (uint64_t)9 * kBitNs / 1000u);
    gpio_put(board::kTmcUartPin, 1);
    wait_until_us(start + (uint64_t)10 * kBitNs / 1000u);
    gpio_set_dir(board::kTmcUartPin, GPIO_IN);
}

uint8_t crc8(const uint8_t* data, size_t len) {
    uint8_t crc = 0;
    for (size_t n = 0; n < len; ++n) {
        uint8_t byte = data[n];
        for (uint32_t bit = 0; bit < 8; ++bit) {
            if (((crc >> 7) ^ (byte & 0x01u)) != 0) {
                crc = (uint8_t)((crc << 1) ^ 0x07u);
            } else {
                crc = (uint8_t)(crc << 1);
            }
            byte >>= 1;
        }
    }
    return crc;
}

// Held across the bit stream and the reply sample. Restored by bb_read_reply
// before any Serial print, so USB can run again without blocking.
uint32_t s_bb_irq = 0;
bool s_bb_irq_held = false;

void bb_release_irq() {
    if (s_bb_irq_held) {
        restore_interrupts(s_bb_irq);
        s_bb_irq_held = false;
    }
}

// Drive a whole datagram as one continuous bit stream. The output stays
// enabled across the stop/start boundary between bytes, so the line never
// floats low between bytes (the board has a pull-down on PDN_UART).
// Interrupts stay masked until bb_read_reply has sampled the reply. Restoring
// them first lets USB run while this pin is still driven high and the TMC
// reply is lost.
// 200 kBaud. A bit is 5 us, an exact number of time_us_64 ticks, and the
// absolute per-bit deadline keeps the edges on that grid. Sample on the 1 us
// system tick, i.e. five samples per bit, so the reply can be resynchronised
// and decoded off-line instead of trusting a short per-reply bit-time guess.
// 128 bit times covers the base reply delay, the maximum 15-bit SENDDELAY
// (SLAVECONF bits 11:8) and the 8-byte 8N1 reply (80 bit times).
constexpr uint32_t kProbeBitUs = 5;
constexpr uint32_t kSampleUs = 1;
constexpr uint32_t kOversample = kProbeBitUs / kSampleUs;
constexpr uint32_t kCaptureUs = 128 * kProbeBitUs;
constexpr uint32_t kCaptureSamples = kCaptureUs / kSampleUs;
uint8_t s_trace[(kCaptureSamples + 7u) / 8u];

void bb_tx_datagram(const uint8_t* data, size_t len) {
    // Four extra high bits keep the bus idle-high after the stop bit, then
    // the pin is released for the TMC2209 reply. Eight bits holds the line
    // into the reply and the chip never drives it.
    uint8_t bits[8 * 10 + 8];
    size_t nbits = 0;
    for (size_t n = 0; n < len && n < 8; ++n) {
        bits[nbits++] = 0;  // start
        for (uint32_t i = 0; i < 8; ++i) {
            bits[nbits++] = (data[n] >> i) & 1u;
        }
        bits[nbits++] = 1;  // stop
    }
    for (uint32_t i = 0; i < 4; ++i) {
        bits[nbits++] = 1;
    }

    gpio_set_function(board::kTmcUartPin, GPIO_FUNC_SIO);
    gpio_set_input_enabled(board::kTmcUartPin, true);
    gpio_put(board::kTmcUartPin, 1);
    gpio_set_dir(board::kTmcUartPin, GPIO_OUT);
    // The pull-downs hold a break between probes. Idle high has to be longer
    // than a break recovery at the slowest UART baud (12 bits at 9000 is 1.3 ms).
    wait_until_us(time_us_64() + 5000);
    s_bb_irq = save_and_disable_interrupts();
    s_bb_irq_held = true;
    const uint64_t start = time_us_64();
    for (size_t k = 0; k < nbits; ++k) {
        gpio_put(board::kTmcUartPin, bits[k]);
        wait_until_us(start + (uint64_t)(k + 1) * kProbeBitUs);
    }
    gpio_set_dir(board::kTmcUartPin, GPIO_IN);
}

bool bb_trace_level(uint32_t index) {
    return ((s_trace[index >> 3] >> (index & 7u)) & 1u) != 0;
}

// Raw oversampling, so a slow capture can be pasted into a host script and
// resynchronised there. One line per 128 samples keeps the output short while
// the sample index in the prefix still makes the lines concatenatable.
constexpr uint32_t kTraceLine = 128;

void bb_dump_trace(uint32_t count) {
    Serial.print("bbprobe: oversample ");
    Serial.print(count);
    Serial.print(" samples, ");
    Serial.print(kOversample);
    Serial.print("/bit, bit ");
    Serial.print(kProbeBitUs);
    Serial.println("us");
    for (uint32_t start = 0; start < count; start += kTraceLine) {
        const uint32_t stop =
            (start + kTraceLine < count) ? start + kTraceLine : count;
        Serial.print("bbprobe: os ");
        Serial.print(start);
        Serial.print(": ");
        for (uint32_t i = start; i < stop; ++i) {
            Serial.print(bb_trace_level(i) ? '1' : '0');
        }
        Serial.println();
    }
}

// Width of the first three bits of the 0x05 sync, in samples. The reply bit
// time is the one the TMC measured from our datagram, which is not always
// exactly kProbeBitUs. A fixed grid walks off the bit centre by the CRC.
uint32_t bb_sync_span(uint32_t edge) {
    uint32_t i = edge;
    auto skip = [&](bool high) {
        while (i < kCaptureSamples && bb_trace_level(i) == high) {
            ++i;
        }
    };
    // 0x05 after its first rise: high, low, high, then a long low.
    skip(true);
    skip(false);
    skip(true);
    const uint32_t span = i - edge;
    // Three bit times is 15 samples at 5 us. Reject noise outside 3..10 us.
    if (span < 9 || span > 30) {
        return kProbeBitUs * 3u;
    }
    return span;
}

bool bb_read_reply(uint8_t* reply, size_t len) {
    // Pin is already an input and interrupts are still masked. Capture the
    // pad, then restore interrupts before printing. The first rising edge is
    // data bit 0 of the 0x05 sync: the start bit is low and the pull-downs
    // already hold the line low.
    memset(s_trace, 0, sizeof(s_trace));
    const uint64_t t0 = time_us_64();
    for (uint32_t i = 0; i < kCaptureSamples; ++i) {
        wait_until_us(t0 + (uint64_t)i * kSampleUs);
        if (gpio_get(board::kTmcUartPin)) {
            s_trace[i >> 3] |= (uint8_t)(1u << (i & 7u));
        }
    }
    bb_release_irq();

    bb_dump_trace(kCaptureSamples);

    uint32_t rises = 0;
    int edge = -1;
    for (uint32_t i = 1; i < kCaptureSamples; ++i) {
        if (bb_trace_level(i) && !bb_trace_level(i - 1u)) {
            if (edge < 0) {
                edge = (int)i;
            }
            ++rises;
        }
    }
    Serial.print("bbprobe: edges ");
    Serial.print(rises);
    if (edge < 0) {
        Serial.print(" idle ");
        Serial.println(bb_trace_level(0) ? "high" : "low");
        return false;
    }
    const uint32_t span = bb_sync_span((uint32_t)edge);
    Serial.print(" bit ");
    Serial.print(span / 3u);
    Serial.println("us");

    for (size_t n = 0; n < len; ++n) {
        uint8_t value = 0;
        for (uint32_t bit = 0; bit < 8; ++bit) {
            const uint32_t index = 10u * n + bit;
            const uint32_t at =
                (uint32_t)edge + ((2u * index + 1u) * span) / 6u;
            if (at < kCaptureSamples && bb_trace_level(at)) {
                value |= (uint8_t)(1u << bit);
            }
        }
        reply[n] = value;
    }
    return true;
}

class TmcStream : public Stream {
public:
    void begin() { tmc_uart_init(); }

    int available() override {
        if (s_peek >= 0) {
            return 1;
        }
        return uart_is_readable(uart1) ? 1 : 0;
    }

    int read() override {
        if (s_peek >= 0) {
            const int value = s_peek;
            s_peek = -1;
            return value;
        }
        if (!uart_is_readable(uart1)) {
            return -1;
        }
        return (int)(uart_get_hw(uart1)->dr & 0xFFu);
    }

    int peek() override {
        if (s_peek < 0) {
            if (!uart_is_readable(uart1)) {
                return -1;
            }
            s_peek = (int)(uart_get_hw(uart1)->dr & 0xFFu);
        }
        return s_peek;
    }

    size_t write(uint8_t byte) override {
        if (s_count == 0) {
            // Take the pad from the UART receiver, discard any late write
            // acknowledgement, and switch to SIO output for the request.
            s_save = save_and_disable_interrupts();
            uart_rx_enable(false);
            uart_rx_drain();
            gpio_put(board::kTmcUartPin, 1);
            gpio_set_dir(board::kTmcUartPin, GPIO_IN);
            gpio_set_function(board::kTmcUartPin, GPIO_FUNC_SIO);
            s_len = 0;
            s_peek = -1;
        }

        uart_tx_byte(byte);
        ++s_count;

        // Byte 2 is the register; bit 7 makes it an 8-byte write datagram,
        // otherwise it is a 4-byte read request. Both get an 8-byte reply.
        if (s_count == 3) {
            s_len = (byte & 0x80) ? 8 : 4;
        }
        if (s_len != 0 && s_count == s_len) {
            // The stop bit is complete; release the pad to UART1 RX. The TMC
            // reply arrives in the UART FIFO while TMCStepper waits.
            gpio_set_function(board::kTmcUartPin, GPIO_FUNC_UART);
            uart_rx_drain();
            uart_rx_enable(true);
            restore_interrupts(s_save);
            s_count = 0;
            s_peek = -1;
        }
        return 1;
    }

    void flush() override {}

private:
    uint8_t s_len = 0;
    uint8_t s_count = 0;
    uint32_t s_save = 0;
    int s_peek = -1;
};

class Driver : public TMC2209Stepper {
public:
    Driver(Stream* stream, float rs, uint8_t addr)
        : TMC2209Stepper(stream, rs, addr) {}

    // OTP_READ is not exposed by TMCStepper's TMC2209 class.
    uint32_t readReg(uint8_t address) { return read(address); }
    void writeReg(uint8_t address, uint32_t value) { write(address, value); }
};

TmcStream s_uart;
Driver s_driver[kDriverCount] = {
    Driver(&s_uart, kRsense, board::kDrives[0].uart_address),
    Driver(&s_uart, kRsense, board::kDrives[1].uart_address),
    Driver(&s_uart, kRsense, board::kDrives[2].uart_address),
    Driver(&s_uart, kRsense, board::kDrives[3].uart_address),
};

Config s_config[kDriverCount];
DriverStatus s_status[kDriverCount];
uint8_t s_ready_mask = 0;
uint8_t s_seen_mask = 0;
uint8_t s_last_diag = 0;
bool s_diag_valid = false;

bool readRegisterImpl(uint8_t index, uint8_t address, uint32_t* value) {
    if (index >= kDriverCount || value == nullptr) {
        return false;
    }
    for (uint8_t attempt = 0; attempt < 2; ++attempt) {
        s_driver[index].CRCerror = false;
        const uint32_t result = s_driver[index].readReg(address);
        if (!s_driver[index].CRCerror) {
            *value = result;
            return true;
        }
        delayMicroseconds(200);
    }
    return false;
}

bool verifyRegister(uint8_t index, uint8_t address, uint32_t expected,
                    uint32_t mask = 0xFFFFFFFFu) {
    uint32_t value = 0;
    return readRegisterImpl(index, address, &value) &&
           ((value & mask) == (expected & mask));
}

bool failDriver(uint8_t index, const char* step) {
    Serial.print("tmc");
    Serial.print(index);
    Serial.print("@");
    Serial.print(board::kDrives[index].uart_address);
    Serial.print(": ");
    Serial.println(step);
    return false;
}

bool configureDriver(uint8_t index) {
    Driver& driver = s_driver[index];
    driver.defaults();

    uint32_t gconf = 0;
    uint32_t otp = 0;
    if (!readRegisterImpl(index, kGconf, &gconf)) {
        return failDriver(index, "GCONF read failed");
    }
    if (!readRegisterImpl(index, kOtpRead, &otp)) {
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
    driver.GCONF(want);
    if (!verifyRegister(index, kGconf, want, 0x3FFu)) {
        return failDriver(index, "GCONF force-write verify failed");
    }

    // Digital current scale, StealthChop, register microstepping.
    want &= ~(1u << 0);  // I_scale_analog = 0
    want &= ~(1u << 2);  // en_spreadcycle = 0
    want |= (1u << 6);   // pdn_disable
    want |= (1u << 7);   // mstep_reg_select
    want |= (1u << 8);   // multistep_filt
    driver.GCONF(want);
    if (!verifyRegister(index, kGconf, want, 0x3FFu)) {
        return failDriver(index, "GCONF config verify failed");
    }
    s_status[index].gconf = want;

    driver.toff(4);
    driver.microsteps(kDefaultMicrosteps);
    driver.intpol(true);
    driver.rms_current(kDefaultRunMa,
                       (float)kDefaultHoldMa / (float)kDefaultRunMa);
    driver.PWMCONF(kPwmconfDefault);
    driver.TPOWERDOWN(20);
    driver.TPWMTHRS(0);
    driver.TCOOLTHRS(kDefaultTcoolthrs);
    driver.SGTHRS(kDefaultSgthrs);

    uint32_t chop = 0;
    uint32_t ihold = 0;
    if (!readRegisterImpl(index, kChopconf, &chop)) {
        return failDriver(index, "CHOPCONF read failed");
    }
    if (!readRegisterImpl(index, kIholdIrun, &ihold)) {
        return failDriver(index, "IHOLD_IRUN read failed");
    }
    if ((chop & 0xFu) != 4u || ((chop >> 24) & 0xFu) != 4u ||
        ((chop >> 28) & 0x1u) != 1u) {
        return failDriver(index, "CHOPCONF fields mismatch");
    }
    if (ihold != driver.IHOLD_IRUN()) {
        return failDriver(index, "IHOLD_IRUN read-back mismatch");
    }
    if (!verifyRegister(index, kPwmconf, kPwmconfDefault)) {
        return failDriver(index, "PWMCONF verify failed");
    }
    if (!verifyRegister(index, kTpowerdown, 20)) {
        return failDriver(index, "TPOWERDOWN verify failed");
    }
    if (!verifyRegister(index, kTpwmthrs, 0)) {
        return failDriver(index, "TPWMTHRS verify failed");
    }
    if (!verifyRegister(index, kTcoolthrs, kDefaultTcoolthrs)) {
        return failDriver(index, "TCOOLTHRS verify failed");
    }
    if (!verifyRegister(index, kSgthrs, kDefaultSgthrs)) {
        return failDriver(index, "SGTHRS verify failed");
    }

    // DRV_STATUS is telemetry, not part of the enable safety check. A missing
    // motor supply can make this read fail; configuration is still valid and
    // the DIAG path retries later. valid_mask bit 3 stays clear until then.
    uint32_t drv_status = 0;
    if (readRegisterImpl(index, kDrvStatus, &drv_status)) {
        s_status[index].drv_status = drv_status;
        s_seen_mask |= (uint8_t)(1u << index);
    } else {
        Serial.print("tmc");
        Serial.print(index);
        Serial.print("@");
        Serial.print(board::kDrives[index].uart_address);
        Serial.println(": DRV_STATUS read failed; telemetry bit stays clear");
    }

    s_status[index].vsense = ((chop >> 17) & 1u) != 0;
    s_status[index].irun = (uint8_t)((ihold >> 8) & 0x1Fu);
    s_status[index].ihold = (uint8_t)(ihold & 0xFFu);
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
        wait_until_us(t0 + (2u * bit + 1u) * kBitNs / 2000u);
        const int expected =
            (bit == 0) ? 0 : (bit == 9) ? 1 : ((value >> (bit - 1u)) & 1u);
        if ((gpio_get(board::kTmcUartPin) ? 1 : 0) != expected) {
            ok = false;
        }
        if (bit < 9) {
            wait_until_us(t0 + (bit + 1u) * kBitNs / 1000u);
            const int next =
                (bit + 1u == 9u) ? 1 : ((value >> bit) & 1u);
            gpio_put(board::kTmcUartPin, next);
        }
    }
    wait_until_us(t0 + 10u * kBitNs / 1000u);
    gpio_set_dir(board::kTmcUartPin, GPIO_IN);
    restore_interrupts(save);
    return ok;
}

}  // namespace

uint8_t begin() {
    // All outputs off before the first UART access.
    stepper_axes::setEnabled(false);
    s_uart.begin();

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
            Serial.print(board::kDrives[i].uart_address);
            Serial.println(": ready");
        } else {
            Serial.print("tmc");
            Serial.print(i);
            Serial.print("@");
            Serial.print(board::kDrives[i].uart_address);
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
    Driver& driver = s_driver[index];
    driver.rms_current(run_ma, (float)hold_ma / (float)run_ma);

    uint32_t ihold = 0;
    uint32_t chop = 0;
    if (!readRegisterImpl(index, kIholdIrun, &ihold)) {
        return false;
    }
    if (!readRegisterImpl(index, kChopconf, &chop)) {
        return false;
    }
    if (ihold != driver.IHOLD_IRUN()) {
        return false;
    }

    s_config[index].run_ma = run_ma;
    s_config[index].hold_ma = hold_ma;
    s_status[index].irun = (uint8_t)((ihold >> 8) & 0x1Fu);
    s_status[index].ihold = (uint8_t)(ihold & 0xFFu);
    s_status[index].vsense = ((chop >> 17) & 1u) != 0;
    return true;
}

bool setStallGuard(uint8_t index, uint8_t sgthrs) {
    if (!ready(index)) {
        return false;
    }
    s_driver[index].SGTHRS(sgthrs);
    if (!verifyRegister(index, kSgthrs, sgthrs)) {
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
    s_driver[index].TCOOLTHRS(value);
    if (!verifyRegister(index, kTcoolthrs, value)) {
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
    uint8_t mres = 0;
    switch (microsteps) {
        case 256: mres = 0; break;
        case 128: mres = 1; break;
        case 64:  mres = 2; break;
        case 32:  mres = 3; break;
        case 16:  mres = 4; break;
        case 8:   mres = 5; break;
        case 4:   mres = 6; break;
        case 2:   mres = 7; break;
        default:  return false;
    }
    s_driver[index].microsteps(microsteps);
    s_driver[index].intpol(interpolate);

    uint32_t chop = 0;
    if (!readRegisterImpl(index, kChopconf, &chop)) {
        return false;
    }
    return ((chop >> 24) & 0xFu) == mres &&
           (((chop >> 28) & 0x1u) != 0) == interpolate;
}

bool setChopper(uint8_t index, uint32_t chopconf) {
    if (!ready(index)) {
        return false;
    }
    // VSENSE belongs to the current setting; keep the raw experiment from
    // changing the current scale behind setCurrents().
    if (s_status[index].vsense) {
        chopconf |= (1u << 17);
    } else {
        chopconf &= ~(1u << 17);
    }
    s_driver[index].CHOPCONF(chopconf);
    return verifyRegister(index, kChopconf, chopconf);
}

bool setPwmConf(uint8_t index, uint32_t pwmconf) {
    if (!ready(index)) {
        return false;
    }
    s_driver[index].PWMCONF(pwmconf);
    return verifyRegister(index, kPwmconf, pwmconf);
}

bool readRegister(uint8_t index, uint8_t address, uint32_t* value) {
    return readRegisterImpl(index, address, value);
}

bool writeRegister(uint8_t index, uint8_t address, uint32_t value) {
    if (index >= kDriverCount) {
        return false;
    }
    // OTP is irreversible; VACTUAL would move the motor outside the step
    // generator; IHOLD_IRUN bypasses the current cap in setCurrents().
    if (address == 0x04 || address == 0x22 || address == kIholdIrun) {
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
        if (s_status[index].vsense) {
            value |= (1u << 17);
        } else {
            value &= ~(1u << 17);
        }
    }
    // Keep TMCStepper's register cache coherent when a raw write touches a
    // register the library later read-modify-writes.
    switch (address) {
        case kGconf: s_driver[index].GCONF(value); break;
        case kChopconf: s_driver[index].CHOPCONF(value); break;
        case kPwmconf: s_driver[index].PWMCONF(value); break;
        default: s_driver[index].writeReg(address, value); break;
    }
    return true;
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
        const bool ok = readRegisterImpl(i, kDrvStatus, &drv_status);
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

    if (readRegisterImpl(index, kDrvStatus, &value)) {
        s_status[index].drv_status = value;
        s_seen_mask |= (uint8_t)(1u << index);
        any_ok = true;
    }
    if (readRegisterImpl(index, kSgResult, &value)) {
        s_status[index].sg_result = (uint16_t)value;
        any_ok = true;
    }
    if (readRegisterImpl(index, kTstep, &value)) {
        s_status[index].tstep = (uint16_t)value;
        any_ok = true;
    }
    if (readRegisterImpl(index, kIholdIrun, &value)) {
        s_status[index].irun = (uint8_t)((value >> 8) & 0x1Fu);
        s_status[index].ihold = (uint8_t)(value & 0xFFu);
        any_ok = true;
    }
    if (readRegisterImpl(index, kChopconf, &value)) {
        s_status[index].vsense = ((value >> 17) & 1u) != 0;
        any_ok = true;
    }
    if (readRegisterImpl(index, kSgthrs, &value)) {
        s_status[index].sgthrs = (uint8_t)value;
        s_config[index].sgthrs = (uint8_t)value;
        any_ok = true;
    }
    if (readRegisterImpl(index, kTcoolthrs, &value)) {
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

    // 1. UART1 internal loopback: proves baud, RX enable, FIFO and our read
    // path. LBE bypasses the pins, so this does not involve the TMC. A fresh
    // pattern per run avoids accepting a stale byte as a pass.
    uart_hw_t* hw = uart_get_hw(uart1);
    uart_rx_enable(false);
    uart_rx_drain();
    hw->cr &= ~UART_UARTCR_UARTEN_BITS;
    hw->cr |= UART_UARTCR_RXE_BITS | UART_UARTCR_TXE_BITS |
              UART_UARTCR_LBE_BITS;
    hw->cr |= UART_UARTCR_UARTEN_BITS;
    delayMicroseconds(200);
    hw->dr = pattern;

    bool rx_ok = false;
    bool saw_byte = false;
    uint8_t first = 0;
    const uint64_t deadline = time_us_64() + 5000;
    while (time_us_64() < deadline) {
        if (!uart_is_readable(uart1)) {
            continue;
        }
        const uint8_t got = (uint8_t)(hw->dr & 0xFFu);
        if (!saw_byte) {
            first = got;
            saw_byte = true;
        }
        if (got == pattern) {
            rx_ok = true;
            break;
        }
    }
    hw->cr &= ~(UART_UARTCR_TXE_BITS | UART_UARTCR_LBE_BITS);
    uart_rx_enable(false);
    uart_rx_drain();

    // 2. Bit-banged TX pad readback: proves the SIO output drives GPIO9.
    const bool tx_ok = tx_pad_test(pattern);

    gpio_set_function(board::kTmcUartPin, GPIO_FUNC_UART);
    uart_rx_drain();
    uart_rx_enable(true);

    Serial.print("selftest: uart rx loopback ");
    if (rx_ok) {
        Serial.println("ok");
    } else if (saw_byte) {
        Serial.print("FAILED got=0x");
        Serial.print(first, HEX);
        Serial.print(" want=0x");
        Serial.println(pattern, HEX);
    } else {
        Serial.print("FAILED timeout, want=0x");
        Serial.println(pattern, HEX);
    }
    Serial.print("selftest: tx pad readback ");
    Serial.println(tx_ok ? "ok" : "FAILED");
    Serial.print("selftest: clk_peri=");
    Serial.print(clock_get_hz(clk_peri));
    Serial.print(" cr=0x");
    Serial.print(uart_get_hw(uart1)->cr, HEX);
    Serial.print(" ibrd=");
    Serial.print(uart_get_hw(uart1)->ibrd);
    Serial.print(" fbrd=");
    Serial.println(uart_get_hw(uart1)->fbrd);
    Serial.print("selftest: pin func=");
    Serial.print((int)gpio_get_function(board::kTmcUartPin));
    Serial.print(" dir=");
    Serial.print(gpio_get_dir(board::kTmcUartPin) ? "out" : "in");
    Serial.print(" ie=");
    Serial.print((pads_bank0_hw->io[board::kTmcUartPin] &
                  PADS_BANK0_GPIO0_IE_BITS) ? 1 : 0);
    Serial.print(" pue=");
    Serial.print(gpio_is_pulled_up(board::kTmcUartPin) ? 1 : 0);
    Serial.print(" pde=");
    Serial.print(gpio_is_pulled_down(board::kTmcUartPin) ? 1 : 0);
    Serial.print(" idle=");
    Serial.print(gpio_get(board::kTmcUartPin) ? 1 : 0);
    Serial.print(" rxe=");
    Serial.println((uart_get_hw(uart1)->cr & UART_UARTCR_RXE_BITS) ? 1 : 0);
    if (rx_ok && tx_ok) {
        Serial.println("selftest: MCU UART/TX path is functional; a "
                       "remaining GCONF failure is external (TMC/power/PDN)");
    }
}

void scanAddresses() {
    Serial.println("scan: GCONF read on addresses 0..3");
    for (uint8_t address = 0; address < 4; ++address) {
        Driver probe(&s_uart, kRsense, address);
        probe.CRCerror = false;
        const uint32_t value = probe.readReg(kGconf);
        Serial.print("  addr ");
        Serial.print(address);
        if (probe.CRCerror) {
            Serial.println(": no reply");
        } else {
            Serial.print(": GCONF=0x");
            Serial.println(value, HEX);
        }
    }
}

void bbProbe(uint8_t address) {
    uint8_t request[4] = {0x05, address, kGconf, 0};
    request[3] = crc8(request, 3);

    Serial.print("bbprobe addr ");
    Serial.print(address);
    Serial.print(" tx");
    for (uint8_t i = 0; i < sizeof(request); ++i) {
        Serial.print(" 0x");
        if (request[i] < 0x10) {
            Serial.print('0');
        }
        Serial.print(request[i], HEX);
    }
    Serial.println();

    uart_rx_enable(false);
    uart_rx_drain();
    bb_tx_datagram(request, sizeof(request));

    uint8_t reply[8] = {};
    const bool got = bb_read_reply(reply, sizeof(reply));

    gpio_set_function(board::kTmcUartPin, GPIO_FUNC_UART);
    uart_rx_drain();
    uart_rx_enable(true);

    if (!got) {
        Serial.println("bbprobe: no reply");
        return;
    }

    Serial.print("bbprobe: rx");
    for (uint8_t i = 0; i < sizeof(reply); ++i) {
        Serial.print(" 0x");
        if (reply[i] < 0x10) {
            Serial.print('0');
        }
        Serial.print(reply[i], HEX);
    }
    Serial.println();

    const uint8_t expected = crc8(reply, sizeof(reply) - 1);
    Serial.print("bbprobe: crc ");
    Serial.print(expected == reply[sizeof(reply) - 1] ? "ok" : "BAD");
    Serial.print(" (calc 0x");
    Serial.print(expected, HEX);
    Serial.print(" got 0x");
    Serial.print(reply[sizeof(reply) - 1], HEX);
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
    Serial.print(board::kDrives[index].uart_address);
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
