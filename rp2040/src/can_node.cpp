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

#ifndef SS_FW_VERSION
#define SS_FW_VERSION "0.1.0"
#endif
#ifndef SS_BUILD_ID
#define SS_BUILD_ID 0
#endif

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
uint8_t own_uuid[MC_UUID_LEN] = {};
mc_version_t own_version = {};
volatile int board_id = -1;
volatile bool disc_pending = false;
volatile bool reidentify_requested = false;
volatile uint32_t disc_due_us = 0;
volatile bool version_pending = false;
volatile uint32_t version_due_us = 0;

/* Position map.  The IRQ only copies a PARAM frame; tick() validates it so
 * the 1 Mbit/s ACK path stays short. */
volatile int16_t s_position_field[MC_AXES_PER_NODE] = {-1, -1, -1, -1};
volatile uint8_t s_position[MC_AXES_PER_NODE] = {MC_POS_UNASSIGNED,
                                                 MC_POS_UNASSIGNED,
                                                 MC_POS_UNASSIGNED,
                                                 MC_POS_UNASSIGNED};
volatile bool s_posmap_pending = false;
volatile uint8_t s_posmap_data[8];
volatile uint32_t s_posmap_seq = 0;

void parse_version(void) {
    unsigned major = 0, minor = 0, patch = 0;
    (void)sscanf(SS_FW_VERSION, "%u.%u.%u", &major, &minor, &patch);
    own_version.major = (uint8_t)major;
    own_version.minor = (uint8_t)minor;
    own_version.patch = (uint8_t)patch;
    own_version.flags = 0;
#ifdef SS_FW_DIRTY
    own_version.flags |= MC_VERSION_FLAG_DIRTY;
#endif
    own_version.build = (uint32_t)SS_BUILD_ID;
}

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
            disc_due_us = time_us_32() + 1000u * (own_uuid[0] % 10u);
            disc_pending = true;
        } else if (frame_type == kAssign) {
            int assigned = board_id;
            if (mc_assign_board(&assigned, field(id), msg->data,
                                own_uuid) == 0) {
                board_id = assigned;
                disc_pending = false;
            }
        } else if (frame_type == kVersionReq) {
            /* One response per request.  A query to field 127 reaches every
             * board; field 127 is not copied into the response field. */
            int bid = board_id;
            if (bid >= 0 &&
                (field(id) == MC_BOARD_ALL || (int)field(id) == bid)) {
                version_due_us = time_us_32() +
                                 1000u * ((uint32_t)bid % 10u);
                version_pending = true;
            }
        } else if (frame_type == kParameter && msg->dlc >= 8 &&
                   msg->data[0] == MC_PARAM_POSMAP &&
                   (field(id) == MC_BOARD_ALL ||
                    (int)field(id) == board_id)) {
            /* Copy only; tick() validates and applies it outside the IRQ. */
            for (uint8_t i = 0; i < 8; i++)
                s_posmap_data[i] = msg->data[i];
            s_posmap_pending = true;
        } else if (frame_type == kCommand && msg->dlc >= 8 &&
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

        /* Drop the type-0 traffic of the other 119 positions here, before the
         * queue: the node hears every motion frame on the bus. */
        if (frame_type == kParameter) return;
        if (frame_type == kMotion) {
            const int f = (int)field(id);
            bool owned = false;
            for (uint8_t i = 0; i < MC_AXES_PER_NODE; i++)
                if (s_position_field[i] == f) owned = true;
            if (!owned) return;
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
    mc_uuid_from_flash_id(own_flash_id, own_uuid);
    parse_version();

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

bool owns_motion_field(int f) {
    return motor_for_field(f) >= 0;
}

int motor_for_field(int f) {
    for (uint8_t i = 0; i < MC_AXES_PER_NODE; i++)
        if (s_position_field[i] == f) return (int)i;
    return -1;
}

int position_for_axis(int axis) {
    if (axis < 0 || axis >= MC_AXES_PER_NODE) return -1;
    return (int)s_position[axis];
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

void apply_posmap(void) {
    if (!s_posmap_pending) return;
    uint8_t data[8];
    for (uint8_t i = 0; i < 8; i++) data[i] = s_posmap_data[i];
    __dmb();
    s_posmap_pending = false;

    mc_posmap_t map;
    if (mc_unpack_param_posmap(data, 8, &map) != 0) return;
    for (uint8_t i = 0; i < MC_AXES_PER_NODE; i++) {
        s_position[i] = map.position[i];
        s_position_field[i] =
            map.position[i] == MC_POS_UNASSIGNED
                ? -1
                : (int16_t)mc_position_field(map.position[i]);
    }
    s_posmap_seq = map.seq;
}

void tick() {
    apply_posmap();
    if (version_pending) {
        int bid = board_id;
        if ((int32_t)(time_us_32() - version_due_us) >= 0) {
            uint8_t payload[8];
            if (bid >= 0 && mc_pack_version_rsp(&own_version, payload) == 0) {
                int id = mc_id_make(bid, kVersionRsp);
                if (id >= 0 &&
                    send((uint16_t)id, payload, 8)) {
                    version_pending = false;
                }
            } else {
                version_pending = false;
            }
        }
    }

    if (!disc_pending || board_id >= 0) {
        return;
    }
    if ((int32_t)(time_us_32() - disc_due_us) < 0) {
        return;
    }

    uint8_t payload[8];
    mc_pack_uuid(own_uuid, payload);
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
