/*
 * can_monitor — non-blocking CAN scope for the operator panel.
 *
 * Concept adapted from ESP32_projects/ESP32/28_hauszentrale/src/can_monitor.cpp:
 * a lock-free SPSC ring between the TWAI RX callback and one consumer task,
 * plus a per-identifier table with change and inter-arrival statistics. The
 * house project's SML reassembly and ADC-wave decoding are intentionally not
 * copied; this panel sees motion, sync, status, command, home and discovery
 * frames only.
 *
 * Threading:
 *   producer  one CAN RX callback (single writer)
 *   consumer  can_monitor consumer task (single reader of the ring)
 *   renderer  copies entries under a short critical section
 */

#ifndef CAN_MONITOR_H
#define CAN_MONITOR_H

#include <stdbool.h>
#include <stdint.h>

#include "can_transport.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef SS_CAN_MONITOR_MAX_IDS
#define SS_CAN_MONITOR_MAX_IDS 32
#endif
#ifndef SS_CAN_MONITOR_RING_SLOTS
#define SS_CAN_MONITOR_RING_SLOTS 128
#endif

typedef struct {
  bool used;
  uint32_t identifier;
  bool is_extended;
  uint8_t data_len;
  uint8_t data[8];
  uint8_t last_data[8];
  uint32_t last_timestamp_ms;
  bool data_changed;
  uint32_t frame_count;
  uint32_t changed_count;
  uint32_t interval_ms;
  uint32_t interval_min_ms;
  uint32_t interval_max_ms;
} ss_can_id_entry_t;

typedef struct {
  uint32_t rx_frames;
  uint32_t dropped;
  uint32_t unique_ids;
  uint32_t rate_1s;
} ss_can_monitor_counters_t;

/* Create the consumer task. Idempotent. Call before the transport starts. */
void can_monitor_start(void);

/* Producer side: enqueue one received frame. Never blocks. */
void can_monitor_push(const ss_can_frame_t *frame);

/* Copy one row. Returns false for an unused index. */
bool can_monitor_get_entry(int index, ss_can_id_entry_t *out);
int can_monitor_count(void);

const ss_can_monitor_counters_t *can_monitor_counters(void);

/* Drop all rows and counters (operator Clear). */
void can_monitor_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* CAN_MONITOR_H */
