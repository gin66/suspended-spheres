#include "upgrade_manager.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <cstdio>
#include <cstring>

#include "app_log.h"
#include "app_watchdog.h"
#include "can_transport.h"
#include "commission.h"
#include "config_store.h"
#include "fleet_core.h"
#include "fly_image.h"
#include "katapult_client.h"
#include "release_core.h"

#ifndef SS_FW_VERSION
#define SS_FW_VERSION "dev"
#endif
#ifndef SS_BUILD_ID
#define SS_BUILD_ID 0
#endif

namespace {

bool start(bool all);

struct pending_t {
  uint8_t uuid[FL_UUID_LEN];
  int board_id;
  uint32_t build;
  uint8_t flags;
  char result[24];
};

SemaphoreHandle_t s_mutex = nullptr;
TaskHandle_t s_task = nullptr;
/* Internal RAM after LVGL is fragmented; a static stack avoids a failed
 * 8 KiB heap allocation at the moment the operator presses UPGRADE. */
StaticTask_t s_task_buf;
StackType_t s_task_stack[8192];

bool s_active = false;
bool s_stop = false;
bool s_show_run = false;
bool s_image_ready = false;

fl_set_t s_set;
fl_image_t s_image;
uint32_t s_image_size = 0;

int s_order[FL_MAX_BOARDS];
int s_total = 0;
int s_next = 0;
int s_done = 0;
int s_node_count = 0;
uint32_t s_last_view_ms = 0;

/* The worker never calls commission_* or config_store_*: after COMPLETE the
 * main loop waits for ASSIGN, sends VERSION_REQ and reports the build back. */
int s_wait_idx = -1;
int s_wait_board = -1;
uint32_t s_wait_deadline = 0;
bool s_wait_query_sent = false;
int s_wait_result = 0;       /* 0 waiting, 1 match, -1 failed */
uint32_t s_wait_build = 0;

uint8_t s_state[FL_MAX_BOARDS];
uint8_t s_color[FL_MAX_BOARDS];
uint8_t s_progress[FL_MAX_BOARDS];
char s_result[FL_MAX_BOARDS][28];

uint8_t s_overall = 0;
uint8_t s_current_progress = 0;
int s_yellow_board = -1;
char s_status[64] = "idle";
char s_image_version[24] = "--";
char s_source[8] = "UART";

pending_t s_pending[FL_MAX_BOARDS];
int s_pending_count = 0;

void lock(void) {
  if (s_mutex != nullptr) xSemaphoreTake(s_mutex, portMAX_DELAY);
}

void unlock(void) {
  if (s_mutex != nullptr) xSemaphoreGive(s_mutex);
}

void update_overall_locked(void) {
  if (s_total <= 0) {
    s_overall = 0;
    return;
  }
  uint32_t v = (uint32_t)s_done * 100u;
  if (s_active) v += s_current_progress;
  v /= (uint32_t)s_total;
  s_overall = (uint8_t)(v > 100u ? 100u : v);
}

void set_tile_locked(int idx, fl_state_t state, uint8_t progress,
                     const char *text) {
  if (idx < 0 || idx >= FL_MAX_BOARDS) return;
  s_state[idx] = (uint8_t)state;
  s_color[idx] = (uint8_t)fl_state_color(state, FL_REASON_SELECTED);
  s_progress[idx] = progress;
  std::snprintf(s_result[idx], sizeof(s_result[idx]), "%s",
                text != nullptr ? text : "");
}

void queue_persist_locked(const uint8_t uuid[FL_UUID_LEN], int board_id,
                          uint32_t build, uint8_t flags, const char *result) {
  if (s_pending_count >= FL_MAX_BOARDS) return;
  pending_t *p = &s_pending[s_pending_count++];
  std::memcpy(p->uuid, uuid, FL_UUID_LEN);
  p->board_id = board_id;
  p->build = build;
  p->flags = flags;
  std::snprintf(p->result, sizeof(p->result), "%s",
                result != nullptr ? result : "");
}

bool make_image_locked(void) {
  std::memset(&s_image, 0, sizeof(s_image));
  s_image.data = ss_fly_image_data();
  s_image_size = ss_fly_image_size();
  if (s_image.data == nullptr || s_image_size == 0 || SS_BUILD_ID == 0) {
    s_image_ready = false;
    return false;
  }
  s_image.size = s_image_size;
  s_image.build = (uint32_t)SS_BUILD_ID;
  std::snprintf(s_image.target, sizeof(s_image.target), "%s", RC_TARGET_FLY);
  std::snprintf(s_image.kind, sizeof(s_image.kind), "app");
  s_image.dirty = 0;

  char tag[20];
  std::snprintf(tag, sizeof(tag), "v%s", SS_FW_VERSION);
  rc_version_t v;
  if (rc_semver_parse(tag, &v) == 0) {
    s_image.version_major = v.major;
    s_image.version_minor = v.minor;
    s_image.version_patch = v.patch;
  }
  rc_sha256_hex(s_image.data, s_image.size, s_image.sha256);
  std::snprintf(s_image_version, sizeof(s_image_version), "%s", SS_FW_VERSION);
  s_image_ready = true;
  return true;
}

int build_nodes(fl_node_t *out, int cap, uint32_t now_ms) {
  const int n = commission_node_count();
  int count = 0;
  for (int i = 0; i < n && count < cap; ++i) {
    ss_node_t node;
    if (!commission_get_node(i, &node)) continue;
    fl_node_t *f = &out[count++];
    std::memset(f, 0, sizeof(*f));
    std::memcpy(f->uuid, node.uuid, FL_UUID_LEN);
    f->board_id = node.board_id;
    f->online = node.last_seen_ms != 0 &&
                (uint32_t)(now_ms - node.last_seen_ms) <= FL_ONLINE_MS;
    f->version_valid = node.version_seen;
    f->version_flags = node.version_flags;
    f->current_build = node.current_build;
    f->last_seen_ms = node.last_seen_ms;
    f->in_config = false;
    const ss_board_map_t *b = config_store_get_board(node.uuid);
    if (b != nullptr && b->board_id >= 0) f->in_config = true;
    char tag[24];
    std::snprintf(tag, sizeof(tag), "v%s", node.sw_version);
    rc_version_t v;
    if (rc_semver_parse(tag, &v) == 0) {
      f->version_major = v.major;
      f->version_minor = v.minor;
      f->version_patch = v.patch;
    }
  }
  return count;
}

bool in_order(int idx, int total) {
  for (int i = 0; i < total; ++i)
    if (s_order[i] == idx) return true;
  return false;
}

void finish_fail_locked(int idx, const uint8_t uuid[FL_UUID_LEN], int board,
                        uint32_t build, uint8_t flags, const char *text) {
  set_tile_locked(idx, FL_FAILED, 0, "failed");
  s_active = false;
  s_yellow_board = -1;
  std::snprintf(s_status, sizeof(s_status), "%s", text != nullptr ? text : "failed");
  queue_persist_locked(uuid, board, build, flags, "failed");
  update_overall_locked();
}

void progress_cb(int board_id, const char *phase, uint8_t progress, void *ctx) {
  (void)ctx;
  lock();
  int idx = -1;
  for (int i = 0; i < s_total; ++i) {
    const int cand = s_order[i];
    if (cand >= 0 && cand < s_node_count &&
        s_set.nodes[cand].board_id == board_id) {
      idx = cand;
      break;
    }
  }
  if (idx >= 0) {
    fl_state_t st = FL_WRITING;
    if (phase != nullptr) {
      if (std::strncmp(phase, "enter", 5) == 0) st = FL_ENTERING;
      else if (std::strncmp(phase, "SET_NODEID", 10) == 0 ||
               std::strncmp(phase, "CONNECT", 7) == 0 ||
               std::strncmp(phase, "GET_CANBUS_ID", 13) == 0)
        st = FL_CONNECTING;
      else if (std::strncmp(phase, "SEND_EOF", 8) == 0 ||
               std::strncmp(phase, "VERIFYING", 9) == 0 ||
               std::strncmp(phase, "verify", 6) == 0)
        st = FL_VERIFYING;
      else if (std::strncmp(phase, "COMPLETE", 8) == 0 ||
               std::strncmp(phase, "flash complete", 14) == 0)
        st = FL_REBOOTING;
    }
    set_tile_locked(idx, st, progress, phase);
    s_current_progress = progress;
    s_yellow_board = board_id;
    if (phase != nullptr)
      std::snprintf(s_status, sizeof(s_status), "board %02d %s %u%%", board_id,
                    phase, (unsigned)progress);
    update_overall_locked();
  }
  unlock();
}

void drain_pending(void) {
  pending_t local[FL_MAX_BOARDS];
  int n = 0;
  lock();
  n = s_pending_count;
  if (n > FL_MAX_BOARDS) n = FL_MAX_BOARDS;
  std::memcpy(local, s_pending, (size_t)n * sizeof(pending_t));
  s_pending_count = 0;
  unlock();
  if (n == 0) return;
  bool changed = false;
  for (int i = 0; i < n; ++i) {
    if (config_store_set_board_version(local[i].uuid, local[i].board_id,
                                       local[i].build, local[i].flags,
                                       local[i].result))
      changed = true;
  }
  /* Do not fopen SPIFFS here: the first FILE lock after LVGL can fail on a
   * fragmented internal heap.  The record is in RAM and the operator's Save
   * persists it (config_dirty is set). */
  if (changed)
    SS_LOGI("upgrade", "%d result(s) recorded; Save config to persist", n);
}

void worker(void *arg) {
  (void)arg;
  for (;;) {
    int idx = -1;
    int board = -1;
    uint8_t uuid[FL_UUID_LEN];
    lock();
    if (!s_active || s_next >= s_total || s_stop) {
      unlock();
      break;
    }
    idx = s_order[s_next];
    board = s_set.nodes[idx].board_id;
    std::memcpy(uuid, s_set.nodes[idx].uuid, FL_UUID_LEN);
    set_tile_locked(idx, FL_ENTERING, 0, "enter");
    s_yellow_board = board;
    s_current_progress = 0;
    std::snprintf(s_status, sizeof(s_status), "board %02d enter", board);
    update_overall_locked();
    unlock();

    char status[64] = "idle";
    const bool ok =
        katapult_flash_ex(board, uuid, s_image.data, s_image.size, status,
                          sizeof(status), progress_cb, nullptr);
    if (!ok) {
      lock();
      finish_fail_locked(idx, uuid, board, 0, 0, status);
      unlock();
      break;
    }

    SS_LOGI("upgrade", "board %d flashed, waiting for ASSIGN", board);
    lock();
    set_tile_locked(idx, FL_REBOOTING, 100, "reboot");
    s_wait_idx = idx;
    s_wait_board = board;
    s_wait_deadline = millis() + 12000u;
    s_wait_query_sent = false;
    s_wait_result = 0;
    s_wait_build = 0;
    std::snprintf(s_status, sizeof(s_status), "board %02d reboot", board);
    unlock();

    /* The main loop owns CAN and the config store.  It sends VERSION_REQ
     * once the rebooted app has a board ID again and publishes the build. */
    int result = 0;
    for (;;) {
      vTaskDelay(pdMS_TO_TICKS(200));
      app_watchdog_kick(SS_WATCH_UI | SS_WATCH_CAN);
      lock();
      result = s_wait_result;
      if (result == 0 && (int32_t)(millis() - s_wait_deadline) >= 0)
        result = -1;
      unlock();
      if (result != 0) break;
    }

    lock();
    int wait_idx = s_wait_idx;
    uint32_t seen_build = s_wait_build;
    s_wait_idx = -1;
    s_wait_board = -1;
    s_wait_result = 0;
    if (result != 1 || wait_idx != idx) {
      char text[64];
      std::snprintf(text, sizeof(text), "board %02d build %lu, expected %lu",
                    board, (unsigned long)seen_build,
                    (unsigned long)s_image.build);
      finish_fail_locked(idx, uuid, board, seen_build, 0, text);
      unlock();
      break;
    }
    set_tile_locked(idx, FL_DONE, 100, "done");
    ++s_done;
    s_yellow_board = -1;
    s_current_progress = 0;
    queue_persist_locked(uuid, board, s_image.build, 0, "done");
    if (s_stop) {
      s_active = false;
      std::snprintf(s_status, sizeof(s_status), "stopped: %d/%d", s_done,
                    s_total);
    } else if (s_next + 1 >= s_total) {
      s_active = false;
      std::snprintf(s_status, sizeof(s_status), "complete: %d/%d", s_done,
                    s_total);
    } else {
      ++s_next;
      std::snprintf(s_status, sizeof(s_status), "board %02d done", board);
    }
    update_overall_locked();
    const bool stop = !s_active;
    unlock();
    if (stop) break;
  }

  lock();
  if (s_active) s_active = false;
  s_yellow_board = -1;
  if (s_stop && s_done < s_total)
    std::snprintf(s_status, sizeof(s_status), "stopped: %d/%d", s_done,
                  s_total);
  else if (s_done == s_total && s_total > 0)
    std::snprintf(s_status, sizeof(s_status), "complete: %d/%d", s_done,
                  s_total);
  s_overall = (s_total > 0 && s_done == s_total) ? 100 : s_overall;
  unlock();
  s_task = nullptr;
  vTaskDelete(nullptr);
}

void rebuild_view(void) {
  fl_node_t nodes[FL_MAX_BOARDS];
  const uint32_t now = millis();
  const int count = build_nodes(nodes, FL_MAX_BOARDS, now);
  fl_set_t tmp;
  fl_build_set(&tmp, nodes, count, &s_image, now);

  lock();
  s_node_count = count;
  for (int i = 0; i < count; ++i) {
    s_state[i] = (uint8_t)tmp.states[i];
    s_color[i] = (uint8_t)tmp.colors[i];
    s_progress[i] = 0;
    if (tmp.colors[i] == FL_COLOR_RED)
      std::snprintf(s_result[i], sizeof(s_result[i]), "waiting");
    else
      std::snprintf(s_result[i], sizeof(s_result[i]), "%s",
                    fl_reason_text(tmp.reasons[i]));
  }
  unlock();
}

}  // namespace

