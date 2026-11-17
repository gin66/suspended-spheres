#include "show_player.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <cstdio>
#include <cstring>

#include "app_log.h"
#include "can_transport.h"
#include "commission.h"
#include "config_store.h"
#include "show_core/show_core.h"

namespace {

enum : uint8_t {
  kCmdStart = 1,
  kCmdPause,
  kCmdResume,
  kCmdStop,
  kCmdCycle,
};

struct ShowCommand {
  uint8_t cmd;
  bool cycle;
  char path[160];
  char name[40];
};

constexpr int kQueueDepth = 4;
constexpr size_t kMaxFileBytes = 4u * 1024u * 1024u;

QueueHandle_t s_queue = nullptr;
TaskHandle_t s_task = nullptr;
portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

ss_show_status_t s_status;
bool s_cycle = false;

uint8_t *s_buf = nullptr;
size_t s_buf_size = 0;
show_hbp_t s_hbp;
bool s_hbp_valid = false;
bool s_loopable = false;

int s_next_window = 0;
int64_t s_finish_at_us = 0;    /* after the last window has played */
int64_t s_pause_at_us = 0;     /* end of the last sent window */
bool s_pause_pending = false;
int64_t s_epoch_us = 0;       /* local time of master T = 0 */
int64_t s_window_us = 0;

void set_state(ss_show_state_t state) {
  portENTER_CRITICAL(&s_lock);
  s_status.state = state;
  portEXIT_CRITICAL(&s_lock);
}

void set_error(const char *text) {
  portENTER_CRITICAL(&s_lock);
  s_status.state = SS_SHOW_FAULT;
  std::snprintf(s_status.error, sizeof(s_status.error), "%s",
                text ? text : "show fault");
  portEXIT_CRITICAL(&s_lock);
  SS_LOGE("show", "%s", text ? text : "show fault");
}

void release_buffer(void) {
  if (s_buf != nullptr) {
    heap_caps_free(s_buf);
    s_buf = nullptr;
  }
  s_buf_size = 0;
  s_hbp_valid = false;
  std::memset(&s_hbp, 0, sizeof(s_hbp));
}

void send_stop(void) {
  if (can_transport_running()) commission_stop_all();
}

bool load_file(const char *path, const char *name, char *error,
               size_t error_cap) {
  FILE *f = std::fopen(path, "rb");
  if (f == nullptr) {
    std::snprintf(error, error_cap, "cannot open %s", path);
    return false;
  }
  if (std::fseek(f, 0, SEEK_END) != 0) {
    std::fclose(f);
    std::snprintf(error, error_cap, "cannot seek %s", path);
    return false;
  }
  const long size = std::ftell(f);
  if (size <= 0 || (size_t)size > kMaxFileBytes) {
    std::fclose(f);
    std::snprintf(error, error_cap, "file size %ld rejected", size);
    return false;
  }
  std::rewind(f);
  uint8_t *buf = (uint8_t *)heap_caps_malloc((size_t)size,
                                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (buf == nullptr) buf = (uint8_t *)std::malloc((size_t)size);
  if (buf == nullptr) {
    std::fclose(f);
    std::snprintf(error, error_cap, "no memory for %ld bytes", size);
    return false;
  }
  const size_t got = std::fread(buf, 1, (size_t)size, f);
  std::fclose(f);
  if (got != (size_t)size) {
    heap_caps_free(buf);
    std::snprintf(error, error_cap, "short read");
    return false;
  }

  show_hbp_t hbp;
  char parse_error[64];
  if (show_hbp_parse(buf, (size_t)size, &hbp, parse_error,
                     sizeof(parse_error)) != 0) {
    heap_caps_free(buf);
    std::snprintf(error, error_cap, "%s", parse_error);
    return false;
  }
  const ss_config_t *cfg = config_store_get();
  if (cfg != nullptr &&
      (hbp.cols != cfg->grid_cols || hbp.rows != cfg->grid_rows)) {
    heap_caps_free(buf);
    std::snprintf(error, error_cap, "grid %dx%d, config is %dx%d", hbp.cols,
                  hbp.rows, cfg->grid_cols, cfg->grid_rows);
    return false;
  }

  release_buffer();
  s_buf = buf;
  s_buf_size = (size_t)size;
  s_hbp = hbp;
  s_hbp_valid = true;
  s_loopable = show_hbp_loopable(&hbp, 1000, 40) == 1;
  s_window_us = (int64_t)hbp.window_ms * 1000;

  portENTER_CRITICAL(&s_lock);
  std::snprintf(s_status.name, sizeof(s_status.name), "%s", name);
  s_status.error[0] = '\0';
  s_status.duration_ms = hbp.duration_ms;
  s_status.window_count = hbp.window_count;
  s_status.window = 0;
  s_status.elapsed_ms = 0;
  portEXIT_CRITICAL(&s_lock);
  SS_LOGI("show", "loaded %s: %d balls, %d windows, %ums", name, hbp.balls,
          hbp.window_count, (unsigned)hbp.duration_ms);
  return true;
}

void start_show(const ShowCommand &cmd) {
  send_stop();
  set_state(SS_SHOW_LOADING);
  char error[64];
  if (!load_file(cmd.path, cmd.name, error, sizeof(error))) {
    release_buffer();
    set_error(error);
    return;
  }
  s_cycle = cmd.cycle && s_loopable;
  if (cmd.cycle && !s_loopable)
    SS_LOGW("show", "%s does not end where it starts; CYCLE ignored", cmd.name);
  s_next_window = 0;
  s_finish_at_us = 0;
  s_pause_pending = false;
  s_epoch_us = esp_timer_get_time() + SHOW_SYNC_LEAD_US;
  portENTER_CRITICAL(&s_lock);
  s_status.cycle = s_cycle;
  portEXIT_CRITICAL(&s_lock);
  set_state(SS_SHOW_PLAYING);
  SS_LOGI("show", "play %s", cmd.name);
}

void finish_show(int64_t now_us) {
  s_finish_at_us = 0;
  s_pause_pending = false;
  if (s_cycle && s_loopable && s_hbp_valid) {
    send_stop();
    s_next_window = 0;
    s_epoch_us = now_us + 500000 + SHOW_SYNC_LEAD_US;
    portENTER_CRITICAL(&s_lock);
    s_status.window = 0;
    s_status.elapsed_ms = 0;
    portEXIT_CRITICAL(&s_lock);
    SS_LOGI("show", "cycle restart");
    return;
  }
  send_stop();
  release_buffer();
  portENTER_CRITICAL(&s_lock);
  s_status.state = SS_SHOW_IDLE;
  s_status.window = s_status.window_count;
  s_status.elapsed_ms = s_status.duration_ms;
  portEXIT_CRITICAL(&s_lock);
  SS_LOGI("show", "finished");
}

void play_step(void) {
  if (!s_hbp_valid) return;
  const int64_t now = esp_timer_get_time();
  if (s_pause_pending) {
    /* Let the window that was already sent finish, then stop at its exact
     * end.  Resuming at the next window then has no position offset. */
    if (now < s_pause_at_us) return;
    s_pause_pending = false;
    send_stop();
    portENTER_CRITICAL(&s_lock);
    s_status.state = SS_SHOW_PAUSED;
    s_status.elapsed_ms = (uint32_t)((int64_t)s_next_window * s_window_us / 1000);
    portEXIT_CRITICAL(&s_lock);
    SS_LOGI("show", "paused at window %d", s_next_window);
    return;
  }
  if (s_next_window >= s_hbp.window_count) {
    /* The last window was sent 20 ms before it starts and then plays for a
     * whole window.  Do not send STOP into the middle of it. */
    if (s_finish_at_us != 0 && now >= s_finish_at_us) finish_show(now);
    return;
  }
  ss_can_stats_t can;
  can_transport_get_stats(&can);
  if (!can.running || can.bus_off) {
    set_error(can.bus_off ? "CAN bus-off" : "CAN stopped");
    return;
  }
  if (commission_any_fault()) {
    send_stop();
    set_error("node fault during show");
    return;
  }

  const int64_t t_start = (int64_t)s_next_window * s_window_us;
  const int64_t due = s_epoch_us + t_start - SHOW_SYNC_LEAD_US;
  if (now < due) return;

  show_frame_t plan[SHOW_MAX_WINDOW_FRAMES];
  const int count = show_plan_window(&s_hbp, s_next_window,
                                     (int64_t)s_next_window * s_window_us,
                                     (uint8_t)(s_next_window & 0xFF), plan,
                                     SHOW_MAX_WINDOW_FRAMES);
  if (count <= 0) {
    set_error("window plan failed");
    return;
  }
  for (int i = 0; i < count; ++i) {
    ss_can_frame_t f;
    std::memset(&f, 0, sizeof(f));
    f.id = plan[i].id;
    f.len = 8;
    std::memcpy(f.data, plan[i].data, 8);
    if (!can_transport_send_timeout(&f, 5)) {
      send_stop();
      set_error("CAN TX queue stalled");
      return;
    }
  }

  portENTER_CRITICAL(&s_lock);
  s_status.window = s_next_window;
  s_status.elapsed_ms = (uint32_t)(t_start / 1000);
  portEXIT_CRITICAL(&s_lock);
  ++s_next_window;
  if (s_next_window >= s_hbp.window_count)
    s_finish_at_us = now + s_window_us + 50000;
}

void handle_command(const ShowCommand &cmd) {
  switch (cmd.cmd) {
    case kCmdStart:
      start_show(cmd);
      break;
    case kCmdPause:
      if (s_status.state == SS_SHOW_PLAYING) {
        s_pause_pending = true;
        s_pause_at_us = s_epoch_us + (int64_t)s_next_window * s_window_us;
        SS_LOGI("show", "pause requested; stopping after window %d",
                s_next_window - 1);
      }
      break;
    case kCmdResume:
      if (s_status.state == SS_SHOW_PAUSED && s_hbp_valid) {
        if (s_next_window >= s_hbp.window_count) {
          finish_show(esp_timer_get_time());
          break;
        }
        s_epoch_us = esp_timer_get_time() + SHOW_SYNC_LEAD_US -
                     (int64_t)s_next_window * s_window_us;
        s_finish_at_us = 0;
        s_pause_pending = false;
        set_state(SS_SHOW_PLAYING);
        SS_LOGI("show", "resume at window %d", s_next_window);
      }
      break;
    case kCmdStop:
      send_stop();
      release_buffer();
      s_finish_at_us = 0;
      s_pause_pending = false;
      portENTER_CRITICAL(&s_lock);
      s_status.state = SS_SHOW_IDLE;
      s_status.elapsed_ms = 0;
      s_status.window = 0;
      portEXIT_CRITICAL(&s_lock);
      SS_LOGI("show", "stopped");
      break;
    case kCmdCycle:
      s_cycle = cmd.cycle;
      portENTER_CRITICAL(&s_lock);
      s_status.cycle = cmd.cycle;
      portEXIT_CRITICAL(&s_lock);
      break;
    default:
      break;
  }
}

void show_task(void *) {
  for (;;) {
    ShowCommand cmd;
    while (xQueueReceive(s_queue, &cmd, 0) == pdTRUE) handle_command(cmd);
    if (s_status.state == SS_SHOW_PLAYING) {
      play_step();
    } else {
      vTaskDelay(pdMS_TO_TICKS(5));
      continue;
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

bool queue_command(uint8_t cmd, const char *path, const char *name,
                   bool cycle) {
  if (s_queue == nullptr) return false;
  ShowCommand c;
  std::memset(&c, 0, sizeof(c));
  c.cmd = cmd;
  c.cycle = cycle;
  if (path != nullptr) std::snprintf(c.path, sizeof(c.path), "%s", path);
  if (name != nullptr) std::snprintf(c.name, sizeof(c.name), "%s", name);
  return xQueueSend(s_queue, &c, 0) == pdTRUE;
}

}  // namespace

void show_player_init(void) {
  std::memset(&s_status, 0, sizeof(s_status));
  s_status.state = SS_SHOW_IDLE;
  s_queue = xQueueCreate(kQueueDepth, sizeof(ShowCommand));
  if (s_queue == nullptr) {
    SS_LOGE("show", "queue alloc failed");
    return;
  }
  xTaskCreatePinnedToCore(show_task, "show", 8192, nullptr, 5, &s_task, 0);
}

bool show_player_start(const char *path, const char *name, bool cycle) {
  if (!can_transport_running()) {
    SS_LOGW("show", "start refused: CAN is not running");
    return false;
  }
  /* The task sets LOADING when it receives the command.  Setting it here
   * would race the task's own transition to PLAYING on the other core. */
  return queue_command(kCmdStart, path, name, cycle);
}

void show_player_pause(void) { queue_command(kCmdPause, nullptr, nullptr, false); }

bool show_player_resume(void) {
  return queue_command(kCmdResume, nullptr, nullptr, false);
}

void show_player_stop(void) { queue_command(kCmdStop, nullptr, nullptr, false); }

void show_player_set_cycle(bool on) {
  queue_command(kCmdCycle, nullptr, nullptr, on);
}

bool show_player_running(void) {
  return s_status.state == SS_SHOW_LOADING ||
         s_status.state == SS_SHOW_PLAYING ||
         s_status.state == SS_SHOW_PAUSED;
}

bool show_player_playing(void) { return s_status.state == SS_SHOW_PLAYING; }

void show_player_snapshot(ss_show_status_t *out) {
  if (out == nullptr) return;
  portENTER_CRITICAL(&s_lock);
  *out = s_status;
  portEXIT_CRITICAL(&s_lock);
}
