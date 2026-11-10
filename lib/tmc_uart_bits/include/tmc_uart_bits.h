#pragma once

#include <stdint.h>

// Pure bit-level helpers for the TMC2209 single-wire UART. Nothing here
// touches hardware, so the same code is compiled for the RP2040 and for the
// host tests. Timing and framing follow doc/motion_streaming_whitepaper.md
// §5.6: 0x05 sync, 8N1, CRC8 polynomial 0x07, 8-byte reply.

namespace tmc_uart_bits {

constexpr uint32_t kReplyBytes = 8;
constexpr uint8_t kSync = 0x05;
constexpr uint8_t kMasterAddress = 0xFF;

// CRC8 over `len` bytes, TMC polynomial 0x07, initial value 0.
uint8_t crc8(const uint8_t* data, uint32_t len);

// Build a datagram in `out`, which must hold kReplyBytes bytes. A read
// request is four bytes (sync, address, register, CRC); a write request is
// eight bytes (sync, address, register|0x80, four data bytes, CRC). Returns
// the number of bytes to transmit.
uint32_t encode(uint8_t address, uint8_t reg, uint32_t value, bool write,
                uint8_t out[kReplyBytes]);

// Packed capture access: sample `index` is bit (index & 7) of
// samples[index >> 3]. Captures are stored little-endian like this so the
// 640-sample buffers printed as `bbprobe: os` lines can be fed in directly.
bool level(const uint8_t* samples, uint32_t index);

struct Frame {
    uint8_t bytes[kReplyBytes];
    uint32_t start;       // sample index of the 0x05 start-bit leading edge
    uint32_t period_q8;   // fitted bit period, samples * 256
};

// Decode one 8-byte reply from a capture sampled once per microsecond.
//
// `nominal_period_q8` is the expected bit period in samples * 256: 5 * 256 at
// 200 kBaud, 4 * 256 at 250 kBaud. The capture is anchored on the transmit
// release, so the decoder scans 1->0 edges for the reply start bit and tries a
// bit period around the nominal value. A candidate is accepted only when the
// frame starts with kSync and its CRC8 matches, which is what locks the
// correct start and period without trusting a short per-reply estimate.
//
// Returns false when no framed reply is present. `out` may be null to only
// ask whether a reply exists.
bool decode_reply(const uint8_t* samples, uint32_t count,
                  uint32_t nominal_period_q8, Frame* out);

// Incremental reply-end tracker for the shared PDN_UART capture.
//
// The reply is 8 bytes = 80 bits and starts with 0x05 then 0xFF. Their edges
// after the start bit are at 1, 2, 3, 4, 9 and 10 reply bits, so the sixth
// edge gives the reply's own bit period without waiting for the whole frame.
// Once the stop bit is in and the line has been high for one bit, the caller
// can drive the line again before the TMC releases it.
//
// This is pure state: feed it one sample at a time. `feed()` returns true on
// the sample where the caller should take the line back (the line is high at
// that sample). `report()` carries the fitted values and the run counters.
struct CaptureReport {
    uint32_t start;      // first 0, the 0x05 start bit
    uint32_t end;        // last fed sample, or the take-over sample
    uint32_t period_q8;  // fitted reply bit period, samples * 256
    uint32_t ones_run;   // consecutive 1s at the end
    uint32_t zeros_run;  // consecutive 0s at the end
    uint32_t total_ones; // 1s after the first 0
    uint32_t total_zeros;
    bool switched;       // caller took the line back before the TMC released
};

class CaptureTracker {
public:
    explicit CaptureTracker(uint32_t nominal_period_q8)
        : nominal_q8_(nominal_period_q8) {}

    bool feed(bool level) {
        const bool prev = prev_;
        if (level) {
            seen_high_ = true;
        }

        if (!started_) {
            // First 1->0 edge after the TMC has driven the line high. At
            // sample 0 the line was high before the release, so a 0 there is
            // the start too; the idle high before the start can be short.
            if ((index_ == 0 && !level) ||
                (prev && !level && seen_high_ && index_ > 0)) {
                started_ = true;
                report_.start = index_;
                edges_ = 0;
            }
        } else if (edges_ < kStartEdges && level != prev) {
            ++edges_;
            if (edges_ == kStartEdges - 1u) {
                edge_index_ = index_;
            } else if (edges_ == kStartEdges) {
                // The 0xFF master-address byte is a 9-bit high run between
                // these two edges. Its span is far less sensitive to edge
                // bias than the start-to-sync edge, which underestimates the
                // reply rate and makes the end estimate overshoot.
                const uint32_t period =
                    ((index_ - edge_index_) * 256u) / kPeriodBits;
                if (period >= nominal_q8_ * 85u / 100u &&
                    period <= nominal_q8_ * 115u / 100u) {
                    const uint32_t end_q8 =
                        report_.start * 256u + kFrameBits * period;
                    switch_q8_ = end_q8 - period;
                    // Two bit times past the frame end. The TMC keeps driving
                    // for about three bits after the stop, so this lands in the
                    // postamble. 105 % (84 bits) is already past the recorded
                    // postamble and would miss the take-over.
                    min_q8_ = report_.start * 256u + kMinEndBits * period;
                    report_.period_q8 = period;
                    have_period_ = true;
                }
            }
        }

        if (level) {
            ++ones_run_;
            zeros_run_ = 0;
            if (started_) {
                ++total_ones_;
            }
        } else {
            ++zeros_run_;
            ones_run_ = 0;
            if (started_) {
                ++total_zeros_;
            }
        }

        const bool take =
            have_period_ && level && index_ * 256u >= switch_q8_ &&
            index_ * 256u >= min_q8_ &&
            ones_run_ * 256u >= 2u * report_.period_q8;
        if (take) {
            report_.switched = true;
            report_.end = index_;
            report_.ones_run = ones_run_;
            report_.zeros_run = zeros_run_;
            report_.total_ones = total_ones_;
            report_.total_zeros = total_zeros_;
        }

        prev_ = level;
        ++index_;
        return take;
    }

    // Fill the report for a capture that ended without a take-over.
    void finish() {
        if (!report_.switched) {
            report_.end = index_ - 1u;
        }
        report_.ones_run = ones_run_;
        report_.zeros_run = zeros_run_;
        report_.total_ones = total_ones_;
        report_.total_zeros = total_zeros_;
    }

    const CaptureReport& report() const { return report_; }

private:
    enum : uint32_t {
        kStartEdges = 8,
        kPeriodBits = 9,
        kFrameBits = 80,
        kMinEndBits = 82,
    };

    uint32_t nominal_q8_;
    uint32_t index_ = 0;
    uint32_t edges_ = 0;
    uint32_t edge_index_ = 0;
    uint32_t ones_run_ = 0;
    uint32_t zeros_run_ = 0;
    uint32_t total_ones_ = 0;
    uint32_t total_zeros_ = 0;
    uint32_t switch_q8_ = 0;
    uint32_t min_q8_ = 0;
    bool prev_ = true;
    bool seen_high_ = false;
    bool started_ = false;
    bool have_period_ = false;
    CaptureReport report_ = {};
};

}  // namespace tmc_uart_bits