extern "C" void upgrade_manager_init(void) {
  if (s_mutex == nullptr) s_mutex = xSemaphoreCreateMutex();
  s_image_size = ss_fly_image_size();
  s_image_ready = s_image_size > 0 && SS_BUILD_ID != 0;
  if (s_image_ready) {
    lock();
    make_image_locked();
    unlock();
    std::snprintf(s_image_version, sizeof(s_image_version), "%s",
                  SS_FW_VERSION);
  }
  std::snprintf(s_status, sizeof(s_status), "%s",
                s_image_ready ? "idle" : "no Fly image selected");
}

extern "C" bool upgrade_manager_start_all(void) { return start(true); }

extern "C" bool upgrade_manager_start_selected(void) { return start(false); }

namespace {

bool start(bool all) {
  lock();
  if (s_active) {
    std::snprintf(s_status, sizeof(s_status), "queue already running");
    unlock();
    return false;
  }
  if (!can_transport_running()) {
    std::snprintf(s_status, sizeof(s_status), "CAN is not running");
    unlock();
    return false;
  }
  if (!make_image_locked()) {
    std::snprintf(s_status, sizeof(s_status), "no Fly image selected");
    unlock();
    return false;
  }

  fl_node_t nodes[FL_MAX_BOARDS];
  const uint32_t now = millis();
  const int count = build_nodes(nodes, FL_MAX_BOARDS, now);
  fl_build_set(&s_set, nodes, count, &s_image, now);
  s_node_count = s_set.node_count;

  int picked = 0;
  if (all) {
    for (int i = 0; i < s_set.selected_count; ++i)
      s_order[picked++] = s_set.selected[i];
  } else {
    const int sel = commission_selected_index();
    if (sel < 0 || sel >= s_node_count ||
        s_set.reasons[sel] != FL_REASON_SELECTED) {
      const char *why = (sel >= 0 && sel < s_node_count)
                            ? fl_reason_text(s_set.reasons[sel])
                            : "no board selected";
      std::snprintf(s_status, sizeof(s_status), "selected board: %s", why);
      unlock();
      return false;
    }
    s_order[picked++] = sel;
  }

  if (picked == 0) {
    std::snprintf(s_status, sizeof(s_status), "no outdated boards");
    unlock();
    return false;
  }

  s_total = picked;
  s_next = 0;
  s_done = 0;
  s_overall = 0;
  s_current_progress = 0;
  s_yellow_board = -1;
  s_stop = false;
  s_show_run = true;
  for (int i = 0; i < s_node_count; ++i) {
    if (in_order(i, s_total)) {
      s_state[i] = UI_UPG_QUEUED;
      s_color[i] = UI_UPG_COLOR_RED;
      s_progress[i] = 0;
      std::snprintf(s_result[i], sizeof(s_result[i]), "waiting");
    } else {
      s_state[i] = UI_UPG_SKIPPED;
      s_color[i] = UI_UPG_COLOR_DIM;
      s_progress[i] = 0;
      const char *why = all ? fl_reason_text(s_set.reasons[i]) : "not selected";
      std::snprintf(s_result[i], sizeof(s_result[i]), "%s", why);
    }
  }
  std::snprintf(s_status, sizeof(s_status), "queue: %d board%s", s_total,
                s_total == 1 ? "" : "s");
  s_active = true;
  unlock();

  /* One board at a time; the test run and the show are stopped first. */
  commission_test_run_stop();

  s_task = xTaskCreateStatic(worker, "upgrade", 8192, nullptr, 1,
                             s_task_stack, &s_task_buf);
  if (s_task == nullptr) {
    lock();
    s_active = false;
    std::snprintf(s_status, sizeof(s_status), "upgrade task not started");
    unlock();
    return false;
  }
  SS_LOGI("upgrade", "started: %d boards, image build %lu", s_total,
          (unsigned long)s_image.build);
  return true;
}

}  // namespace

