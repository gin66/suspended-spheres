/*
 * can_transport — the one boundary between commissioning/show logic and the
 * physical CAN controller.
 *
 * The bench has no CAN wiring yet, so the default build selects the mock
 * (src/can_transport_mock.cpp). When the TJA1051 and the 1 Mbit/s bus exist,
 * build without -DSS_CAN_MOCK and src/can_transport_twai.cpp takes over.
 * Nothing above this header may include driver/twai.h.
 */

#ifndef CAN_TRANSPORT_H
#define CAN_TRANSPORT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SS_CAN_DATA_LEN 8

typedef struct {
  uint32_t id;          /* 11-bit standard identifier only */
  uint8_t len;          /* 0..8 */
  bool extended;        /* always false for this bus */
  uint8_t data[SS_CAN_DATA_LEN];
  uint32_t timestamp_ms;
} ss_can_frame_t;

typedef struct {
  bool running;
  bool bus_off;
  bool mock;
  uint32_t rx_frames;
  uint32_t tx_frames;
  uint32_t rx_dropped;
  uint32_t tx_failed;
  uint32_t bus_errors;
  uint32_t tec;
  uint32_t rec;
  int state;
} ss_can_stats_t;

typedef void (*ss_can_rx_fn)(const ss_can_frame_t *frame, void *ctx);

bool can_transport_start(uint32_t bitrate, ss_can_rx_fn cb, void *ctx);
void can_transport_stop(void);
bool can_transport_running(void);
bool can_transport_send(const ss_can_frame_t *frame);
void can_transport_get_stats(ss_can_stats_t *out);
bool can_transport_is_mock(void);

#ifdef SS_CAN_MOCK
/* Test/mock feed. A frame is delivered synchronously to the registered RX
 * callback. `online` gates send() so the UI can show a dead bus. */
void can_transport_mock_inject(const ss_can_frame_t *frame);
void can_transport_mock_set_online(bool online);
#endif

#ifdef __cplusplus
}
#endif

#endif /* CAN_TRANSPORT_H */
