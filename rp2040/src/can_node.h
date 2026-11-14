#pragma once

#include <stdint.h>

namespace can_node {

// Standard 11-bit identifier layout from doc/Fly_Micro4_addressing.md.
// [10:4] field, [3:0] type.
inline constexpr uint8_t field(uint16_t id) { return (id >> 4) & 0x7F; }
inline constexpr uint8_t type(uint16_t id) { return id & 0x0F; }

enum Type : uint8_t {
    kMotion = 0,
    kSync = 2,
    kStatus = 3,
    kCommand = 4,
    kHome = 5,
    kParameter = 6,
    kFault = 7,
    kDisc = 8,
    kAssign = 10,
    kAsk = 11,
    kVersionReq = 12,
    kVersionRsp = 13,
    kHealth = 14,
};

struct Frame {
    uint16_t id;
    uint8_t dlc;
    uint8_t data[8];
};

struct Stats {
    uint32_t rx;
    uint32_t tx;
    uint32_t error;
    uint32_t dropped;
    uint32_t nack;
};

// One ACK-slot measurement taken from the CAN2040-Lite edge-pair record.
// Times are in the receiver's two-cycle tick domain; the 1/16-tick fields
// keep the fractional positions without floating point.
struct AckSample {
    bool valid;
    bool acknowledged;
    uint16_t rx_id;
    uint32_t stuffed_bits;
    uint32_t bit_ticks;
    int32_t phase_16;        // ACK fall vs ideal slot + 0.25 bit
    int32_t start_margin_16; // sample point vs ACK fall
    int32_t end_margin_16;   // ACK rise vs sample point
    int32_t width_16;        // ACK dominant width
};

void begin();
bool ready();
int begin_stage();
bool poll(Frame* out);
Stats stats();

// Discovery state and transmit. board_id is -1 until ASSIGN matches this
// board's flash id. tick() services the delayed DISC advertisement.
bool assigned();
int assigned_board_id();
bool send(uint16_t id, const uint8_t data[8], uint8_t dlc);
void tick();

// Transmit queue/in-flight state from the CAN2040-Lite statistics.
bool tx_idle();

// Print the driver statistics and the application counters.
void print_stats();

// Receiver edge-timestamp ticks per CAN bit (2 PIO cycles per tick).
uint32_t bit_ticks();

// Raw pin/edge observation for bring-up. sample_rx_edges() polls the RX pad
// for `ms` milliseconds and returns the number of level changes.
int rx_level();
int tx_level();
uint32_t sample_rx_edges(uint32_t ms);
uint32_t irq_count();
uint32_t irq_us_max();
uint32_t irq_us_sum();
uint32_t arm_count();
uint32_t pio_fdebug();
void pio_fdebug_clear();

// Bench diagnostics. recording_enabled() is false on a production image.
bool recording_enabled();
void record_clear();
// Request a clean RX re-arm on core 1 (flushes the PIO FIFO and parser).
void request_rx_arm();
// Core 1 service hook for the request. Call from loop1().
void core1_tick();
void record_print();
// Wait for the next received frame and measure the ACK pulse it contains.
// expected_id is masked by 0xFFF; use 0xFFFF to accept any frame. The
// measurement operates on the RX edge-pair log, so it reports the ACK that
// this node generated (when it is the receiver).
bool capture_ack(uint16_t expected_id, uint32_t timeout_ms, AckSample* out);

// True once after a re-identify command addressed to this board (or to 127)
// cleared the RAM board ID. The caller stops the axes; the next ASK then
// re-advertises the board.
bool take_reidentify_request();

}  // namespace can_node
