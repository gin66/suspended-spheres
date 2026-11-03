#include "can_monitor.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>
#include <cstring>

#include "app_log.h"

namespace {

struct RingFrame {
  ss_can_frame_t frame;
};

RingFrame s_ring[SS_CAN_MONITOR_RING_SLOTS];
std::atomic<uint8_t> s_head{0};
std::atomic<uint8_t> s_tail{0};
std::atomic<uint32_t> s_dropped{0};
std::atomic<uint32_t> s_rx_frames{0};

portMUX_TYPE s_data_lock = portMUX_INITIALIZER_UNLOCKED;
ss_can_id_entry_t s_table[SS_CAN_MONITOR_MAX_IDS];
int s_count = 0;
ss_can_monitor_counters_t s_counters;
TaskHandle_t s_task = nullptr;
volatile bool s_reset = false;

bool ring_pop(ss_can_frame_t *out) {
  const uint8_t tail = s_tail.load(std::memory_order_relaxed);
  if (tail == s_head.load(std::memory_order_acquire)) return false;
  *out = s_ring[tail].frame;
  s_tail.store((uint8_t)((tail + 1u) % SS_CAN_MONITOR_RING_SLOTS),
               std::memory_order_release);
  return true;
}

void table_update(const ss_can_frame_t *f) {
  portENTER_CRITICAL(&s_data_lock);
  ss_can_id_entry_t *entry = nullptr;
  for (int i = 0; i < s_count; ++i) {
    if (s_table[i].used && s_table[i].identifier == f->id) {
      entry = &s_table[i];
      break;
    }
  }
  if (entry == nullptr && s_count < SS_CAN_MONITOR_MAX_IDS) {
    entry = &s_table[s_count++];
    std::memset(entry, 0, sizeof(*entry));
    entry->used = true;
    entry->identifier = f->id;
    entry->is_extended = f->extended;
  }
  if (entry == nullptr) {
    portEXIT_CRITICAL(&s_data_lock);
    return;
  }

  const uint32_t now = f->timestamp_ms;
  if (entry->frame_count > 0) {
    entry->interval_ms = now - entry->last_timestamp_ms;
    if (entry->frame_count == 1 || entry->interval_ms < entry->interval_min_ms) {
      entry->interval_min_ms = entry->interval_ms;
    }
    if (entry->frame_count == 1 || entry->interval_ms > entry->interval_max_ms) {
      entry->interval_max_ms = entry->interval_ms;
    }
  }
  entry->last_timestamp_ms = now;
  entry->data_changed =
      (entry->data_len != f->len) ||
      (std::memcmp(entry->data, f->data, f->len) != 0);

  std::memcpy(entry->last_data, entry->data, sizeof(entry->last_data));
  std::memset(entry->data, 0, sizeof(entry->data));
  std::memcpy(entry->data, f->data, f->len);
  entry->data_len = f->len;
  entry->frame_count++;
  if (entry->data_changed) entry->changed_count++;
  portEXIT_CRITICAL(&s_data_lock);
}

void monitor_task(void *) {
  uint32_t last_rate_ms = millis();
  uint32_t last_rate_frames = 0;

  for (;;) {
    ss_can_frame_t f;
    bool did_work = false;
    while (ring_pop(&f)) {
      table_update(&f);
      did_work = true;
    }

    if (s_reset) {
      s_reset = false;
      s_rx_frames.store(0, std::memory_order_relaxed);
      s_dropped.store(0, std::memory_order_relaxed);
      last_rate_frames = 0;
      portENTER_CRITICAL(&s_data_lock);
      std::memset(s_table, 0, sizeof(s_table));
      std::memset(&s_counters, 0, sizeof(s_counters));
      s_count = 0;
      portEXIT_CRITICAL(&s_data_lock);
      SS_LOGI("can_mon", "scope cleared");
    }

    const uint32_t now = millis();
    if ((uint32_t)(now - last_rate_ms) >= 1000u) {
      const uint32_t total_rx = s_rx_frames.load(std::memory_order_relaxed);
      portENTER_CRITICAL(&s_data_lock);
      s_counters.rx_frames = total_rx;
      s_counters.dropped = s_dropped.load(std::memory_order_relaxed);
      s_counters.rate_1s = total_rx - last_rate_frames;
      s_counters.unique_ids = (uint32_t)s_count;
      portEXIT_CRITICAL(&s_data_lock);
      last_rate_frames = total_rx;
      last_rate_ms = now;
    }

    if (!did_work) vTaskDelay(pdMS_TO_TICKS(10));
  }
}

}  // namespace

void can_monitor_start(void) {
  if (s_task != nullptr) return;
  if (xTaskCreatePinnedToCore(monitor_task, "can_mon", 4096, nullptr, 2,
                              &s_task, 0) != pdPASS) {
    SS_LOGE("can_mon", "consumer task creation failed");
    s_task = nullptr;
  }
}

void can_monitor_push(const ss_can_frame_t *frame) {
  if (frame == nullptr) return;
  const uint8_t head = s_head.load(std::memory_order_relaxed);
  const uint8_t next = (uint8_t)((head + 1u) % SS_CAN_MONITOR_RING_SLOTS);
  if (next == s_tail.load(std::memory_order_acquire)) {
    s_dropped.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  s_ring[head].frame = *frame;
  s_head.store(next, std::memory_order_release);
  s_rx_frames.fetch_add(1, std::memory_order_relaxed);
}

bool can_monitor_get_entry(int index, ss_can_id_entry_t *out) {
  if (out == nullptr) return false;
  bool ok = false;
  portENTER_CRITICAL(&s_data_lock);
  if (index >= 0 && index < s_count && s_table[index].used) {
    *out = s_table[index];
    ok = true;
  }
  portEXIT_CRITICAL(&s_data_lock);
  return ok;
}

int can_monitor_count(void) {
  portENTER_CRITICAL(&s_data_lock);
  const int n = s_count;
  portEXIT_CRITICAL(&s_data_lock);
  return n;
}

const ss_can_monitor_counters_t *can_monitor_counters(void) {
  return &s_counters;
}

void can_monitor_reset(void) { s_reset = true; }
