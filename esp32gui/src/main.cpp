/*
 * esp32gui — installation master and operator UI.
 *
 * Stage 1 scaffolding + stage 3 commissioning UI. There is no WiFi, no OTA and
 * no .private secret in this project. CAN runs through can_transport; the
 * default bench build is the mock because the bus is not wired yet.
 */

#include <Arduino.h>
#include <lvgl.h>

#include <cstdio>
#include <cstring>

#include "app_log.h"
#include "app_watchdog.h"
#include "board_ws7.h"
#include "can_monitor.h"
#include "can_transport.h"
#include "commission.h"
#include "config_store.h"
#include "motion_core.h"
#include "ui.h"

#ifndef SS_FW_VERSION
#define SS_FW_VERSION "dev"
#endif

namespace {

/* RGB565 draw buffer in internal RAM. The panel DMA cannot cross into PSRAM. */
uint8_t s_draw_buf[BOARD_WS7_SCREEN_WIDTH * BOARD_WS7_DRAW_LINES * 2];

ui_snapshot_t s_snapshot;
lv_display_t *s_display = nullptr;
uint32_t s_last_ui_ms = 0;
uint32_t s_test_run_started_ms = 0;
bool s_upgrade_active = false;
uint8_t s_upgrade_progress = 0;
char s_upgrade_status[64] = "idle";
ss_can_id_entry_t s_can_scratch[SS_CAN_MONITOR_MAX_IDS];

static uint32_t lv_tick_cb(void) { return (uint32_t)millis(); }

void can_rx_callback(const ss_can_frame_t *frame, void *ctx) {
  (void)ctx;
  app_watchdog_note_can_rx();
  can_monitor_push(frame);
  commission_on_frame(frame, nullptr);
}

void on_safe_hold(const char *reason) {
  SS_LOGE("main", "safe hold: %s", reason != nullptr ? reason : "?");
  commission_stop_all();
}

const ss_node_t *selected_node(ss_node_t *out) {
  const int index = commission_selected_index();
  if (!commission_get_node(index, out)) return nullptr;
  return out;
}

void action_node_select(int index) {
  commission_select(index);
}

void action_motor(int action, int axis) {
  ss_node_t node;
  if (selected_node(&node) == nullptr || node.board_id < 0) {
    SS_LOGW("motor", "no assigned board selected");
    return;
  }
  const ss_config_t *cfg = config_store_get();
  ss_motor_action_t op = SS_MOTOR_STOP;
  uint16_t distance = 10;
  uint16_t speed = cfg->test_speed_mm_s;
  switch (action) {
    case 0:
      op = SS_MOTOR_HOME;
      distance = 0;
      break;
    case 1:
      op = SS_MOTOR_UP;
      break;
    case 2:
      op = SS_MOTOR_DOWN;
      break;
    default:
      op = SS_MOTOR_STOP;
      distance = 0;
      break;
  }
  commission_test_motor(node.flash_id, (uint8_t)axis, op, distance, speed);
}

void action_assign_position(int position, int axis) {
  ss_node_t node;
  if (selected_node(&node) == nullptr || node.board_id < 0) {
    SS_LOGW("motor", "cannot assign: no assigned board selected");
    return;
  }
  if (config_store_assign_position((uint8_t)position, node.flash_id,
                                   (uint8_t)axis)) {
    SS_LOGI("motor", "position %d <- board %d axis %d", position, node.board_id,
            axis);
  }
}

void action_unassign_position(int position) {
  config_store_unassign_position((uint8_t)position);
}

void action_grid_select(int position) {
  const ss_config_t *cfg = config_store_get();
  if (position < 0 || position >= config_store_capacity()) return;
  const ss_position_map_t *p = &cfg->positions[position];
  if (!p->assigned) return;
  const int idx = commission_find_node(p->flash_id);
  if (idx >= 0) commission_select(idx);
}

void action_config_grid(int cols, int rows) {
  if (!config_store_set_grid((uint8_t)cols, (uint8_t)rows)) {
    SS_LOGW("config", "grid %dx%d rejected", cols, rows);
  }
}

void action_config_test(int drop_mm, int speed_mm_s, int delay_ms, int serp) {
  if (!config_store_set_test_params((uint16_t)drop_mm, (uint16_t)speed_mm_s,
                                    (uint16_t)delay_ms, serp != 0)) {
    SS_LOGW("config", "test parameters rejected");
  }
}

void action_config_save(void) {
  commission_poll(); /* flush newly assigned board IDs into the config */
  if (config_store_save()) {
    SS_LOGI("config", "saved to SPIFFS");
  } else {
    SS_LOGE("config", "save failed");
  }
}

void action_config_reset(void) {
  config_store_reset_defaults();
  commission_reload_config();
  commission_select(0);
}

void action_config_clear_map(void) {
  config_store_clear_positions();
  SS_LOGI("config", "grid mapping cleared (unsaved)");
}

void action_test_start(void) {
  if (commission_test_run_start()) {
    s_test_run_started_ms = millis();
  }
}

void action_test_stop(void) { commission_test_run_stop(); }

void action_log_clear(void) { app_log_clear(); }

void action_version_query_all(void) {
  if (commission_query_versions()) {
    std::snprintf(s_upgrade_status, sizeof(s_upgrade_status),
                  "version query sent, waiting for responses");
  } else {
    std::snprintf(s_upgrade_status, sizeof(s_upgrade_status),
                  "CAN is not running");
  }
}

void action_upgrade_selected(void) {
  ss_node_t node;
  if (selected_node(&node) == nullptr || node.board_id < 0) {
    std::snprintf(s_upgrade_status, sizeof(s_upgrade_status),
                  "select an assigned board first");
    return;
  }
  if (!node.version_seen) {
    std::snprintf(s_upgrade_status, sizeof(s_upgrade_status),
                  "no version reported by board %d", node.board_id);
    return;
  }
  std::snprintf(s_upgrade_status, sizeof(s_upgrade_status),
                "board %d: CAN upgrade transfer is not implemented in the "
                "node firmware",
                node.board_id);
  SS_LOGW("upgrade", "upgrade requested for board %d, transfer not implemented",
          node.board_id);
}

void action_can_start(void) {
  if (!can_transport_start(1000000u, can_rx_callback, nullptr)) {
    SS_LOGE("can", "start failed");
    return;
  }
  app_watchdog_set_can_expected(!can_transport_is_mock());
}

void action_can_stop(void) {
  can_transport_stop();
  app_watchdog_set_can_expected(false);
}

void action_can_clear(void) { can_monitor_reset(); }

#ifdef SS_CAN_MOCK
void action_mock_inject(void) {
  static uint8_t counter = 1;
  ss_can_frame_t f;
  std::memset(&f, 0, sizeof(f));
  f.id = 8; /* field 0, type DISC */
  f.len = 8;
  f.data[0] = counter++;
  f.data[1] = 0x11;
  f.data[2] = 0x22;
  f.data[3] = 0x33;
  f.data[4] = 0x44;
  f.data[5] = 0x55;
  f.data[6] = 0x66;
  f.data[7] = 0x77;
  can_transport_mock_inject(&f);
  SS_LOGI("mock", "injected DISC node");
}
#endif

void build_snapshot(ui_snapshot_t *snap) {
  std::memset(snap, 0, sizeof(*snap));

  const ss_config_t *cfg = config_store_get();
  const ss_commission_stats_t *stats = commission_stats();
  const app_watchdog_status_t *wd = app_watchdog_status();
  ss_can_stats_t can;
  can_transport_get_stats(&can);

  snap->can_running = can.running;
  snap->can_mock = can.mock;
  snap->bus_ok = can.running && !can.bus_off;
  snap->can_rx_frames = can.rx_frames;
  snap->can_tx_frames = can.tx_frames;
  snap->can_errors = can.tx_failed + can.bus_errors;

  const ss_can_monitor_counters_t *mon = can_monitor_counters();
  snap->can_rate_1s = mon->rate_1s;
  snap->can_dropped = mon->dropped;
  snap->can_unique_ids = mon->unique_ids;
  int can_n = can_monitor_copy(s_can_scratch, SS_CAN_MONITOR_MAX_IDS);
  if (can_n > UI_MAX_CAN_IDS) can_n = UI_MAX_CAN_IDS;
  snap->can_entry_count = can_n;
  for (int i = 0; i < can_n; ++i) {
    const ss_can_id_entry_t *src = &s_can_scratch[i];
    ui_can_entry_t *dst = &snap->can_entries[i];
    dst->identifier = src->identifier;
    dst->type = (uint8_t)mc_id_type((int)src->identifier);
    dst->field = (uint8_t)mc_id_field((int)src->identifier);
    dst->data_len = src->data_len;
    std::memcpy(dst->data, src->data, sizeof(dst->data));
    dst->frame_count = src->frame_count;
    dst->changed_count = src->changed_count;
    dst->interval_ms = src->interval_ms;
    dst->interval_min_ms = src->interval_min_ms;
    dst->interval_max_ms = src->interval_max_ms;
  }
  snap->safe_hold = wd->safe_hold;
  std::snprintf(snap->hold_reason, sizeof(snap->hold_reason), "%s",
                wd->hold_reason);
  std::snprintf(snap->run_state, sizeof(snap->run_state), "%s",
                commission_test_run_active()
                    ? "TEST RUN"
                    : (wd->safe_hold ? "SAFE HOLD" : "IDLE"));
  std::snprintf(snap->pattern_name, sizeof(snap->pattern_name), "%s",
                "COMMISSIONING");
  snap->elapsed_ms =
      commission_test_run_active() ? (millis() - s_test_run_started_ms) : 0;
  snap->uptime_s = millis() / 1000u;

  snap->discovered_boards = stats->discovered_boards;
  snap->assigned_boards = stats->assigned_boards;
  snap->online_boards = stats->online_boards;
  snap->discovered_steppers = stats->discovered_steppers;
  snap->assigned_positions = stats->assigned_positions;
  snap->unassigned_positions = stats->unassigned_positions;
  snap->unassigned_steppers = stats->unassigned_steppers;
  snap->capacity = stats->capacity;

  snap->node_count = commission_node_count();
  if (snap->node_count > UI_MAX_NODES) snap->node_count = UI_MAX_NODES;
  for (int i = 0; i < snap->node_count; ++i) {
    ss_node_t n;
    if (!commission_get_node(i, &n)) continue;
    ui_node_t *u = &snap->nodes[i];
    u->used = n.used;
    std::memcpy(u->flash_id, n.flash_id, sizeof(u->flash_id));
    u->board_id = n.board_id;
    u->online = (uint32_t)(millis() - n.last_seen_ms) <= 10000u;
    u->status_seen = n.status_seen;
    u->state = n.state;
    u->fault_flags = n.fault_flags;
    u->buffer_depth = n.buffer_depth;
    u->version_seen = n.version_seen;
    std::snprintf(u->sw_version, sizeof(u->sw_version), "%s", n.sw_version);
    u->assigned_positions = n.assigned_positions;
  }
  snap->selected_node = commission_selected_index();
  snap->upgrade_active = s_upgrade_active;
  snap->upgrade_progress = s_upgrade_progress;
  std::snprintf(snap->upgrade_status, sizeof(snap->upgrade_status), "%s",
                s_upgrade_status);

  snap->grid_cols = cfg->grid_cols;
  snap->grid_rows = cfg->grid_rows;
  for (int pos = 0; pos < UI_MAX_POSITIONS; ++pos) {
    if (pos >= config_store_capacity()) break;
    const ss_position_map_t *p = &cfg->positions[pos];
    snap->cells[pos].assigned = p->assigned;
    snap->cells[pos].axis = p->axis;
    snap->cells[pos].node_index = -1;
    if (p->assigned) {
      const int idx = commission_find_node(p->flash_id);
      if (idx >= 0) snap->cells[pos].node_index = idx;
    }
  }

  snap->test_drop_mm = cfg->test_drop_mm;
  snap->test_speed_mm_s = cfg->test_speed_mm_s;
  snap->test_step_delay_ms = cfg->test_step_delay_ms;
  snap->test_serpentine = cfg->test_serpentine != 0;
  snap->test_run_active = commission_test_run_active();
  snap->test_run_remaining = commission_test_run_remaining();
  snap->config_dirty = config_store_dirty();

  snap->boot_count = wd->boot_count;
  snap->last_watchpoint = wd->last_watchpoint;
  std::snprintf(snap->firmware_version, sizeof(snap->firmware_version), "%s",
                SS_FW_VERSION);
  app_log_dump(snap->log_text, sizeof(snap->log_text));
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(100);

  app_log_init(APP_LOG_DEFAULT_BYTES);
  SS_LOGI("boot", "esp32gui %s starting (no WiFi/secrets)", SS_FW_VERSION);
  app_watchdog_init();
  app_watchdog_set_hold_callback(on_safe_hold);
  WATCH(1);

  config_store_init();
  commission_init();
  can_monitor_start();

  /* CH422G first: it owns touch reset, panel reset, backlight and USB_SEL. */
  if (board_ws7_expander_init() != ESP_OK) {
    SS_LOGE("boot", "CH422G expander init failed");
  }
  /* Keep the CH9102 console: USB_SEL low means native USB on 19/20. CAN start
   * raises it only when the transport actually starts. */
  board_ws7_set_usb_mode(true);
  board_ws7_backlight(false);

  WATCH(2);
  board_ws7_panel_reset();
  const bool touch_ok = (board_ws7_touch_init() == ESP_OK);
  SS_LOGI("boot", "GT911 touch: %s", touch_ok ? "ready" : "not found");

  lv_init();
  lv_tick_set_cb(lv_tick_cb);
  s_display = board_ws7_display_init(BOARD_WS7_ROTATION,
                                     reinterpret_cast<uint32_t *>(s_draw_buf),
                                     sizeof(s_draw_buf));
  if (s_display == nullptr) {
    SS_LOGE("boot", "RGB panel init failed");
  } else {
    if (touch_ok) {
      lv_indev_t *indev = lv_indev_create();
      lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
      lv_indev_set_read_cb(indev, board_ws7_touch_read_cb);
      lv_indev_set_display(indev, s_display);
    }
  }

  ui_actions_t actions;
  std::memset(&actions, 0, sizeof(actions));
  actions.node_select = action_node_select;
  actions.motor_action = action_motor;
  actions.assign_position = action_assign_position;
  actions.unassign_position = action_unassign_position;
  actions.grid_select = action_grid_select;
  actions.config_grid = action_config_grid;
  actions.config_test = action_config_test;
  actions.config_save = action_config_save;
  actions.config_reset = action_config_reset;
  actions.config_clear_map = action_config_clear_map;
  actions.test_run_start = action_test_start;
  actions.test_run_stop = action_test_stop;
  actions.log_clear = action_log_clear;
  actions.can_start = action_can_start;
  actions.can_stop = action_can_stop;
  actions.can_clear = action_can_clear;
  actions.version_query_all = action_version_query_all;
  actions.upgrade_selected = action_upgrade_selected;
#ifdef SS_CAN_MOCK
  actions.mock_inject = action_mock_inject;
#endif
  ui_init(&actions);

  build_snapshot(&s_snapshot);
  ui_update(&s_snapshot);

  if (s_display != nullptr) {
    lv_refr_now(s_display);
    board_ws7_backlight(true);
  }

  action_can_start();
  app_watchdog_set_can_expected(!can_transport_is_mock());
  WATCH(3);
  SS_LOGI("boot", "ready: %s transport, %d position capacity",
          can_transport_is_mock() ? "mock" : "TWAI",
          config_store_capacity());
}

void loop() {
  lv_task_handler();
  commission_poll();
  app_watchdog_poll();
  app_watchdog_kick(SS_WATCH_ALL);

  const uint32_t now = millis();
  if ((uint32_t)(now - s_last_ui_ms) >= 100u) {
    s_last_ui_ms = now;
    build_snapshot(&s_snapshot);
    ui_update(&s_snapshot);
  }

  delay(5);
}
