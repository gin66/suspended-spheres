#include "can_transport.h"

#ifdef SS_CAN_MOCK

#include <Arduino.h>

#include <cstring>

namespace {
ss_can_stats_t s_stats;
ss_can_rx_fn s_cb = nullptr;
void *s_ctx = nullptr;
bool s_online = true;
}  // namespace

bool can_transport_start(uint32_t bitrate, ss_can_rx_fn cb, void *ctx) {
  (void)bitrate;
  std::memset(&s_stats, 0, sizeof(s_stats));
  s_cb = cb;
  s_ctx = ctx;
  s_online = true;
  s_stats.running = true;
  s_stats.mock = true;
  return true;
}

void can_transport_stop(void) {
  s_stats.running = false;
  s_cb = nullptr;
  s_ctx = nullptr;
}

bool can_transport_running(void) { return s_stats.running; }

bool can_transport_send(const ss_can_frame_t *frame) {
  if (!s_stats.running || frame == nullptr || !s_online) {
    s_stats.tx_failed++;
    return false;
  }
  s_stats.tx_frames++;
  return true;
}

void can_transport_get_stats(ss_can_stats_t *out) {
  if (out != nullptr) *out = s_stats;
}

bool can_transport_is_mock(void) { return true; }

void can_transport_mock_inject(const ss_can_frame_t *frame) {
  if (!s_stats.running || frame == nullptr || s_cb == nullptr) {
    s_stats.rx_dropped++;
    return;
  }
  ss_can_frame_t copy = *frame;
  if (copy.timestamp_ms == 0) copy.timestamp_ms = millis();
  s_stats.rx_frames++;
  s_cb(&copy, s_ctx);
}

void can_transport_mock_set_online(bool online) { s_online = online; }

#endif /* SS_CAN_MOCK */