extern "C" void upgrade_manager_stop(void) {
  lock();
  if (!s_active) {
    std::snprintf(s_status, sizeof(s_status), "no upgrade running");
    unlock();
    return;
  }
  s_stop = true;
  std::snprintf(s_status, sizeof(s_status), "finish board %d, then stop",
                s_yellow_board >= 0 ? s_yellow_board : 0);
  unlock();
}

extern "C" void upgrade_manager_refresh(void) {
  lock();
  s_show_run = false;
  unlock();
}

extern "C" void upgrade_manager_status(char *out, unsigned len) {
  if (out == nullptr || len == 0) return;
  lock();
  std::snprintf(out, len, "%s%s", s_active ? "running: " : "", s_status);
  unlock();
}

extern "C" bool upgrade_manager_active(void) {
  lock();
  const bool active = s_active;
  unlock();
  return active;
}

extern "C" void upgrade_manager_poll(void) {
  drain_pending();

  /* Post-flash verify, on the main loop task where CAN and config are safe. */
  lock();
  int widx = s_wait_idx;
  int wboard = s_wait_board;
  bool sent = s_wait_query_sent;
  uint32_t deadline = s_wait_deadline;
  unlock();
  if (widx >= 0) {
    ss_node_t n;
    const bool assigned =
        commission_get_node(widx, &n) && n.board_id == wboard &&
        n.last_seen_ms != 0 &&
        (uint32_t)(millis() - n.last_seen_ms) <= FL_ONLINE_MS;
    if (!sent && assigned) {
      commission_query_version(wboard);
      lock();
      s_wait_query_sent = true;
      s_wait_deadline = millis() + 8000u;
      unlock();
    } else if (sent) {
      if (commission_get_node(widx, &n) && n.version_seen) {
        lock();
        s_wait_build = n.current_build;
        s_wait_result = (n.current_build == s_image.build) ? 1 : -1;
        unlock();
      } else if ((int32_t)(millis() - deadline) >= 0) {
        lock();
        s_wait_build = 0;
        s_wait_result = -1;
        unlock();
      }
    } else if ((int32_t)(millis() - deadline) >= 0) {
      lock();
      s_wait_result = -1;
      unlock();
    }
  }

  lock();
  const bool active = s_active;
  const bool show = s_show_run;
  const bool ready = s_image_ready;
  unlock();
  if (active || show) return;
  const uint32_t now = millis();
  if (s_last_view_ms != 0 && (uint32_t)(now - s_last_view_ms) < 1000u) return;
  s_last_view_ms = now;

  if (!ready) {
    lock();
    const int n = commission_node_count();
    s_node_count = n > FL_MAX_BOARDS ? FL_MAX_BOARDS : n;
    for (int i = 0; i < s_node_count; ++i) {
      s_state[i] = UI_UPG_SKIPPED;
      s_color[i] = UI_UPG_COLOR_DIM;
      s_progress[i] = 0;
      std::snprintf(s_result[i], sizeof(s_result[i]), "no image");
    }
    s_done = 0;
    s_total = 0;
    s_overall = 0;
    unlock();
    return;
  }
  rebuild_view();
}

