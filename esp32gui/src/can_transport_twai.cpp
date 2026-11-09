#include "can_transport.h"

#ifndef SS_CAN_MOCK

#include <Arduino.h>
#include <driver/twai.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstring>

#include "app_log.h"
#include "board_ws7.h"

#ifndef SS_CAN_TX_GPIO
#define SS_CAN_TX_GPIO 20
#endif
#ifndef SS_CAN_RX_GPIO
#define SS_CAN_RX_GPIO 19
#endif

namespace {
ss_can_stats_t s_stats;
ss_can_rx_fn s_cb = nullptr;
void *s_ctx = nullptr;
TaskHandle_t s_task = nullptr;
volatile bool s_running = false;
uint32_t s_last_recovery_ms = 0;

void refresh_status() {
  twai_status_info_t info;
  if (twai_get_status_info(&info) != ESP_OK) return;
  s_stats.state = (int)info.state;
  s_stats.tec = info.tx_error_counter;
  s_stats.rec = info.rx_error_counter;
  s_stats.bus_errors = info.bus_error_count;
  s_stats.tx_failed = info.tx_failed_count;
  s_stats.rx_dropped = info.rx_missed_count + info.rx_overrun_count;
  s_stats.bus_off = (info.state == TWAI_STATE_BUS_OFF);
}

void can_rx_task(void *) {
  /* The task outlives a stop/start cycle. It only touches TWAI while
   * s_running is true, so can_transport_stop() can uninstall the driver
   * without deleting and later recreating a task (heap is tight after the
   * LVGL UI is built). */
  while (true) {
    if (!s_running) {
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }
    twai_message_t msg;
    if (twai_receive(&msg, pdMS_TO_TICKS(50)) == ESP_OK) {
      if (!msg.extd && !msg.rtr && msg.identifier <= 0x7FFu) {
        ss_can_frame_t f;
        std::memset(&f, 0, sizeof(f));
        f.id = msg.identifier;
        f.len = (uint8_t)(msg.data_length_code > 8 ? 8 : msg.data_length_code);
        std::memcpy(f.data, msg.data, f.len);
        f.timestamp_ms = millis();
        s_stats.rx_frames++;
        if (s_cb != nullptr) s_cb(&f, s_ctx);
      }
      refresh_status();
    } else {
      refresh_status();
      /* Manual bus-off recovery. Controller_analysis.md asks for detection and
       * a hold, not silent retries forever. */
      if (s_running && s_stats.bus_off &&
          (uint32_t)(millis() - s_last_recovery_ms) > 1000u) {
        s_last_recovery_ms = millis();
        SS_LOGW("can", "bus-off; starting recovery");
        if (twai_initiate_recovery() == ESP_OK) {
          vTaskDelay(pdMS_TO_TICKS(100));
          if (twai_start() == ESP_OK) {
            SS_LOGI("can", "bus-off recovery restarted");
          }
        }
      }
    }
  }
}

}  // namespace

bool can_transport_start(uint32_t bitrate, ss_can_rx_fn cb, void *ctx) {
  if (s_running) return true;
  if (bitrate != 1000000u) {
    SS_LOGE("can", "only 1 Mbit/s is supported on this bus (%lu)",
            (unsigned long)bitrate);
    return false;
  }

  s_cb = cb;
  s_ctx = ctx;
  std::memset(&s_stats, 0, sizeof(s_stats));
  s_stats.mock = false;

  /* IO5 high routes GPIO 19/20 to the TJA1051. */
  if (board_ws7_set_usb_mode(false) != ESP_OK) {
    SS_LOGE("can", "USB_SEL mux switch to CAN failed");
    return false;
  }
  vTaskDelay(pdMS_TO_TICKS(5));

  twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(
      (gpio_num_t)SS_CAN_TX_GPIO, (gpio_num_t)SS_CAN_RX_GPIO,
      TWAI_MODE_NORMAL);
  g.rx_queue_len = 64;
  g.tx_queue_len = 32;
  g.alerts_enabled = TWAI_ALERT_BUS_OFF | TWAI_ALERT_BUS_ERROR |
                     TWAI_ALERT_RX_QUEUE_FULL;
  const twai_timing_config_t t = TWAI_TIMING_CONFIG_1MBITS();
  const twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  if (twai_driver_install(&g, &t, &f) != ESP_OK) {
    SS_LOGE("can", "twai_driver_install failed");
    return false;
  }
  if (twai_start() != ESP_OK) {
    SS_LOGE("can", "twai_start failed");
    twai_driver_uninstall();
    return false;
  }

  s_running = true;
  s_stats.running = true;
  s_last_recovery_ms = 0;
  if (s_task == nullptr) {
    SS_LOGI("can", "starting RX task: internal free=%u largest=%u",
            (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
            (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    if (xTaskCreatePinnedToCore(can_rx_task, "can_rx", 4096, nullptr, 5,
                                &s_task, 0) != pdPASS) {
      SS_LOGE("can", "CAN RX task creation failed");
      s_running = false;
      s_stats.running = false;
      s_task = nullptr;
      twai_stop();
      twai_driver_uninstall();
      return false;
    }
  }

  SS_LOGI("can", "TWAI started at 1 Mbit/s (TX=%d RX=%d)",
          SS_CAN_TX_GPIO, SS_CAN_RX_GPIO);
  return true;
}

void can_transport_stop(void) {
  if (!s_running) return;
  s_running = false;
  s_stats.running = false;
  vTaskDelay(pdMS_TO_TICKS(150));
  twai_stop();
  twai_driver_uninstall();
  board_ws7_set_usb_mode(true);
  SS_LOGI("can", "TWAI stopped; GPIO 19/20 returned to USB");
}

bool can_transport_running(void) { return s_running; }

bool can_transport_send(const ss_can_frame_t *frame) {
  if (!s_running || frame == nullptr) {
    s_stats.tx_failed++;
    return false;
  }
  if (frame->extended || frame->id > 0x7FFu || frame->len > 8) {
    s_stats.tx_failed++;
    return false;
  }
  twai_message_t msg;
  std::memset(&msg, 0, sizeof(msg));
  msg.identifier = frame->id;
  msg.extd = 0;
  msg.rtr = 0;
  msg.data_length_code = frame->len;
  std::memcpy(msg.data, frame->data, frame->len);

  if (twai_transmit(&msg, 0) != ESP_OK) {
    s_stats.tx_failed++;
    return false;
  }
  s_stats.tx_frames++;
  return true;
}

void can_transport_get_stats(ss_can_stats_t *out) {
  if (out == nullptr) return;
  refresh_status();
  *out = s_stats;
  out->running = s_running;
}

bool can_transport_is_mock(void) { return false; }

#endif /* !SS_CAN_MOCK */
