#pragma once

#include <stdint.h>

// TMC2209 configuration over the shared single-wire UART (GPIO9). The wire is
// bit-banged end to end through SIO; no UART peripheral or TMC library is used.
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
    uint32_t tstep;       // raw 20-bit TSTEP; 0xFFFFF at standstill
    uint8_t  irun;
    uint8_t  ihold;
    bool     vsense;
    uint8_t  sgthrs;
    uint32_t tcoolthrs;
    // Last PWM/current sample. CS_ACTUAL follows IHOLD/IRUN; a value below
    // irun while moving means the driver is not delivering the set current.
    uint8_t  cs_actual;
    uint8_t  pwm_scale_sum;    // PWM_SCALE bits 0..7, 255 = regulator at limit
    int16_t  pwm_scale_auto;   // PWM_SCALE bits 24..16, signed 9-bit
    uint8_t  pwm_ofs_auto;     // PWM_AUTO bits 0..7
    uint8_t  pwm_grad_auto;    // PWM_AUTO bits 23..16
    bool     stall;            // SG_RESULT <= 2*SGTHRS while moving
    bool     current_limited;  // moving and current not reached / PWM at limit
    bool     diag_event;       // a DIAG rising edge occurred since bring-up
};

// Bit-banged UART health counters, one set per TMC2209 address. IFCNT is the
// driver's count of accepted writes; writes_since_check counts writes we
// verified. The periodic check flags the IFCNT delta only when it is smaller
// than that count; a retried frame that landed twice is not a lost write.
struct UartStats {
    uint32_t read_attempts;    // read frames sent, including retries
    uint32_t reads;            // read transactions decoded successfully
    uint32_t read_failures;    // transactions that failed after all retries
    uint32_t writes;           // write frames sent, including retries
    uint32_t write_verified;   // writes confirmed by an IFCNT increment
    uint32_t write_failures;   // verified writes that failed after all retries
    uint32_t ifcnt_checks;     // successful IFCNT comparisons
    uint32_t ifcnt_mismatches; // IFCNT delta != writes sent since last check
    uint8_t  ifcnt_last;
    uint8_t  writes_since_check;
    bool     ifcnt_valid;
};

// Drives EN high first, then configures every driver over UART. Returns a
// bitmask of drivers that passed the GCONF/OTP safety check and register
// verification. A driver that fails is left disabled.
uint8_t begin();

bool ready(uint8_t index);
const Config& config(uint8_t index);
const DriverStatus& status(uint8_t index);
const UartStats& uartStats(uint8_t index);

// Read IFCNT and compare its advance with the writes sent since the previous
// check. Called periodically from poll(); the bench can call it on demand.
bool checkIfcnt(uint8_t index);
// Bench-only: time one SG_RESULT read and one full refresh() in microseconds,
// including the interrupt-masked window and any retries. Prints min/mean/max.
void timeUart(uint8_t index, uint16_t n);
void printStats(uint8_t index);
void printStats();

// Clear the latched DIAG event flags and the pending ISR bits. The pin state
// is not changed; a new rising edge latches again.
void clearDiagEvents();

bool setCurrents(uint8_t index, uint16_t run_ma, uint16_t hold_ma);
bool setStallGuard(uint8_t index, uint8_t sgthrs);
bool setTcoolthrs(uint8_t index, uint32_t value);
bool setMicrosteps(uint8_t index, uint16_t microsteps, bool interpolate);
bool setChopper(uint8_t index, uint32_t chopconf);
bool setPwmConf(uint8_t index, uint32_t pwmconf);
// GCONF.en_spreadCycle. Call only with the axis stopped; the caller checks
// that. Motion uses SpreadCycle, homing uses StealthChop.
bool setSpreadCycle(uint8_t index, bool enable);
// True when StallGuard4 can be trusted for a stall: moving, StealthChop,
// SGTHRS != 0, and TSTEP inside the TCOOLTHRS window. Does not mean the rotor
// is turning; a high-speed stall can still leave SG_RESULT high.
bool stallArmed(uint8_t index);
// SLAVECONF SENDDELAY, 0..15 bit times. Written as 2 during bring-up.
bool setSendDelay(uint8_t index, uint8_t delay);

bool readRegister(uint8_t index, uint8_t address, uint32_t* value);
bool writeRegister(uint8_t index, uint8_t address, uint32_t value);

// Watch the four DIAG pins. A changed pin triggers one DRV_STATUS read for
// the health frame; no periodic UART work is done on the motion loop.
void poll(uint32_t now_ms);

// Read DRV_STATUS, SG_RESULT, TSTEP and the current/SG registers now.
void refresh(uint8_t index);

// Bench-only motion monitor: sample SG_RESULT, PWM_SCALE and DRV_STATUS once
// per interval_ms and print one line per sample. A continuous UART read would
// stall the motion loop, so this is explicit and time-limited.
// duration_ms == 0 stops an active monitor.
bool startMonitor(uint8_t index, uint32_t duration_ms, uint32_t interval_ms);

uint8_t otpwMask();
uint8_t otMask();
bool statusValid();

// Bench self-test: bit-bang a pattern through SIO and sample the pad at every
// bit centre. Proves the pin function and output path, not the TMC.
void selftest();

// Read GCONF at UART addresses 0..3 to find which strap addresses answer.
// Read-only; useful when the configured drive addresses all fail.
void scanAddresses();

// Pure bit-banged test path: drive the whole GCONF-read datagram as one
// continuous bit stream, release the pad, oversample the reply and decode it
// with the production CRC-guided decoder (no TMC library).
void bbProbe(uint8_t address);
void bbProbeAll();

// Release the shared line and observe it for `us` microseconds. Internal
// pulls are disabled so the report shows what the TMC/external network does,
// then the line is restored to output high. `us` is clamped to the capture
// buffer (kCaptureSamplesMax samples, 4 per bit).
void sampleLine(uint32_t us);

// Idle high bits after the request stop bit, before the line is released.
// The default is 4; the bench can sweep 0..12 to trade line-low time against
// driving into the reply start.
bool setIdleBits(uint32_t bits);
uint32_t idleBits();

void printDriver(uint8_t index);

// Read every readable register over the UART and print it next to the value the
// firmware wrote at bring-up (or the running configuration for tunables).
// Write-only registers (IHOLD_IRUN, SGTHRS, TCOOLTHRS, ...) are printed from the
// configuration shadow because the TMC2209 cannot return them.
void printRegisters(uint8_t index);

}  // namespace tmc2209
