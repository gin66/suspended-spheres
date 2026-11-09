#include "can_node.h"

#include <Arduino.h>
#include <hardware/clocks.h>
#include <hardware/irq.h>
#include <hardware/pio.h>
#include <pico/multicore.h>
#include <pico/stdlib.h>

#include "board_pins.h"

extern "C" {
#include "can2040.h"
}

namespace can_node {
namespace {

constexpr uint8_t kQueueLength = 16;
constexpr uint32_t kBitrate = 1000000;

struct can2040 cbus;
Frame queue[kQueueLength];
volatile uint8_t head = 0;
volatile uint8_t tail = 0;
volatile uint32_t rx_count = 0;
volatile uint32_t tx_count = 0;
volatile uint32_t error_count = 0;
volatile uint32_t drop_count = 0;
volatile bool ready_flag = false;

void on_can(struct can2040*, uint32_t notify, struct can2040_msg* msg) {
    if (notify == CAN2040_NOTIFY_RX) {
        rx_count++;
        uint8_t next = (uint8_t)((head + 1) % kQueueLength);
        if (next == tail) {
            drop_count++;
            return;
        }
        queue[head].id = (uint16_t)msg->id;
        queue[head].dlc = msg->dlc > 8 ? 8 : msg->dlc;
        for (uint8_t i = 0; i < 8; i++) {
            queue[head].data[i] = msg->data[i];
        }
        __dmb();
        head = next;
    } else if (notify == CAN2040_NOTIFY_TX) {
        tx_count++;
    } else {
        error_count++;
    }
}

void pio1_irq() {
    can2040_pio_irq_handler(&cbus);
}

}  // namespace

void begin() {
    if (get_core_num() != 1) {
        panic("can_node::begin must run on core 1");
    }

    can2040_setup(&cbus, 1);

    // can2040 uses raw PIO registers and does not claim its state machines.
    // Claim all four so FastAccelStepper's allocator cannot reuse the block.
    for (uint sm = 0; sm < NUM_PIO_STATE_MACHINES; sm++) {
        pio_sm_claim(pio1, sm);
    }

    can2040_callback_config(&cbus, on_can);
    irq_set_exclusive_handler(PIO1_IRQ_0, pio1_irq);
    irq_set_priority(PIO1_IRQ_0, 1);
    irq_set_enabled(PIO1_IRQ_0, true);

    can2040_start(&cbus, clock_get_hz(clk_sys), kBitrate, board::kCanRxPin,
                  board::kCanTxPin);

    __dmb();
    ready_flag = true;
}

bool ready() {
    return ready_flag;
}

bool poll(Frame* out) {
    if (tail == head) {
        return false;
    }
    __dmb();
    *out = queue[tail];
    tail = (uint8_t)((tail + 1) % kQueueLength);
    return true;
}

Stats stats() {
    // The counters are updated by the core 1 IRQ and read here on core 0.
    // Each 32-bit read is atomic; the four values need not share an instant.
    Stats s;
    s.rx = rx_count;
    s.tx = tx_count;
    s.error = error_count;
    s.dropped = drop_count;
    return s;
}

}  // namespace can_node
