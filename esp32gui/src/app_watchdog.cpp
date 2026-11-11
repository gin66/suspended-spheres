#include "app_watchdog.h"

#include <Arduino.h>
#include <esp_attr.h>
#include <esp_system.h>

#include <cstdio>
#include <cstring>

#include "app_log.h"

namespace {

constexpr uint32_t kRtcMagic = 0x53535744u;  /* "SSWD" */
constexpr uint32_t kGraceMs = 5000u;

RTC_DATA_ATTR uint32_t s_rtc_magic;
RTC_DATA_ATTR uint32_t s_rtc_boot_count;
RTC_DATA_ATTR uint16_t s_rtc_watchpoint;
RTC_DATA_ATTR uint16_t s_rtc_last_watchpoint;

app_watchdog_status_t s_status;
app_watchdog_hold_fn s_hold_fn = nullptr;
uint32_t s_last_kick[3];
uint32_t s_started_ms;
bool s_can_expected;

void raise_hold(const char *reason) {
  s_status.safe_hold = true;
  s_status.safe_hold_count++;
  std::snprintf(s_status.hold_reason, sizeof(s_status.hold_reason), "%s",
                reason != nullptr ? reason : "unspecified");
  SS_LOGE("watchdog", "SAFE HOLD: %s", s_status.hold_reason);
  if (s_hold_fn != nullptr) s_hold_fn(s_status.hold_reason);
}

bool stale(uint32_t now, uint32_t last, uint32_t timeout) {
  return (uint32_t)(now - last) > timeout;
}

}  // namespace

void app_watchdog_init(void) {
  if (s_rtc_magic != kRtcMagic) {
    s_rtc_magic = kRtcMagic;
    s_rtc_boot_count = 0;
    s_rtc_watchpoint = 0;
    s_rtc_last_watchpoint = 0;
  }

  s_rtc_boot_count++;
  s_rtc_last_watchpoint = s_rtc_watchpoint;
  s_rtc_watchpoint = 0;

  std::memset(&s_status, 0, sizeof(s_status));
  s_status.boot_count = s_rtc_boot_count;
  s_status.reset_reason = (uint32_t)esp_reset_reason();
  s_status.last_watchpoint = s_rtc_last_watchpoint;
  s_status.watchpoint = 0;
  s_status.can_link_ok = false;

  const uint32_t now = millis();
  for (uint32_t &i : s_last_kick) i = now;
  s_started_ms = now;
  s_can_expected = false;

  SS_LOGI("boot", "boot #%lu reset=%lu last_watchpoint=%u",
          (unsigned long)s_status.boot_count,
          (unsigned long)s_status.reset_reason,
          (unsigned)s_status.last_watchpoint);
}

void app_watchdog_set_watchpoint(uint16_t point) {
  s_rtc_watchpoint = point;
  s_status.watchpoint = point;
}

void app_watchdog_kick(int mask) {
  const uint32_t now = millis();
  if (mask & SS_WATCH_UI) {
    s_last_kick[0] = now;
    s_status.ui_alive = true;
  }
  if (mask & SS_WATCH_CAN) {
    s_last_kick[1] = now;
    s_status.can_alive = true;
  }
  if (mask & SS_WATCH_SHOW) {
    s_last_kick[2] = now;
    s_status.show_alive = true;
  }
}

void app_watchdog_note_can_rx(void) {
  s_status.can_link_ok = true;
  s_status.last_can_rx_ms = millis();
}

void app_watchdog_set_can_expected(bool expected) {
  s_can_expected = expected;
  if (expected) {
    /* Start the link timeout from the moment CAN is expected; otherwise the
     * stale 0 timestamp raises a hold before the first frame arrives. */
    s_status.last_can_rx_ms = millis();
  } else {
    s_status.can_link_ok = true;
  }
}

void app_watchdog_set_hold_callback(app_watchdog_hold_fn fn) { s_hold_fn = fn; }

void app_watchdog_poll(void) {
  const uint32_t now = millis();
  if ((uint32_t)(now - s_started_ms) < kGraceMs) return;

  bool all_alive = true;

  if (stale(now, s_last_kick[0], SS_WATCH_UI_TIMEOUT_MS)) {
    s_status.ui_alive = false;
    all_alive = false;
    raise_hold("UI task stalled");
  }
  if (stale(now, s_last_kick[1], SS_WATCH_CAN_TIMEOUT_MS)) {
    s_status.can_alive = false;
    all_alive = false;
    raise_hold("CAN task stalled");
  }
  if (stale(now, s_last_kick[2], SS_WATCH_SHOW_TIMEOUT_MS)) {
    s_status.show_alive = false;
    all_alive = false;
    raise_hold("show task stalled");
  }

  if (s_can_expected && stale(now, s_status.last_can_rx_ms,
                              SS_WATCH_CAN_TIMEOUT_MS)) {
    s_status.can_link_ok = false;
    all_alive = false;
    raise_hold("CAN link silent");
  }

  if (all_alive && s_status.safe_hold) {
    s_status.safe_hold = false;
    s_status.hold_reason[0] = '\0';
    SS_LOGI("watchdog", "all supervised tasks alive; hold cleared");
  }
}

const app_watchdog_status_t *app_watchdog_status(void) { return &s_status; }