extern "C" void upgrade_manager_snapshot(ui_snapshot_t *snap) {
  if (snap == nullptr) return;
  lock();
  snap->upgrade_active = s_active;
  snap->upgrade_progress = s_overall;
  snap->upgrade_done = s_done;
  snap->upgrade_total = s_total;
  snap->upgrade_yellow_board = s_yellow_board;
  snap->upgrade_image_build = s_image_ready ? s_image.build : 0;
  std::snprintf(snap->upgrade_image_version,
                sizeof(snap->upgrade_image_version), "%s",
                s_image_ready ? s_image_version : "--");
  std::snprintf(snap->upgrade_source, sizeof(snap->upgrade_source), "%s",
                s_image_ready ? s_source : "");
  std::snprintf(snap->upgrade_status, sizeof(snap->upgrade_status), "%s",
                s_status);
  for (int i = 0; i < snap->node_count; ++i) {
    ui_node_t *n = &snap->nodes[i];
    if (i < s_node_count) {
      n->upgrade_state = s_state[i];
      n->upgrade_color = s_color[i];
      n->upgrade_progress = s_progress[i];
      std::snprintf(n->upgrade_result, sizeof(n->upgrade_result), "%s",
                    s_result[i]);
    } else {
      n->upgrade_state = UI_UPG_SKIPPED;
      n->upgrade_color = UI_UPG_COLOR_DIM;
      n->upgrade_progress = 0;
      std::snprintf(n->upgrade_result, sizeof(n->upgrade_result), "no image");
    }
  }
  unlock();
}
