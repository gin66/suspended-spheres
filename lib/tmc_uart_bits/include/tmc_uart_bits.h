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

}  // namespace tmc_uart_bits
