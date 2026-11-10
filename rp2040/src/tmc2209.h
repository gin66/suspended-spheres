#pragma once

#include <stdint.h>

// TMC2209 configuration over the shared single-wire UART (GPIO9).
// Register semantics and the current calculation are in
// doc/motion_streaming_whitepaper.md §5.6; the current-sense safety rule is in
// doc/controller_analysis.md §6.1.

namespace tmc2209 {

constexpr uint8_t kDriverCount = 4;

struct Config {
    uint16_t run_ma;
    uint16_t hold_ma;
    uint8_t  sgthrs;
    uint32_t tcoolthrs;
};

struct DriverStatus {
    bool     ready;       // configured at boot; safe to enable
    bool     online;      // last UART transaction succeeded
    uint32_t gconf;
    uint32_t drv_status;
    uint16_t sg_result;
    uint16_t tstep;
    uint8_t  irun;
    uint8_t  ihold;
    bool     vsense;
    uint8_t  sgthrs;
    uint32_t tcoolthrs;
};

// Drives EN high first, then configures every driver over UART. Returns a
// bitmask of drivers that passed the GCONF/OTP safety check and register
// verification. A driver that fails is left disabled.
uint8_t begin();

bool ready(uint8_t index);
const Config& config(uint8_t index);
const DriverStatus& status(uint8_t index);

bool setCurrents(uint8_t index, uint16_t run_ma, uint16_t hold_ma);
bool setStallGuard(uint8_t index, uint8_t sgthrs);
bool setTcoolthrs(uint8_t index, uint32_t value);
bool setMicrosteps(uint8_t index, uint16_t microsteps, bool interpolate);
bool setChopper(uint8_t index, uint32_t chopconf);
bool setPwmConf(uint8_t index, uint32_t pwmconf);

bool readRegister(uint8_t index, uint8_t address, uint32_t* value);
bool writeRegister(uint8_t index, uint8_t address, uint32_t value);

// Watch the four DIAG pins. A changed pin triggers one DRV_STATUS read for
// the health frame; no periodic UART work is done on the motion loop.
void poll(uint32_t now_ms);

// Read DRV_STATUS, SG_RESULT, TSTEP and the current/SG registers now.
void refresh(uint8_t index);

uint8_t otpwMask();
uint8_t otMask();
bool statusValid();

// Bench self-test: UART1 internal loopback (proves the RX/FIFO/baud path)
// followed by a SIO readback of the bit-banged TX waveform (proves the pad is
// driven). It does not prove the TMC saw the request.
void selftest();

// Read GCONF at UART addresses 0..3 to find which strap addresses answer.
// Read-only; useful when the configured drive addresses all fail.
void scanAddresses();

// Pure bit-banged test path (no TMCStepper, no hardware UART): drive the whole
// GCONF-read datagram as one continuous bit stream, release the pad, then
// bit-bang the reply. Proves whether the TMC answers at all.
void bbProbe(uint8_t address);
void bbProbeAll();

void printDriver(uint8_t index);

}  // namespace tmc2209
