#include "tmc_uart_bits.h"

namespace tmc_uart_bits {
namespace {

// Centre of bit position p, where p = 0 is the start bit of byte 0:
// start + (p + 0.5) * period. The period is Q8 samples, so the division is
// (2p + 1) * period_q8 / 512.
bool decode_at(const uint8_t* samples, uint32_t count, uint32_t start,
               uint32_t period_q8, uint8_t out[kReplyBytes]) {
    for (uint32_t n = 0; n < kReplyBytes; ++n) {
        uint8_t value = 0;
        for (uint32_t bit = 0; bit < 8; ++bit) {
            const uint32_t position = n * 10u + 1u + bit;
            const uint32_t index =
                start + ((2u * position + 1u) * period_q8) / 512u;
            if (index >= count) {
                return false;
            }
            if (level(samples, index)) {
                value |= (uint8_t)(1u << bit);
            }
        }
        out[n] = value;
    }
    return true;
}

bool try_frame(const uint8_t* samples, uint32_t count, uint32_t start,
               uint32_t period_q8, uint8_t out[kReplyBytes]) {
    if (!decode_at(samples, count, start, period_q8, out)) {
        return false;
    }
    return out[0] == kSync && crc8(out, kReplyBytes - 1) == out[kReplyBytes - 1];
}

}  // namespace

uint8_t crc8(const uint8_t* data, uint32_t len) {
    uint8_t crc = 0;
    for (uint32_t n = 0; n < len; ++n) {
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

uint32_t encode(uint8_t address, uint8_t reg, uint32_t value, bool write,
                uint8_t out[kReplyBytes]) {
    out[0] = kSync;
    out[1] = address;
    out[2] = write ? (uint8_t)(reg | 0x80u) : reg;
    if (!write) {
        out[3] = crc8(out, 3);
        return 4;
    }
    out[3] = (uint8_t)(value >> 24);
    out[4] = (uint8_t)(value >> 16);
    out[5] = (uint8_t)(value >> 8);
    out[6] = (uint8_t)value;
    out[7] = crc8(out, 7);
    return kReplyBytes;
}

bool level(const uint8_t* samples, uint32_t index) {
    return ((samples[index >> 3] >> (index & 7u)) & 1u) != 0;
}

bool decode_reply(const uint8_t* samples, uint32_t count,
                  uint32_t nominal_period_q8, Frame* out) {
    if (samples == nullptr || nominal_period_q8 == 0) {
        return false;
    }

    // The TMC re-times its reply from our datagram, so the measured bit period
    // is close to but not exactly the nominal one. Cover -15%..+6% in ~0.4%
    // steps; the CRC decides which candidate is right. The fine step matters:
    // the CRC only fits in a narrow window once the frame is 80 bits long.
    const uint32_t lo = (nominal_period_q8 * 85u) / 100u;
    const uint32_t hi = (nominal_period_q8 * 106u) / 100u;
    uint32_t step = nominal_period_q8 / 256u;
    if (step == 0) {
        step = 1;
    }

    uint8_t candidate[kReplyBytes];
    for (uint32_t start = 0; start < count; ++start) {
        // The reply starts on a 1->0 edge: the start bit of the 0x05 sync.
        // Sample 0 is the transmit release, so it may itself be low.
        const bool falling = (start == 0)
                                 ? !level(samples, 0)
                                 : (level(samples, start - 1u) &&
                                    !level(samples, start));
        if (!falling) {
            continue;
        }
        for (uint32_t period = lo; period <= hi; period += step) {
            if (!try_frame(samples, count, start, period, candidate)) {
                continue;
            }
            if (out != nullptr) {
                for (uint32_t i = 0; i < kReplyBytes; ++i) {
                    out->bytes[i] = candidate[i];
                }
                out->start = start;
                out->period_q8 = period;
            }
            return true;
        }
    }
    return false;
}

}  // namespace tmc_uart_bits
