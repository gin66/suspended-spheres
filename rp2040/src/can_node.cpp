#include "can_node.h"

#include <Arduino.h>
#include <hardware/clocks.h>
#include <hardware/irq.h>
#include <hardware/pio.h>
#include <hardware/timer.h>
#include <pico/multicore.h>
#include <pico/stdlib.h>
#include <pico/unique_id.h>

#include "board_pins.h"
#include "motion_core.h"

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

uint8_t own_flash_id[8] = {};
volatile int board_id = -1;
volatile bool disc_pending = false;
volatile bool reidentify_requested = false;
volatile uint32_t disc_due_us = 0;

/* The ACK must be armed within about 7 µs at 1 Mbit/s. These two run inside
 * the can2040 IRQ, so they stay in RAM with can2040 itself. */
void __not_in_flash_func(on_can)(struct can2040*, uint32_t notify,
                                 struct can2040_msg* msg) {
    if (notify == CAN2040_NOTIFY_RX) {
        rx_count++;
        const uint16_t id = (uint16_t)msg->id;
        const uint8_t frame_type = type(id);

        if (frame_type == kAsk && field(id) == 0 && board_id < 0) {
            /* The addressing document staggers advertisements by up to
             * 9 ms so unidentified boards do not all answer at once. */
            disc_due_us = time_us_32() + 1000u * (own_flash_id[0] % 10u);
            disc_pending = true;
        } else if (frame_type == kAssign) {
            int assigned = board_id;
            if (mc_assign_board(&assigned, field(id), msg->data,
                                own_flash_id) == 0) {
                board_id = assigned;
                disc_pending = false;
            }
        } else if (frame_type == kCommand && msg->dlc >= 1 &&
                   msg->data[0] == MC_CMD_REIDENTIFY &&
                   (field(id) == MC_BOARD_ALL || (int)field(id) == board_id)) {
            /* The master reboots into an empty node table, or has a board ID
             * it cannot map. Dropping the RAM ID here, in the receive path,
             * lets the ASK that follows re-discover this board. The main
             * loop stops the axes. */
            board_id = -1;
            disc_pending = false;
            reidentify_requested = true;
        }

        uint8_t next = (uint8_t)((head + 1) % kQueueLength);
        if (next == tail) {
            drop_count++;
            return;
        }
        queue[head].id = id;
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

void __not_in_flash_func(pio1_irq)() {
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

    pico_unique_board_id_t uid;
    pico_get_unique_board_id(&uid);
    for (uint8_t i = 0; i < 8; i++) {
        own_flash_id[i] = uid.id[i];
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

bool assigned() {
    return assigned_board_id() >= 0;
}

int assigned_board_id() {
    int id = board_id;
    __dmb();
    return id;
}

bool send(uint16_t id, const uint8_t data[8], uint8_t dlc) {
    struct can2040_msg msg;
    msg.id = id;
    msg.dlc = dlc > 8 ? 8 : dlc;
    for (uint8_t i = 0; i < 8; i++) {
        msg.data[i] = i < msg.dlc ? data[i] : 0;
    }
    return can2040_transmit(&cbus, &msg) == 0;
}

void tick() {
    if (!disc_pending || board_id >= 0) {
        return;
    }
    if ((int32_t)(time_us_32() - disc_due_us) < 0) {
        return;
    }

    uint8_t payload[8];
    mc_pack_flash_id(own_flash_id, payload);
    if (send((uint16_t)mc_id_make(0, kDisc), payload, 8)) {
        disc_pending = false;
    } else {
        disc_due_us = time_us_32() + 1000u;
    }
}

bool take_reidentify_request() {
    if (!reidentify_requested) {
        return false;
    }
    reidentify_requested = false;
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
