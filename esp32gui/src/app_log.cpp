#include "app_log.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace {

portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
char *s_buf = nullptr;
size_t s_cap = 0;
size_t s_head = 0;
size_t s_used = 0;
bool s_serial = false;

const char *level_name(int level) {
  switch (level) {
    case APP_LOG_LEVEL_DEBUG:
      return "D";
    case APP_LOG_LEVEL_WARN:
      return "W";
    case APP_LOG_LEVEL_ERROR:
      return "E";
    default:
      return "I";
  }
}

void ring_write(const char *data, size_t n) {
  if (s_buf == nullptr || s_cap == 0 || n == 0) return;

  if (n > s_cap) {
    data += n - s_cap;
    n = s_cap;
  }

  taskENTER_CRITICAL(&s_mux);
  const size_t first = (n < s_cap - s_head) ? n : (s_cap - s_head);
  std::memcpy(s_buf + s_head, data, first);
  if (n > first) std::memcpy(s_buf, data + first, n - first);
  s_head = (s_head + n) % s_cap;
  s_used = (s_used + n < s_cap) ? (s_used + n) : s_cap;
  taskEXIT_CRITICAL(&s_mux);
}

}  // namespace

bool app_log_init(size_t bytes) {
  if (s_buf != nullptr) return true;
  if (bytes == 0) bytes = APP_LOG_DEFAULT_BYTES;

  s_buf = static_cast<char *>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM));
  if (s_buf == nullptr) {
    s_buf = static_cast<char *>(heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL));
  }
  if (s_buf == nullptr) return false;

  s_cap = bytes;
  s_head = 0;
  s_used = 0;
  s_serial = true;
  return true;
}

void app_log_write(const char *tag, int level, const char *fmt, ...) {
  char line[288];
  const int64_t us = esp_timer_get_time();
  const unsigned long sec = static_cast<unsigned long>(us / 1000000);
  const unsigned long ms = static_cast<unsigned long>((us / 1000) % 1000);

  int n = snprintf(line, sizeof(line), "[%lu.%03lu][%s][%s] ", sec, ms,
                   level_name(level), (tag != nullptr) ? tag : "-");
  if (n < 0) return;
  if (static_cast<size_t>(n) >= sizeof(line)) n = sizeof(line) - 1;

  va_list ap;
  va_start(ap, fmt);
  const int m = vsnprintf(line + n, sizeof(line) - static_cast<size_t>(n), fmt, ap);
  va_end(ap);
  if (m < 0) return;

  size_t len = static_cast<size_t>(n) + static_cast<size_t>(m);
  if (len >= sizeof(line) - 1) len = sizeof(line) - 2;
  line[len++] = '\n';
  line[len] = '\0';

  ring_write(line, len);
  if (s_serial) {
    Serial.write(reinterpret_cast<const uint8_t *>(line), len);
  }
}

size_t app_log_dump(char *out, size_t max) {
  if (out == nullptr || max < 2) return 0;
  out[0] = '\0';
  if (s_buf == nullptr || s_cap == 0) return 0;

  taskENTER_CRITICAL(&s_mux);
  const size_t avail = s_used;
  const size_t oldest = (s_used < s_cap) ? 0 : s_head;
  size_t take = (avail < max - 1) ? avail : (max - 1);
  size_t start = (oldest + (avail - take)) % s_cap;

  for (size_t i = 0; i < take; ++i) {
    out[i] = s_buf[(start + i) % s_cap];
  }
  out[take] = '\0';
  taskEXIT_CRITICAL(&s_mux);

  /* The oldest retained byte can be the middle of a record after wrap, and
   * a max-limited dump can start mid-record. Drop the partial first line so
   * the UI never starts with a fragment. */
  if (take < avail || avail == s_cap) {
    char *nl = static_cast<char *>(std::memchr(out, '\n', take));
    if (nl != nullptr && nl + 1 < out + take) {
      const size_t skip = static_cast<size_t>((nl + 1) - out);
      std::memmove(out, nl + 1, take - skip + 1);
      take -= skip;
    }
  }
  return take;
}

void app_log_clear(void) {
  taskENTER_CRITICAL(&s_mux);
  s_head = 0;
  s_used = 0;
  if (s_buf != nullptr && s_cap != 0) s_buf[0] = '\0';
  taskEXIT_CRITICAL(&s_mux);
}

size_t app_log_length(void) {
  taskENTER_CRITICAL(&s_mux);
  const size_t used = s_used;
  taskEXIT_CRITICAL(&s_mux);
  return used;
}

void app_log_set_serial(bool enabled) { s_serial = enabled; }
bool app_log_serial_enabled(void) { return s_serial; }
