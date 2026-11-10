/* test_tmc_uart_bits.cpp — bit-banged TMC2209 reply decoder.
 *
 * The patterns are the 640-sample (1 us grid, 5x oversampled at 200 kBaud)
 * captures printed by `bbprobe`. Each decodes to the same default GCONF
 * reply 05 FF 00 00 00 01 01 BB; the fitted bit period is below the 5.00 us
 * nominal, which is what made the old 3-bit estimate walk off the CRC.
 */

#include "assert.h"
#include "test_list.h"
#include "tmc_uart_bits.h"

#include <string.h>

static const char* kAddr3 =
    "11111111111111110000011111000001111100000000000000000000000001111100000111111111111111111111111111111111111111111110000000000000"
    "00000000000000000000000000000001111100000000000000000000000000000000000000000000011111000000000000000000000000000000000000000000"
    "00111110000011111000000000000000000000000000000000011111000001111100000000000000000000000000000000000111110000111111111100000111"
    "11111111111100000111111111111111111111111111111000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000";

static const char* kAddr1 =
    "11111111111110000011111000001111000000000000000000000000111110000011111111111111111111111111111111111111111110000000000000000000"
    "00000000000000000000000111110000000000000000000000000000000000000000000111110000000000000000000000000000000000000000001111100000"
    "11110000000000000000000000000000000000111100000111110000000000000000000000000000000001111100000111111111000001111111111111100000"
    "11111111111111111111111111111000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000";

static const uint8_t kExpected[8] = {0x05, 0xFF, 0x00, 0x00,
                                     0x00, 0x01, 0x01, 0xBB};

static uint32_t pack(const char* text, uint8_t* samples, uint32_t capacity) {
    const uint32_t len = (uint32_t)strlen(text);
    const uint32_t bytes = (len + 7u) / 8u;
    if (bytes > capacity) {
        return 0;
    }
    memset(samples, 0, bytes);
    for (uint32_t i = 0; i < len; ++i) {
        if (text[i] == '1') {
            samples[i >> 3] |= (uint8_t)(1u << (i & 7u));
        }
    }
    return len;
}

static int check_pattern(const char* text) {
    uint8_t samples[128];
    const uint32_t count = pack(text, samples, sizeof(samples));
    ASSERT(count == 640);

    tmc_uart_bits::Frame frame;
    ASSERT(tmc_uart_bits::decode_reply(samples, count, 5u * 256u, &frame));
    ASSERT(memcmp(frame.bytes, kExpected, sizeof(kExpected)) == 0);

    /* The TMC replies faster than our 5 us nominal: the CRC only fits with a
     * period of roughly 4.7-4.9 us. Guard that the decoder does not silently
     * accept a different alignment. */
    ASSERT(frame.period_q8 >= 1150u && frame.period_q8 <= 1280u);
    ASSERT(tmc_uart_bits::crc8(frame.bytes, 7) == frame.bytes[7]);
    return 0;
}

static int test_bb1_decode_addr3(void) {
    return check_pattern(kAddr3);
}

static int test_bb2_decode_addr1(void) {
    return check_pattern(kAddr1);
}

static int test_bb3_crc_and_encode(void) {
    uint8_t request[8];

    ASSERT(tmc_uart_bits::encode(0, 0x00, 0, false, request) == 4);
    ASSERT(request[0] == 0x05 && request[1] == 0x00 && request[2] == 0x00);
    ASSERT(request[3] == 0x48);   /* crc8(05 00 00) */

    ASSERT(tmc_uart_bits::encode(1, 0x00, 0, false, request) == 4);
    ASSERT(request[3] == 0xFE);   /* crc8(05 01 00) */

    ASSERT(tmc_uart_bits::encode(0, 0x03, 0x0200, true, request) == 8);
    ASSERT(request[0] == 0x05 && request[1] == 0x00);
    ASSERT(request[2] == 0x83);   /* SLAVECONF | write */
    ASSERT(request[3] == 0x00 && request[4] == 0x00);
    ASSERT(request[5] == 0x02 && request[6] == 0x00);
    ASSERT(request[7] == tmc_uart_bits::crc8(request, 7));
    return 0;
}

static int test_bb4_reject_silence_and_truncation(void) {
    uint8_t samples[128];
    tmc_uart_bits::Frame frame;

    memset(samples, 0, sizeof(samples));
    ASSERT(!tmc_uart_bits::decode_reply(samples, 640, 5u * 256u, &frame));

    /* A real capture cut off before the CRC cannot produce a valid frame. */
    uint8_t pattern[128];
    const uint32_t count = pack(kAddr3, pattern, sizeof(pattern));
    ASSERT(count == 640);
    ASSERT(!tmc_uart_bits::decode_reply(pattern, 200, 5u * 256u, &frame));
    return 0;
}

static int reg_bb1 = add_test("BB1", test_bb1_decode_addr3);
static int reg_bb2 = add_test("BB2", test_bb2_decode_addr1);
static int reg_bb3 = add_test("BB3", test_bb3_crc_and_encode);
static int reg_bb4 = add_test("BB4", test_bb4_reject_silence_and_truncation);
