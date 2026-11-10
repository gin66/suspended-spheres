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

/* A valid reply whose bit period is slightly slower than the 4 us nominal.
 * The 6th-edge estimate came out at 4.39 us and the tracker missed the
 * release, so this is the regression case for the end detection. */
static const char* kSlowReply =
    "11111111111110000011110000011110000000000000000000001111100011111111111111111111111111111111111110000000000000000000000000000000"
    "00000111100000000000000000000000000000000000001111000000000000000000000000000000000000111100001111000000000000000000000000000001"
    "11100001111000000000000000000000000000011110000111111110000111111111111000011111111111111111111111110000000000000000000000000000"
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

static void set_level(uint8_t* samples, uint32_t index, bool high) {
    if (high) {
        samples[index >> 3] |= (uint8_t)(1u << (index & 7u));
    } else {
        samples[index >> 3] &= (uint8_t)~(1u << (index & 7u));
    }
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

static int check_tracker(const char* text, uint32_t expected_start) {
    uint8_t samples[128];
    const uint32_t count = pack(text, samples, sizeof(samples));
    ASSERT(count == 640);

    tmc_uart_bits::CaptureTracker tracker(5u * 256u);
    uint32_t switched_at = count;
    for (uint32_t i = 0; i < count; ++i) {
        if (tracker.feed(tmc_uart_bits::level(samples, i))) {
            switched_at = i;
            break;
        }
    }

    const tmc_uart_bits::CaptureReport& report = tracker.report();
    ASSERT(report.start == expected_start);
    ASSERT(report.switched);
    ASSERT(switched_at == report.end);
    ASSERT(report.period_q8 >= 1150u && report.period_q8 <= 1280u);
    /* The switch only happens while the line is high. */
    ASSERT(tmc_uart_bits::level(samples, report.end));
    /* After the 80-bit frame, before the end of the capture. */
    ASSERT(report.end - report.start >= 360u);
    ASSERT(report.end < count - 1u);
    ASSERT(report.total_ones + report.total_zeros ==
           report.end - report.start + 1u);
    return 0;
}

static int test_bb5_tracker_addr3(void) {
    return check_tracker(kAddr3, 16);
}

static int test_bb6_tracker_addr1(void) {
    return check_tracker(kAddr1, 13);
}

static int test_bb7_tracker_no_reply(void) {
    tmc_uart_bits::CaptureTracker tracker(5u * 256u);
    for (uint32_t i = 0; i < 640; ++i) {
        ASSERT(!tracker.feed(false));
    }
    ASSERT(!tracker.report().switched);
    return 0;
}

static int test_bb8_tracker_truncated(void) {
    uint8_t samples[128];
    const uint32_t count = pack(kAddr3, samples, sizeof(samples));
    ASSERT(count == 640);

    tmc_uart_bits::CaptureTracker tracker(5u * 256u);
    for (uint32_t i = 0; i < 200; ++i) {
        ASSERT(!tracker.feed(tmc_uart_bits::level(samples, i)));
    }
    ASSERT(!tracker.report().switched);
    return 0;
}

static int test_bb9_tracker_slow_reply(void) {
    uint8_t samples[128];
    const uint32_t count = pack(kSlowReply, samples, sizeof(samples));
    ASSERT(count == 512);

    tmc_uart_bits::Frame frame;
    ASSERT(tmc_uart_bits::decode_reply(samples, count, 4u * 256u, &frame));
    ASSERT(frame.start == 13);
    ASSERT(frame.period_q8 >= 1000u && frame.period_q8 <= 1080u);

    tmc_uart_bits::CaptureTracker tracker(4u * 256u);
    uint32_t switched_at = count;
    for (uint32_t i = 0; i < count; ++i) {
        if (tracker.feed(tmc_uart_bits::level(samples, i))) {
            switched_at = i;
            break;
        }
    }

    const tmc_uart_bits::CaptureReport& report = tracker.report();
    ASSERT(report.start == 13);
    ASSERT(report.switched);
    ASSERT(switched_at == report.end);
    ASSERT(report.period_q8 >= 1000u && report.period_q8 <= 1080u);
    ASSERT(tmc_uart_bits::level(samples, report.end));
    ASSERT(report.end < count - 1u);
    return 0;
}

/* A false start plus a too-fast "reply" period still passes the 85..115 %
 * sanity window. The take-over must wait for 102.5 % of the frame (82 bits)
 * and two high bits, so it lands in the postamble and never in the data. For
 * this synthetic frame (start 1, period 3.44 us) that is sample 284. */
static int test_bb10_tracker_early_guard(void) {
    uint8_t samples[128];
    memset(samples, 0, sizeof(samples));

    for (uint32_t i = 0; i < 512; ++i) {
        set_level(samples, i, true);
    }
    /* start bit at 1; edges at 5, 9, 14, 18, 39, 44, 48, 79 */
    for (uint32_t i = 1; i <= 4; ++i) set_level(samples, i, false);
    for (uint32_t i = 5; i <= 8; ++i) set_level(samples, i, true);
    for (uint32_t i = 9; i <= 13; ++i) set_level(samples, i, false);
    for (uint32_t i = 14; i <= 17; ++i) set_level(samples, i, true);
    for (uint32_t i = 18; i <= 38; ++i) set_level(samples, i, false);
    for (uint32_t i = 39; i <= 43; ++i) set_level(samples, i, true);
    for (uint32_t i = 44; i <= 47; ++i) set_level(samples, i, false);
    for (uint32_t i = 48; i <= 78; ++i) set_level(samples, i, true);
    set_level(samples, 79, false);
    for (uint32_t i = 80; i < 512; ++i) set_level(samples, i, true);

    tmc_uart_bits::CaptureTracker tracker(4u * 256u);
    uint32_t first = 512;
    for (uint32_t i = 0; i < 512; ++i) {
        if (tracker.feed(tmc_uart_bits::level(samples, i))) {
            first = i;
            break;
        }
    }

    ASSERT(first == 284u);
    ASSERT(tracker.report().switched);
    return 0;
}

static int reg_bb1 = add_test("BB1", test_bb1_decode_addr3);
static int reg_bb2 = add_test("BB2", test_bb2_decode_addr1);
static int reg_bb3 = add_test("BB3", test_bb3_crc_and_encode);
static int reg_bb4 = add_test("BB4", test_bb4_reject_silence_and_truncation);
static int reg_bb5 = add_test("BB5", test_bb5_tracker_addr3);
static int reg_bb6 = add_test("BB6", test_bb6_tracker_addr1);
static int reg_bb7 = add_test("BB7", test_bb7_tracker_no_reply);
static int reg_bb8 = add_test("BB8", test_bb8_tracker_truncated);
static int reg_bb9 = add_test("BB9", test_bb9_tracker_slow_reply);
static int reg_bb10 = add_test("BB10", test_bb10_tracker_early_guard);
