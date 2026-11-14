#include "can_node.h"

#include <Arduino.h>
#include <hardware/clocks.h>
#include <hardware/gpio.h>
#include <hardware/irq.h>
#include <hardware/pio.h>
#include <hardware/structs/sio.h>
#include <hardware/timer.h>
#include <pico/multicore.h>
#include <pico/stdlib.h>
#include <pico/unique_id.h>

#include "board_pins.h"
#include "motion_core.h"

extern "C" {
#include "can2040.h"
#include "can2040_lite_backend.h"
}

#include "pico/can2040_lite_pico.h"

namespace can_node {
namespace {

constexpr uint8_t kQueueLength = 16;
#ifndef SS_CAN_BITRATE
#define SS_CAN_BITRATE 1000000u
#endif
#ifndef SS_CAN_RX_ONLY
#define SS_CAN_RX_ONLY 0
#endif
#ifndef SS_CAN_ISR_PROBE
#define SS_CAN_ISR_PROBE 0
#endif
// timer_hw->timerawl sampling inside the IRQ. Set to 0 for a clean logic-
// analyzer probe of the handler, because the two timer reads and the sum/max
// update otherwise count toward the measured high time.
#ifndef SS_CAN_ISR_TIMING
#define SS_CAN_ISR_TIMING 1
#endif
constexpr uint32_t kBitrate = SS_CAN_BITRATE;

#if SS_CAN_ISR_PROBE
constexpr uint8_t kIsrProbePin = SS_CAN_ISR_PROBE;
constexpr uint32_t kIsrProbeMask = 1u << kIsrProbePin;
#endif

struct can2040 cbus;
Frame queue[kQueueLength];
volatile uint8_t head = 0;
volatile uint8_t tail = 0;
volatile uint32_t rx_count = 0;
volatile uint32_t tx_count = 0;
volatile uint32_t error_count = 0;
volatile uint32_t drop_count = 0;
volatile bool ready_flag = false;
volatile uint8_t s_begin_stage = 0;
volatile uint32_t s_irq_count = 0;
volatile uint32_t s_irq_us_max = 0;
volatile uint32_t s_irq_us_sum = 0;
volatile bool s_arm_requested = false;
volatile uint32_t s_arm_count = 0;

uint8_t own_flash_id[8] = {};
volatile int board_id = -1;
volatile bool disc_pending = false;
volatile bool reidentify_requested = false;
volatile uint32_t disc_due_us = 0;

/* The PIO IRQ keeps only the FIFO drain and inline edge parser.  This
 * callback runs from the main-loop portable service on the application core,
 * so the queue has a single consumer there. */
void __not_in_flash_func(on_can)(struct can2040*, uint32_t notify,
                                 struct can2040_msg* msg) {
    if (notify == CAN2040_NOTIFY_RX) {
        rx_count++;
        const uint16_t id = (uint16_t)msg->id;
        const uint8_t frame_type = type(id);

        if (SS_CAN_RX_ONLY) {
            /* receive-only bring-up: never answer discovery or commands */
        } else if (frame_type == kAsk && field(id) == 0 && board_id < 0) {
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

// Production message processor.  The fixed backend only reports raw events;
// queue advance, TX/ACK matching and statistics live here in the application.
void tx_advance_locked(struct can2040* cd) {
    cd->tx_head = (cd->tx_head + 1u) % CAN2040_LITE_TX_QUEUE_LEN;
    cd->tx_count--;
    cd->tx_active = 0;
}

void tx_kick(struct can2040* cd) {
    const uint32_t save = can2040_lite_critical_enter();
    if (!cd->tx_active && cd->tx_count > 0) {
        const uint32_t slot = cd->tx_head;
        if (can2040_lite_backend_tx_frame(cd->tx_bits[slot],
                                          cd->tx_nbits[slot]) == 0) {
            cd->tx_active = 1;
        }
    }
    can2040_lite_critical_exit(save);
}

bool tx_matches(const struct can2040* cd, const can_parsed_t* p) {
    if (!cd->tx_active || cd->tx_count == 0) {
        return false;
    }
    uint32_t id = p->id;
    if (p->is_extended) {
        id |= CAN2040_LITE_ID_EFF;
    }
    if (p->is_rtr) {
        id |= CAN2040_LITE_ID_RTR;
    }
    return id == cd->tx_msg[cd->tx_head].id;
}

void msg_from_parsed(const can_parsed_t* p, struct can2040_msg* msg) {
    msg->id = p->id;
    if (p->is_extended) {
        msg->id |= CAN2040_LITE_ID_EFF;
    }
    if (p->is_rtr) {
        msg->id |= CAN2040_LITE_ID_RTR;
    }
    msg->dlc = p->dlc;
    for (uint8_t i = 0; i < 8; i++) {
        msg->data[i] = p->data[i];
    }
}

void service_events() {
    struct can2040_lite_event ev;
    while (can2040_lite_backend_poll(&ev)) {
        if (ev.kind == 1) {
            if (ev.tx_error) {
                const uint32_t save = can2040_lite_critical_enter();
                const bool complete = cbus.tx_active;
                if (complete) {
                    cbus.tx_done_msg = cbus.tx_msg[cbus.tx_head];
                    tx_advance_locked(&cbus);
                    cbus.stats.tx_error++;
                    cbus.stats.parse_error++;
                }
                can2040_lite_critical_exit(save);
                if (complete) {
                    tx_kick(&cbus);
                }
            }
            continue;
        }
        if (ev.kind != 2) {
            continue;
        }
        if (ev.rx.validity != VALID_FRAME) {
            const uint32_t save = can2040_lite_critical_enter();
            cbus.stats.parse_error++;
            if (ev.rx.validity == INVALID_CRC) {
                cbus.stats.crc_errors++;
            } else if (ev.rx.validity == INVALID_STUFF) {
                cbus.stats.stuff_errors++;
            } else {
                cbus.stats.bit_errors++;
            }
            can2040_lite_critical_exit(save);
            continue;
        }

        bool done_tx = false;
        bool ack = false;
        {
            const uint32_t save = can2040_lite_critical_enter();
            if (tx_matches(&cbus, &ev.rx)) {
                done_tx = true;
                ack = ev.rx.ack != 0;
                cbus.tx_done_msg = cbus.tx_msg[cbus.tx_head];
                tx_advance_locked(&cbus);
                if (ack) {
                    cbus.stats.tx_total++;
                } else {
                    cbus.stats.tx_nack++;
                    cbus.stats.parse_error++;
                }
            } else {
                cbus.stats.rx_total++;
            }
            can2040_lite_critical_exit(save);
        }

        if (done_tx) {
            if (ack) {
                on_can(&cbus, CAN2040_NOTIFY_TX, &cbus.tx_done_msg);
            }
            tx_kick(&cbus);
        } else {
            struct can2040_msg msg;
            msg_from_parsed(&ev.rx, &msg);
            on_can(&cbus, CAN2040_NOTIFY_RX, &msg);
        }
    }
}

void __not_in_flash_func(pio1_irq)() {
#if SS_CAN_ISR_PROBE
    sio_hw->gpio_set = kIsrProbeMask;
#endif
#if SS_CAN_ISR_TIMING
    const uint32_t t0 = timer_hw->timerawl;
#endif
    s_irq_count++;
    can2040_pio_irq_handler(&cbus);
#if SS_CAN_ISR_TIMING
    const uint32_t dt = timer_hw->timerawl - t0;
    s_irq_us_sum += dt;
    if (dt > s_irq_us_max) {
        s_irq_us_max = dt;
    }
#endif
#if SS_CAN_ISR_PROBE
    sio_hw->gpio_clr = kIsrProbeMask;
#endif
}

}  // namespace

void begin() {
    if (get_core_num() != 1) {
        panic("can_node::begin must run on core 1");
    }

    s_begin_stage = 1;
    can2040_setup(&cbus, 1);
    s_begin_stage = 2;

    pico_unique_board_id_t uid;
    pico_get_unique_board_id(&uid);
    for (uint8_t i = 0; i < 8; i++) {
        own_flash_id[i] = uid.id[i];
    }

    s_begin_stage = 3;
#if SS_CAN_ISR_PROBE
    gpio_init(kIsrProbePin);
    gpio_set_dir(kIsrProbePin, GPIO_OUT);
    gpio_put(kIsrProbePin, 0);
#endif
    irq_set_exclusive_handler(PIO1_IRQ_0, pio1_irq);
    irq_set_priority(PIO1_IRQ_0, 1);
    irq_set_enabled(PIO1_IRQ_0, true);

    s_begin_stage = 4;
    can2040_start(&cbus, clock_get_hz(clk_sys), kBitrate, board::kCanRxPin,
                  board::kCanTxPin);
    s_begin_stage = 5;

    // can2040-lite claims its rx and tx state machines. The block's remaining
    // state machines still belong to CAN, so claim them before core 0 starts
    // FastAccelStepper and its allocator could select PIO1.
    for (uint sm = 0; sm < NUM_PIO_STATE_MACHINES; sm++) {
        if (!pio_sm_is_claimed(pio1, sm)) {
            pio_sm_claim(pio1, sm);
        }
    }

    s_begin_stage = 6;
    __dmb();
    ready_flag = true;
}

bool ready() {
    return ready_flag;
}

int begin_stage() {
    return s_begin_stage;
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
    if (SS_CAN_RX_ONLY) {
        return false;
    }
    struct can2040_msg msg;
    msg.id = id;
    msg.dlc = dlc > 8 ? 8 : dlc;
    for (uint8_t i = 0; i < 8; i++) {
        msg.data[i] = i < msg.dlc ? data[i] : 0;
    }
    return can2040_transmit(&cbus, &msg) == 0;
}

void tick() {
    // The PIO IRQ only drains the FIFO and parses edges.  Convert the parsed
    // ring into callbacks and service the quiet CRC tail from the main loop.
    if (ready_flag) {
        service_events();
    }

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
    // Frames whose ACK slot was recessive.  With ACK generation disabled this
    // is the missing-ACK count for the received traffic.
    s.nack = can2040_lite_pico_rx_ack_errors();
    return s;
}

bool tx_idle() {
    struct can2040_stats st;
    can2040_get_statistics(&cbus, &st);
    return st.tx_attempt == st.tx_total + st.tx_nack + st.tx_error;
}

void print_stats() {
    struct can2040_stats st;
    can2040_get_statistics(&cbus, &st);
    Serial.print("lite rx=");
    Serial.print(st.rx_total);
    Serial.print(" tx=");
    Serial.print(st.tx_total);
    Serial.print(" attempt=");
    Serial.print(st.tx_attempt);
    Serial.print(" nack=");
    Serial.print(st.tx_nack);
    Serial.print(" txerr=");
    Serial.print(st.tx_error);
    Serial.print(" parse_err=");
    Serial.print(st.parse_error);
    Serial.print(" crc=");
    Serial.print(st.crc_errors);
    Serial.print(" stuff=");
    Serial.print(st.stuff_errors);
    Serial.print(" bit=");
    Serial.print(st.bit_errors);
    Serial.print(" overflow=");
    Serial.println(st.rx_overflow);
    const Stats s = stats();
    Serial.print("app rx=");
    Serial.print(s.rx);
    Serial.print(" tx=");
    Serial.print(s.tx);
    Serial.print(" err=");
    Serial.print(s.error);
    Serial.print(" drop=");
    Serial.print(s.dropped);
    Serial.print(" nack=");
    Serial.println(s.nack);
}

uint32_t bit_ticks() {
    const uint32_t ticks = clock_get_hz(clk_sys) / (2u * kBitrate);
    return ticks == 0 ? 1u : ticks;
}

int rx_level() {
    return gpio_get(board::kCanRxPin) ? 1 : 0;
}

int tx_level() {
    return gpio_get(board::kCanTxPin) ? 1 : 0;
}

uint32_t irq_count() {
    return s_irq_count;
}

uint32_t irq_us_max() {
    return s_irq_us_max;
}

uint32_t irq_us_sum() {
    return s_irq_us_sum;
}

uint32_t arm_count() {
    return s_arm_count;
}

uint32_t pio_fdebug() {
    return pio1->fdebug;
}

void pio_fdebug_clear() {
    pio1->fdebug = 0xFFFFFFFFu;
    s_irq_count = 0;
    s_irq_us_max = 0;
    s_irq_us_sum = 0;
}

uint32_t sample_rx_edges(uint32_t ms) {
    uint32_t edges = 0;
    int prev = rx_level();
    const uint32_t start = millis();
    while ((uint32_t)(millis() - start) < ms) {
        const int level = rx_level();
        if (level != prev) {
            edges++;
            prev = level;
        }
    }
    return edges;
}

bool recording_enabled() {
    return false;
}

void record_clear() {}

void request_rx_arm() {
    s_arm_requested = true;
}

void core1_tick() {
    if (!s_arm_requested) {
        return;
    }
    s_arm_requested = false;
    s_arm_count++;
    can2040_lite_pico_rx_arm();
}

void record_print() {
    Serial.println("edge-pair recording removed in this can2040-lite version");
}

bool capture_ack(uint16_t, uint32_t, AckSample* out) {
    if (out != nullptr) {
        *out = AckSample{};
    }
    return false;
}

}  // namespace can_node
