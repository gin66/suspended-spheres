#include "ui.h"
#include "release_gate.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <lvgl.h>

#ifdef BOARD_HAS_PSRAM
#include <esp_heap_caps.h>
#endif

namespace {

constexpr int kWidth = 800;
constexpr int kHeight = 480;
constexpr int kRailW = 88;
constexpr int kRailX = kWidth - kRailW;
constexpr int kStatusH = 28;
constexpr int kScreenW = kRailX;
constexpr int kScreenH = kHeight - kStatusH;

/* CAN scope: fixed columns and a paged row pool. A proportional font cannot
 * keep space-padded columns aligned, so each field has its own label x. */
constexpr int kCanRows = 16;
constexpr int kCanRowH = 20;
constexpr int kCanIdX = 8;
constexpr int kCanTypeX = 56;
constexpr int kCanCntX = 104;
constexpr int kCanCntW = 60;
constexpr int kCanDataX = 176;

const lv_color_t C_BG = lv_color_hex(0x0E1116);
const lv_color_t C_RAIL = lv_color_hex(0x0A0D12);
const lv_color_t C_PANEL = lv_color_hex(0x171C24);
const lv_color_t C_PANEL2 = lv_color_hex(0x222A35);
const lv_color_t C_LINE = lv_color_hex(0x303A48);
const lv_color_t C_TEXT = lv_color_hex(0xE6EDF3);
const lv_color_t C_MUTED = lv_color_hex(0x8B949E);
const lv_color_t C_ACCENT = lv_color_hex(0x3AA0FF);
const lv_color_t C_OK = lv_color_hex(0x2ECC71);
const lv_color_t C_WARN = lv_color_hex(0xF1C40F);
const lv_color_t C_ERR = lv_color_hex(0xE74C3C);
const lv_color_t C_OFF = lv_color_hex(0x2A2F38);

ui_actions_t s_actions;
ui_snapshot_t *s_snap = nullptr;
lv_obj_t *s_screens[UI_SCREEN_COUNT];
lv_obj_t *s_nav;
lv_obj_t *s_link_label;
int s_screen = UI_SCREEN_SHOW;
int s_selected_axis = 0;
int s_assign_position = 0;
uint32_t s_log_hash = 0;
uint32_t s_log_last_ms = 0;

/* Show screen */
lv_obj_t *s_show_pattern;
lv_obj_t *s_show_state;
lv_obj_t *s_show_elapsed;
lv_obj_t *s_show_bus;
lv_obj_t *s_show_discovered;
lv_obj_t *s_show_assigned_boards;
lv_obj_t *s_show_online;
lv_obj_t *s_show_positions;
lv_obj_t *s_show_unassigned;
lv_obj_t *s_show_testrun;
lv_obj_t *s_show_thermal;
lv_obj_t *s_show_hold;
lv_obj_t *s_show_run_btn;
lv_obj_t *s_show_list;
lv_obj_t *s_show_library;
lv_obj_t *s_show_anim_summary;
lv_obj_t *s_show_library_summary;
lv_obj_t *s_show_progress;
lv_obj_t *s_show_play;
lv_obj_t *s_show_pause;
lv_obj_t *s_show_stop;
lv_obj_t *s_show_cycle;
lv_obj_t *s_show_transport;
lv_obj_t *s_show_playlist_panel;
lv_obj_t *s_show_library_panel;
lv_obj_t *s_show_big_stop;
lv_obj_t *s_show_status;
uint32_t s_show_list_hash = 0;
uint32_t s_playlist_source_hash = 0;
bool s_show_playing = false;
bool s_playlist_flags[UI_MAX_ANIMATIONS];
bool s_show_cycle_on = false;

/* Animations screen */
lv_obj_t *s_anim_list;
lv_obj_t *s_anim_summary;
lv_obj_t *s_anim_status;
uint32_t s_anim_shown_hash = 0;

/* Grid screen */
lv_obj_t *s_grid_summary;
lv_obj_t *s_grid_cells[UI_MAX_POSITIONS];
int s_grid_cols = 0;
int s_grid_rows = 0;
int s_grid_selected = -1;

/* Nodes screen */
lv_obj_t *s_nodes_list;
lv_obj_t *s_nodes_summary;
uint32_t s_nodes_shown_hash = 0;

/* Motor screen */
lv_obj_t *s_motor_node;
lv_obj_t *s_motor_flash;
lv_obj_t *s_motor_board;
lv_obj_t *s_motor_axis_btn[4];
lv_obj_t *s_motor_result;
lv_obj_t *s_motor_assign_pos;
lv_obj_t *s_motor_grid;
lv_obj_t *s_motor_cells[UI_MAX_POSITIONS];
int s_motor_cols = 0;
int s_motor_rows = 0;

/* Config screen */
lv_obj_t *s_cfg_cols;
lv_obj_t *s_cfg_rows;
lv_obj_t *s_cfg_drop;
lv_obj_t *s_cfg_speed;
lv_obj_t *s_cfg_delay;
lv_obj_t *s_cfg_serp;
lv_obj_t *s_cfg_status;

/* Log screen */
lv_obj_t *s_log_text;
lv_obj_t *s_log_status;

/* CAN scope screen */
lv_obj_t *s_can_summary;
lv_obj_t *s_can_bus;
lv_obj_t *s_can_empty;
lv_obj_t *s_can_row_id[kCanRows];
lv_obj_t *s_can_row_type[kCanRows];
lv_obj_t *s_can_row_cnt[kCanRows];
lv_obj_t *s_can_row_data[kCanRows];
int s_can_first = 0;
uint32_t s_can_shown_hash = 0;
uint32_t s_can_last_ms = 0;

/* Upgrade screen */
lv_obj_t *s_upgrade_tiles;
lv_obj_t *s_upgrade_summary;
lv_obj_t *s_upgrade_status;
lv_obj_t *s_upgrade_progress;
lv_obj_t *s_upgrade_stop;
lv_obj_t *s_upg_github_check;
lv_obj_t *s_upg_github_install;
lv_obj_t *s_upg_github_status;
uint32_t s_upgrade_shown_hash = 0;

/* Settings screen */
lv_obj_t *s_set_wifi;
lv_obj_t *s_set_ip;
lv_obj_t *s_set_can;
lv_obj_t *s_set_firmware;
lv_obj_t *s_set_boot;
lv_obj_t *s_set_uptime;
lv_obj_t *s_set_capacity;
lv_obj_t *s_set_watch;
lv_obj_t *s_upg_panel_status;
lv_obj_t *s_upg_panel_update;
lv_obj_t *s_set_boards;
lv_obj_t *s_set_positions;
lv_obj_t *s_set_node_image;
lv_obj_t *s_disp_btn[4];
lv_obj_t *s_wifi_list;
lv_obj_t *s_wifi_list_panel;
lv_obj_t *s_wifi_list_title;
lv_obj_t *s_wifi_qr_card;
lv_obj_t *s_wifi_qr;
lv_obj_t *s_wifi_url_qr;
uint32_t s_wifi_qr_hash = 0;
uint32_t s_wifi_url_qr_hash = 0;
lv_obj_t *s_wifi_status;
lv_obj_t *s_wifi_selected;
lv_obj_t *s_wifi_connect_hint;
lv_obj_t *s_wifi_pass;
lv_obj_t *s_wifi_modal;
lv_obj_t *s_wifi_modal_ta;
lv_obj_t *s_wifi_modal_title;
lv_obj_t *s_wifi_modal_kb;
int s_wifi_modal_target = 0;
bool s_wifi_upper = false;
bool s_wifi_symbols = false;
bool s_wifi_sta_seeded = false;
lv_obj_t *s_wifi_scan_btn;
lv_obj_t *s_wifi_reason;
lv_obj_t *s_wifi_mode_off;
lv_obj_t *s_wifi_mode_connect;
lv_obj_t *s_wifi_mode_ap;
lv_obj_t *s_wifi_service;
lv_obj_t *s_wifi_forget;
lv_obj_t *s_wifi_admin_btn;
lv_obj_t *s_wifi_ap_info;
lv_obj_t *s_wifi_connect;
lv_obj_t *s_wifi_connect_panel;
lv_obj_t *s_wifi_ap_panel;
lv_obj_t *s_wifi_url_qr_card;
lv_obj_t *s_wifi_service_panel;
lv_obj_t *s_wifi_ap_service;
lv_obj_t *s_wifi_ap_forget;
lv_obj_t *s_wifi_ap_admin;
lv_obj_t *s_wifi_ap_name;
lv_obj_t *s_wifi_ap_reason;
lv_obj_t *s_wifi_ap_key;
lv_obj_t *s_wifi_new_key_btn;
lv_obj_t *s_wifi_set_key_btn;
char s_wifi_admin_buf[65];
char s_wifi_ap_key_buf[64];
int s_wifi_selected_index = -1;
uint32_t s_wifi_shown_hash = 0;
/* Hubs */
lv_obj_t *s_maint_net;
lv_obj_t *s_maint_fw;
lv_obj_t *s_diag_can;
lv_obj_t *s_diag_fly;
lv_obj_t *s_diag_radio;

/* ── helpers ───────────────────────────────────────────────────────────── */

lv_obj_t *make_label(lv_obj_t *parent, const char *text, const lv_font_t *font,
                     lv_color_t color) {
  lv_obj_t *l = lv_label_create(parent);
  lv_label_set_text(l, text != nullptr ? text : "");
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, color, 0);
  lv_obj_clear_flag(l, LV_OBJ_FLAG_CLICKABLE);
  return l;
}

lv_obj_t *make_panel(lv_obj_t *parent, int x, int y, int w, int h) {
  lv_obj_t *p = lv_obj_create(parent);
  lv_obj_remove_style_all(p);
  lv_obj_set_pos(p, x, y);
  lv_obj_set_size(p, w, h);
  lv_obj_set_style_bg_color(p, C_PANEL, 0);
  lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(p, 8, 0);
  lv_obj_set_style_border_width(p, 1, 0);
  lv_obj_set_style_border_color(p, C_LINE, 0);
  lv_obj_set_style_pad_all(p, 12, 0);
  lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
  return p;
}

lv_obj_t *make_button(lv_obj_t *parent, const char *text, int w, int h,
                      lv_event_cb_t cb, void *user) {
  lv_obj_t *b = lv_button_create(parent);
  lv_obj_set_size(b, w, h);
  lv_obj_set_style_radius(b, 6, 0);
  lv_obj_set_style_bg_color(b, C_PANEL2, 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(b, C_ACCENT, LV_STATE_PRESSED);
  lv_obj_set_style_border_width(b, 1, 0);
  lv_obj_set_style_border_color(b, C_LINE, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_set_style_pad_all(b, 0, 0);
  lv_obj_set_style_bg_color(b, C_OFF, LV_STATE_DISABLED);
  lv_obj_set_style_border_color(b, C_LINE, LV_STATE_DISABLED);
  lv_obj_set_style_opa(b, LV_OPA_70, LV_STATE_DISABLED);
  lv_obj_t *l = lv_label_create(b);
  lv_label_set_text(l, text);
  lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(l, C_TEXT, 0);
  lv_obj_center(l);
  if (cb != nullptr) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
  return b;
}

lv_obj_t *make_kv(lv_obj_t *parent, const char *key, int y) {
  lv_obj_t *k = make_label(parent, key, &lv_font_montserrat_12, C_MUTED);
  lv_obj_set_pos(k, 12, y);
  lv_obj_t *v = make_label(parent, "--", &lv_font_montserrat_14, C_TEXT);
  lv_obj_set_pos(v, 116, y - 2);
  return v;
}

void set_hidden(lv_obj_t *obj, bool hidden) {
  if (hidden) {
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
  }
}

void on_tab(lv_event_t *e);

void nav_job(void *arg) {
  ui_show_screen((int)(intptr_t)arg);
}

void on_tab(lv_event_t *e) {
  const int index = (int)(intptr_t)lv_event_get_user_data(e);
  lv_async_call(nav_job, (void *)(intptr_t)index);
}

/* Every detail screen carries the same back target as its section rail icon.
 * The chevron is the second way home, so a sub-screen is never a dead end. */
void make_back_button(lv_obj_t *head, int target) {
  lv_obj_t *b = make_button(head, LV_SYMBOL_LEFT, 32, 32, on_tab,
                            (void *)(intptr_t)target);
  lv_obj_set_pos(b, 0, 0);
  lv_obj_set_style_text_font(lv_obj_get_child(b, 0), &lv_font_montserrat_14, 0);
}

void on_grid_cell(lv_event_t *e) {
  const int pos = (int)(intptr_t)lv_event_get_user_data(e);
  s_grid_selected = pos;
  s_assign_position = pos;
  if (s_actions.grid_select != nullptr) s_actions.grid_select(pos);
}

void on_node_row(lv_event_t *e) {
  const int index = (int)(intptr_t)lv_event_get_user_data(e);
  if (s_actions.node_select != nullptr) s_actions.node_select(index);
  ui_show_screen(UI_SCREEN_MOTOR);
}

void on_axis(lv_event_t *e) {
  s_selected_axis = (int)(intptr_t)lv_event_get_user_data(e);
  ui_update(s_snap);
}

void on_motor(lv_event_t *e) {
  const int action = (int)(intptr_t)lv_event_get_user_data(e);
  if (s_actions.motor_action != nullptr) {
    s_actions.motor_action(action, s_selected_axis);
  }
}

void on_assign(lv_event_t *e) {
  (void)e;
  if (s_actions.assign_position != nullptr) {
    s_actions.assign_position(s_assign_position, s_selected_axis);
  }
}

void on_unassign(lv_event_t *e) {
  (void)e;
  if (s_actions.unassign_position != nullptr) {
    s_actions.unassign_position(s_assign_position);
  }
}

void on_motor_pos(lv_event_t *e) {
  s_assign_position = (int)(intptr_t)lv_event_get_user_data(e);
  ui_update(s_snap);
}

void on_node_prev(lv_event_t *e) {
  (void)e;
  if (s_actions.node_select != nullptr) {
    s_actions.node_select(s_snap->selected_node - 1);
  }
}

void on_node_next(lv_event_t *e) {
  (void)e;
  if (s_actions.node_select != nullptr) {
    s_actions.node_select(s_snap->selected_node + 1);
  }
}

void on_grid_minus(lv_event_t *e) {
  (void)e;
  if (s_actions.config_grid != nullptr) {
    s_actions.config_grid(s_snap->grid_cols - 1, s_snap->grid_rows);
  }
}

void on_grid_plus(lv_event_t *e) {
  (void)e;
  if (s_actions.config_grid != nullptr) {
    s_actions.config_grid(s_snap->grid_cols + 1, s_snap->grid_rows);
  }
}

void on_rows_minus(lv_event_t *e) {
  (void)e;
  if (s_actions.config_grid != nullptr) {
    s_actions.config_grid(s_snap->grid_cols, s_snap->grid_rows - 1);
  }
}

void on_rows_plus(lv_event_t *e) {
  (void)e;
  if (s_actions.config_grid != nullptr) {
    s_actions.config_grid(s_snap->grid_cols, s_snap->grid_rows + 1);
  }
}

void on_preset_10x10(lv_event_t *e) {
  (void)e;
  if (s_actions.config_grid != nullptr) s_actions.config_grid(10, 10);
}

void on_preset_10x12(lv_event_t *e) {
  (void)e;
  if (s_actions.config_grid != nullptr) s_actions.config_grid(10, 12);
}

void on_drop_minus(lv_event_t *e) {
  (void)e;
  if (s_actions.config_test != nullptr) {
    s_actions.config_test(s_snap->test_drop_mm - 10, s_snap->test_speed_mm_s,
                          s_snap->test_step_delay_ms, s_snap->test_serpentine);
  }
}

void on_drop_plus(lv_event_t *e) {
  (void)e;
  if (s_actions.config_test != nullptr) {
    s_actions.config_test(s_snap->test_drop_mm + 10, s_snap->test_speed_mm_s,
                          s_snap->test_step_delay_ms, s_snap->test_serpentine);
  }
}

void on_speed_minus(lv_event_t *e) {
  (void)e;
  if (s_actions.config_test != nullptr) {
    s_actions.config_test(s_snap->test_drop_mm, s_snap->test_speed_mm_s - 5,
                          s_snap->test_step_delay_ms, s_snap->test_serpentine);
  }
}

void on_speed_plus(lv_event_t *e) {
  (void)e;
  if (s_actions.config_test != nullptr) {
    s_actions.config_test(s_snap->test_drop_mm, s_snap->test_speed_mm_s + 5,
                          s_snap->test_step_delay_ms, s_snap->test_serpentine);
  }
}

void on_delay_minus(lv_event_t *e) {
  (void)e;
  if (s_actions.config_test != nullptr) {
    s_actions.config_test(s_snap->test_drop_mm, s_snap->test_speed_mm_s,
                          s_snap->test_step_delay_ms - 50, s_snap->test_serpentine);
  }
}

void on_delay_plus(lv_event_t *e) {
  (void)e;
  if (s_actions.config_test != nullptr) {
    s_actions.config_test(s_snap->test_drop_mm, s_snap->test_speed_mm_s,
                          s_snap->test_step_delay_ms + 50, s_snap->test_serpentine);
  }
}

void on_serp(lv_event_t *e) {
  (void)e;
  if (s_actions.config_test != nullptr) {
    s_actions.config_test(s_snap->test_drop_mm, s_snap->test_speed_mm_s,
                          s_snap->test_step_delay_ms, !s_snap->test_serpentine);
  }
}

void on_save(lv_event_t *e) {
  (void)e;
  if (s_actions.config_save != nullptr) s_actions.config_save();
}

void on_reset(lv_event_t *e) {
  (void)e;
  if (s_actions.config_reset != nullptr) s_actions.config_reset();
}

void on_clear_map(lv_event_t *e) {
  (void)e;
  if (s_actions.config_clear_map != nullptr) s_actions.config_clear_map();
}

void on_test_start(lv_event_t *e) {
  (void)e;
  if (s_actions.test_run_start != nullptr) s_actions.test_run_start();
}

void on_test_stop(lv_event_t *e) {
  (void)e;
  if (s_actions.test_run_stop != nullptr) s_actions.test_run_stop();
}

void on_log_clear(lv_event_t *e) {
  (void)e;
  if (s_actions.log_clear != nullptr) s_actions.log_clear();
}

void on_scroll_up(lv_event_t *e) {
  lv_obj_t *target = static_cast<lv_obj_t *>(lv_event_get_user_data(e));
  if (target != nullptr) lv_obj_scroll_by_bounded(target, 0, -160, LV_ANIM_ON);
}

void on_scroll_down(lv_event_t *e) {
  lv_obj_t *target = static_cast<lv_obj_t *>(lv_event_get_user_data(e));
  if (target != nullptr) lv_obj_scroll_by_bounded(target, 0, 160, LV_ANIM_ON);
}

/* The panel has no swipe gestures: clearing SCROLLABLE stops touch scrolling
 * while the two buttons still move the content with lv_obj_scroll_by_bounded. */
void add_scroll_buttons(lv_obj_t *parent, lv_obj_t *target, int x, int y,
                        int w, int h, int gap, bool vertical) {
  lv_obj_clear_flag(target, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scrollbar_mode(target, LV_SCROLLBAR_MODE_OFF);
  lv_obj_t *up = make_button(parent, LV_SYMBOL_UP, w, h, on_scroll_up, target);
  lv_obj_set_pos(up, x, y);
  lv_obj_t *down = make_button(parent, LV_SYMBOL_DOWN, w, h, on_scroll_down,
                               target);
  lv_obj_set_pos(down, vertical ? x : x + w + gap,
                 vertical ? y + h + gap : y);
}

void can_refresh_rows(void) {
  const int n = s_snap->can_entry_count;
  const int max_first = n > kCanRows ? n - kCanRows : 0;
  if (s_can_first > max_first) s_can_first = max_first;
  if (s_can_first < 0) s_can_first = 0;

  for (int i = 0; i < kCanRows; ++i) {
    lv_obj_t *id = s_can_row_id[i];
    lv_obj_t *type = s_can_row_type[i];
    lv_obj_t *cnt = s_can_row_cnt[i];
    lv_obj_t *data = s_can_row_data[i];
    const int idx = s_can_first + i;
    if (idx >= n) {
      lv_obj_add_flag(id, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(type, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(cnt, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(data, LV_OBJ_FLAG_HIDDEN);
      continue;
    }

    const ui_can_entry_t *e = &s_snap->can_entries[idx];
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%03lx", (unsigned long)e->identifier);
    lv_label_set_text(id, buf);
    std::snprintf(buf, sizeof(buf), "%u", (unsigned)e->type);
    lv_label_set_text(type, buf);
    std::snprintf(buf, sizeof(buf), "%lu", (unsigned long)e->frame_count);
    lv_label_set_text(cnt, buf);
    size_t dp = 0;
    for (int b = 0; b < e->data_len && b < 8; ++b) {
      dp += (size_t)std::snprintf(buf + dp, sizeof(buf) - dp, "%s%02x",
                                  b ? " " : "", e->data[b]);
    }
    buf[dp] = '\0';
    lv_label_set_text(data, buf);

    lv_obj_clear_flag(id, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(type, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(cnt, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(data, LV_OBJ_FLAG_HIDDEN);
  }

  if (s_can_empty != nullptr) {
    if (n == 0) lv_obj_clear_flag(s_can_empty, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_can_empty, LV_OBJ_FLAG_HIDDEN);
  }
}

void on_can_page_up(lv_event_t *e) {
  (void)e;
  s_can_first -= kCanRows;
  if (s_can_first < 0) s_can_first = 0;
  can_refresh_rows();
}

void on_can_page_down(lv_event_t *e) {
  (void)e;
  s_can_first += kCanRows;
  can_refresh_rows();
}

void on_can_toggle(lv_event_t *e) {
  (void)e;
  if (s_snap->can_running) {
    if (s_actions.can_stop != nullptr) s_actions.can_stop();
  } else {
    if (s_actions.can_start != nullptr) s_actions.can_start();
  }
}

#ifdef SS_CAN_MOCK
void on_mock_inject(lv_event_t *e) {
  (void)e;
  if (s_actions.mock_inject != nullptr) s_actions.mock_inject();
}
#endif

void on_anim_row(lv_event_t *e) {
  const int index = (int)(intptr_t)lv_event_get_user_data(e);
  if (s_actions.animation_select != nullptr) s_actions.animation_select(index);
  if (s_snap != nullptr) ui_update(s_snap);
}

void on_anim_refresh(lv_event_t *e) {
  (void)e;
  if (s_actions.animation_refresh != nullptr) s_actions.animation_refresh();
}

void on_anim_load(lv_event_t *e) {
  (void)e;
  if (s_actions.animation_load != nullptr) s_actions.animation_load();
  s_show_playing = true;
  if (s_snap != nullptr) ui_update(s_snap);
}

void on_anim_add(lv_event_t *e) {
  const int index = (int)(intptr_t)lv_event_get_user_data(e);
  if (index >= 0 && index < UI_MAX_ANIMATIONS) s_playlist_flags[index] = true;
  if (s_actions.animation_add != nullptr) s_actions.animation_add(index);
  if (s_snap != nullptr) ui_update(s_snap);
}

void on_anim_remove(lv_event_t *e) {
  const int index = (int)(intptr_t)lv_event_get_user_data(e);
  if (index >= 0 && index < UI_MAX_ANIMATIONS) s_playlist_flags[index] = false;
  if (s_actions.animation_remove != nullptr) s_actions.animation_remove(index);
  if (s_snap != nullptr) ui_update(s_snap);
}

void on_anim_pause(lv_event_t *e) {
  (void)e;
  if (s_actions.animation_pause != nullptr) s_actions.animation_pause();
  s_show_playing = false;
  if (s_snap != nullptr) ui_update(s_snap);
}

void on_anim_stop(lv_event_t *e) {
  (void)e;
  if (s_actions.animation_stop != nullptr) s_actions.animation_stop();
  s_show_playing = false;
  if (s_snap != nullptr) ui_update(s_snap);
}

void on_anim_stop_now(lv_event_t *e) {
  (void)e;
  if (s_actions.animation_stop_immediate != nullptr)
    s_actions.animation_stop_immediate();
  s_show_playing = false;
  if (s_snap != nullptr) ui_update(s_snap);
}

void on_anim_cycle(lv_event_t *e) {
  (void)e;
  s_show_cycle_on = !s_show_cycle_on;
  if (s_snap != nullptr) ui_update(s_snap);
}

void on_can_clear(lv_event_t *e) {
  (void)e;
  if (s_actions.can_clear != nullptr) s_actions.can_clear();
}

void on_version_query(lv_event_t *e) {
  (void)e;
  if (s_actions.version_query_all != nullptr) s_actions.version_query_all();
}

void on_upgrade_start(lv_event_t *e) {
  (void)e;
  if (s_actions.upgrade_selected != nullptr) s_actions.upgrade_selected();
}

void on_upgrade_all(lv_event_t *e) {
  (void)e;
  if (s_actions.upgrade_all_outdated != nullptr)
    s_actions.upgrade_all_outdated();
}

void on_upgrade_stop(lv_event_t *e) {
  (void)e;
  if (s_actions.upgrade_stop != nullptr) s_actions.upgrade_stop();
}

void on_wifi_setup(lv_event_t *e) {
  (void)e;
  if (s_actions.wifi_setup != nullptr) s_actions.wifi_setup();
}

void on_wifi_forget(lv_event_t *e) {
  (void)e;
  if (s_actions.wifi_forget != nullptr) s_actions.wifi_forget();
}

void on_wifi_scan(lv_event_t *e) {
  (void)e;
  if (s_actions.wifi_scan != nullptr) s_actions.wifi_scan();
}

void on_service_toggle(lv_event_t *e) {
  (void)e;
  if (s_actions.service_toggle != nullptr) s_actions.service_toggle();
}

void on_github_check(lv_event_t *e) {
  (void)e;
  if (s_actions.github_check != nullptr) s_actions.github_check();
}

void on_github_install(lv_event_t *e) {
  (void)e;
  if (s_actions.github_install != nullptr) s_actions.github_install();
}

void on_wifi_join(lv_event_t *e) {
  (void)e;
  if (s_actions.wifi_connect == nullptr) return;
  const char *pass = lv_textarea_get_text(s_wifi_pass);
  if (s_wifi_selected_index >= 0) {
    const char *ssid = s_snap->wifi_scan_ssid[s_wifi_selected_index];
    if (ssid != nullptr && ssid[0] != '\0' && pass != nullptr &&
        pass[0] != '\0') {
      s_actions.wifi_connect(ssid, pass);
      lv_textarea_set_text(s_wifi_pass, "");
      return;
    }
  }
  if (s_actions.wifi_retry != nullptr) s_actions.wifi_retry();
}

void on_wifi_off(lv_event_t *e) {
  (void)e;
  if (s_actions.wifi_off != nullptr) s_actions.wifi_off();
}

void on_wifi_ap(lv_event_t *e) {
  (void)e;
  if (s_actions.wifi_ap != nullptr) s_actions.wifi_ap();
}

void wifi_open_modal(int target);

void on_wifi_new_key(lv_event_t *e) {
  (void)e;
  if (s_actions.wifi_new_ap_key != nullptr) s_actions.wifi_new_ap_key();
}

void on_wifi_set_ap_key(lv_event_t *e) {
  (void)e;
  wifi_open_modal(2);
}

void on_wifi_admin(lv_event_t *e) {
  (void)e;
  if (s_actions.wifi_set_admin == nullptr) return;
  if (s_wifi_admin_buf[0] != '\0') {
    s_actions.wifi_set_admin(s_wifi_admin_buf);
    s_wifi_admin_buf[0] = '\0';
  }
}

void on_wifi_forget_btn(lv_event_t *e) {
  (void)e;
  if (s_actions.wifi_forget != nullptr) s_actions.wifi_forget();
}

void on_wifi_row(lv_event_t *e) {
  s_wifi_selected_index = (int)(intptr_t)lv_event_get_user_data(e);
  if (s_snap != nullptr &&
      s_wifi_selected_index >= 0 &&
      s_wifi_selected_index < s_snap->wifi_scan_count &&
      std::strcmp(s_snap->wifi_scan_ssid[s_wifi_selected_index],
                  s_snap->wifi_ssid) != 0) {
    lv_textarea_set_text(s_wifi_pass, "");
    s_wifi_sta_seeded = true;
  }
  if (s_snap != nullptr) ui_update(s_snap);
}

void wifi_close_modal(bool keep) {
  if (s_wifi_modal == nullptr) return;
  if (keep) {
    const char *text = lv_textarea_get_text(s_wifi_modal_ta);
    if (s_wifi_modal_target == 0) {
      lv_textarea_set_text(s_wifi_pass, text);
    } else if (s_wifi_modal_target == 1) {
      std::snprintf(s_wifi_admin_buf, sizeof(s_wifi_admin_buf), "%s", text);
    } else {
      std::snprintf(s_wifi_ap_key_buf, sizeof(s_wifi_ap_key_buf), "%s", text);
    }
  }
  lv_obj_add_flag(s_wifi_modal, LV_OBJ_FLAG_HIDDEN);
}

constexpr int kWifiBack = -1;
constexpr int kWifiShift = -2;
constexpr int kWifiSym = -3;
constexpr int kWifiSpace = -4;
constexpr int kWifiDone = -5;

void wifi_build_keypad(void);

void on_wifi_key(lv_event_t *e) {
  const int code = (int)(intptr_t)lv_event_get_user_data(e);
  if (code == kWifiShift) {
    s_wifi_upper = !s_wifi_upper;
    wifi_build_keypad();
  } else if (code == kWifiSym) {
    s_wifi_symbols = !s_wifi_symbols;
    wifi_build_keypad();
  } else if (code == kWifiBack) {
    lv_textarea_delete_char(s_wifi_modal_ta);
  } else if (code == kWifiSpace) {
    lv_textarea_add_char(s_wifi_modal_ta, ' ');
  } else if (code == kWifiDone) {
    wifi_close_modal(true);
  } else {
    lv_textarea_add_char(s_wifi_modal_ta, (uint32_t)code);
  }
}

void add_key(lv_obj_t *parent, const char *label, int x, int y, int w, int h,
             int code) {
  lv_obj_t *b = make_button(parent, label, w, h, on_wifi_key,
                            (void *)(intptr_t)code);
  lv_obj_set_pos(b, x, y);
  lv_obj_set_style_text_font(b, &lv_font_montserrat_20, 0);
}

void wifi_build_keypad(void) {
  if (s_wifi_modal_kb == nullptr) return;
  lv_obj_clean(s_wifi_modal_kb);
  const char *digits = "1234567890";
  const char *row1 = s_wifi_symbols
                         ? "!@#$%^&*()"
                         : (s_wifi_upper ? "QWERTYUIOP" : "qwertyuiop");
  const char *row2 = s_wifi_symbols ? "-_=+[]{};"
                                    : (s_wifi_upper ? "ASDFGHJKL" : "asdfghjkl");
  const char *row3 = s_wifi_symbols
                         ? ",.<>/?\\"
                         : (s_wifi_upper ? "ZXCVBNM" : "zxcvbnm");
  for (int i = 0; i < 10; ++i) add_key(s_wifi_modal_kb,
                                       (const char[]){digits[i], 0},
                                       2 + i * 78, 0, 70, 58, digits[i]);
  for (int i = 0; i < 10; ++i) add_key(s_wifi_modal_kb,
                                       (const char[]){row1[i], 0},
                                       2 + i * 78, 66, 70, 58, row1[i]);
  for (int i = 0; i < 9; ++i) add_key(s_wifi_modal_kb,
                                      (const char[]){row2[i], 0},
                                      i * 87, 132, 79, 58, row2[i]);
  add_key(s_wifi_modal_kb, s_wifi_upper ? "SHIFT" : "shift", 0, 198, 96, 58,
          kWifiShift);
  for (int i = 0; i < 7; ++i) add_key(s_wifi_modal_kb,
                                      (const char[]){row3[i], 0},
                                      104 + i * 78, 198, 70, 58, row3[i]);
  add_key(s_wifi_modal_kb, "DEL", 650, 198, 96, 58, kWifiBack);
  add_key(s_wifi_modal_kb, s_wifi_symbols ? "ABC" : "SYM", 0, 264, 96, 58,
          kWifiSym);
  add_key(s_wifi_modal_kb, "SPACE", 104, 264, 672, 58, kWifiSpace);
}

void wifi_open_modal(int target) {
  if (s_wifi_modal == nullptr) return;
  s_wifi_modal_target = target;
  lv_label_set_text(s_wifi_modal_title,
                    target == 0 ? "WiFi password"
                                : (target == 1 ? "Admin password"
                                               : "AP password"));
  const char *text = target == 0 ? lv_textarea_get_text(s_wifi_pass)
                                 : (target == 1 ? s_wifi_admin_buf
                                                : (s_snap != nullptr &&
                                                           s_snap->wifi_ap_password[0]
                                                       ? s_snap->wifi_ap_password
                                                       : s_wifi_ap_key_buf));
  lv_textarea_set_text(s_wifi_modal_ta, text);
  lv_textarea_set_password_mode(s_wifi_modal_ta, target != 2);
  wifi_build_keypad();
  lv_obj_clear_flag(s_wifi_modal, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(s_wifi_modal);
}

void on_wifi_pass_focus(lv_event_t *e) {
  (void)e;
  wifi_open_modal(0);
}

void on_wifi_admin_focus(lv_event_t *e) {
  (void)e;
  wifi_open_modal(1);
}

void on_wifi_modal_done(lv_event_t *e) {
  (void)e;
  const int target = s_wifi_modal_target;
  wifi_close_modal(true);
  if (target == 1) on_wifi_admin(nullptr);
  else if (target == 2 && s_actions.wifi_set_ap_key != nullptr &&
           s_wifi_ap_key_buf[0] != '\0')
    s_actions.wifi_set_ap_key(s_wifi_ap_key_buf);
}

void on_wifi_modal_cancel(lv_event_t *e) {
  (void)e;
  wifi_close_modal(false);
}


void on_display_preset(lv_event_t *e) {
  const uint16_t seconds =
      (uint16_t)(intptr_t)lv_event_get_user_data(e);
  if (s_actions.display_timeout_set != nullptr)
    s_actions.display_timeout_set(seconds);
  else if (s_actions.display_timeout_cycle != nullptr)
    s_actions.display_timeout_cycle();
}

void on_panel_update(lv_event_t *e) {
  (void)e;
  if (s_actions.panel_update != nullptr) s_actions.panel_update();
}

void on_upgrade_row(lv_event_t *e) {
  const int index = (int)(intptr_t)lv_event_get_user_data(e);
  if (s_actions.node_select != nullptr) s_actions.node_select(index);
}

/* ── navigation rail ───────────────────────────────────────────────────── */

lv_obj_t *nav_part(lv_obj_t *parent, int x, int y, int w, int h,
                   lv_color_t color) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
  lv_obj_set_style_bg_color(o, color, 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_clear_flag(o, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
  return o;
}

lv_obj_t *nav_dot(lv_obj_t *parent, int x, int y, int d, lv_color_t color) {
  lv_obj_t *o = nav_part(parent, x, y, d, d, color);
  lv_obj_set_style_radius(o, d / 2, 0);
  return o;
}

void nav_line(lv_obj_t *parent, const lv_point_precise_t *pts, int count,
              int x, int y, lv_color_t color) {
  lv_obj_t *l = lv_line_create(parent);
  lv_line_set_points(l, pts, count);
  lv_obj_set_pos(l, x, y);
  lv_obj_set_style_line_width(l, 2, 0);
  lv_obj_set_style_line_color(l, color, 0);
  lv_obj_set_style_line_rounded(l, true, 0);
  lv_obj_clear_flag(l, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
}

void nav_icon_grid(lv_obj_t *parent, int cx, int cy, lv_color_t color) {
  const int sq = 4;
  const int gap = 4;
  const int w = 4 * sq + 3 * gap;
  const int x0 = cx - w / 2;
  const int y0 = cy - 10;
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 4; ++c) {
      lv_obj_t *o = nav_part(parent, x0 + c * (sq + gap), y0 + r * (sq + gap),
                             sq, sq, color);
      lv_obj_set_style_radius(o, 1, 0);
    }
  }
}

void nav_icon_nodes(lv_obj_t *parent, int cx, int cy, lv_color_t color) {
  static const lv_point_precise_t diag[] = {{0, 0}, {8, 8}};
  nav_line(parent, diag, 2, cx - 9, cy - 9, color);
  nav_line(parent, diag, 2, cx + 1, cy - 9, color);
  nav_line(parent, diag, 2, cx - 9, cy + 1, color);
  nav_line(parent, diag, 2, cx + 1, cy + 1, color);
  nav_dot(parent, cx - 4, cy - 4, 8, color);
  nav_dot(parent, cx - 12, cy - 12, 5, color);
  nav_dot(parent, cx + 7, cy - 12, 5, color);
  nav_dot(parent, cx - 12, cy + 7, 5, color);
  nav_dot(parent, cx + 7, cy + 7, 5, color);
}

void nav_icon_motor(lv_obj_t *parent, int cx, int cy, lv_color_t color) {
  static const lv_point_precise_t shaft[] = {{0, 0}, {0, 16}};
  static const lv_point_precise_t up_head[] = {{0, 5}, {5, 0}, {10, 5}};
  static const lv_point_precise_t down_head[] = {{0, 0}, {5, 5}, {10, 0}};
  nav_line(parent, shaft, 2, cx - 7, cy - 8, color);
  nav_line(parent, up_head, 3, cx - 12, cy - 8, color);
  nav_line(parent, shaft, 2, cx + 7, cy - 8, color);
  nav_line(parent, down_head, 3, cx + 2, cy + 3, color);
}

void nav_icon_pulse(lv_obj_t *parent, int cx, int cy, lv_color_t color) {
  static const lv_point_precise_t pts[] = {
      {0, 10}, {7, 10}, {10, 2}, {14, 18}, {18, 10}, {28, 10}};
  nav_line(parent, pts, 6, cx - 14, cy - 9, color);
}

void nav_icon_chip(lv_obj_t *parent, int cx, int cy, lv_color_t color) {
  static const lv_point_precise_t top[] = {{0, 0}, {16, 0}};
  static const lv_point_precise_t bottom[] = {{0, 0}, {16, 0}};
  static const lv_point_precise_t left[] = {{0, 0}, {0, 16}};
  static const lv_point_precise_t right[] = {{0, 0}, {0, 16}};
  nav_line(parent, top, 2, cx - 8, cy - 8, color);
  nav_line(parent, bottom, 2, cx - 8, cy + 8, color);
  nav_line(parent, left, 2, cx - 8, cy - 8, color);
  nav_line(parent, right, 2, cx + 8, cy - 8, color);
  static const lv_point_precise_t pin[] = {{0, 0}, {4, 0}};
  for (int i = -1; i <= 1; ++i) {
    nav_line(parent, pin, 2, cx - 13, cy + i * 5, color);
    nav_line(parent, pin, 2, cx + 9, cy + i * 5, color);
  }
  nav_dot(parent, cx - 3, cy - 3, 6, color);
}

void nav_icon_sliders(lv_obj_t *parent, int cx, int cy, lv_color_t color) {
  static const lv_point_precise_t line[] = {{0, 0}, {20, 0}};
  const int knob_x[3] = {2, 13, 8};
  for (int i = 0; i < 3; ++i) {
    const int y = cy - 8 + i * 8;
    nav_line(parent, line, 2, cx - 10, y, color);
    nav_dot(parent, cx - 10 + knob_x[i], y - 2, 5, color);
  }
}

void nav_icon_symbol(lv_obj_t *parent, const char *symbol, lv_color_t color) {
  lv_obj_t *l = make_label(parent, symbol, &lv_font_montserrat_24, color);
  lv_obj_set_width(l, kRailW);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_pos(l, 0, 2);
}

/* Ordered by how often an operator needs it: SHOW every session, BUILD while
 * commissioning, CONNECT sometimes, SERVICE rarely (firmware, diagnostics,
 * system).  Four places, no action hiding behind an unrelated screen. */
int section_for_screen(int screen) {
  switch (screen) {
    case UI_SCREEN_SHOW:
    case UI_SCREEN_ANIMATIONS:
      return 0;
    case UI_SCREEN_SETUP:
    case UI_SCREEN_GRID:
    case UI_SCREEN_NODES:
    case UI_SCREEN_MOTOR:
    case UI_SCREEN_CONFIG:
      return 1;
    case UI_SCREEN_WIFI:
      return 2;
    default:
      return 3;
  }
}

int section_home(int section) {
  switch (section) {
    case 0: return UI_SCREEN_SHOW;
    case 1: return UI_SCREEN_SETUP;
    case 2: return UI_SCREEN_WIFI;
    default: return UI_SCREEN_SERVICE;
  }
}

void build_nav(int active_screen) {
  s_nav = lv_obj_create(lv_screen_active());
  lv_obj_remove_style_all(s_nav);
  lv_obj_set_pos(s_nav, kRailX, 0);
  lv_obj_set_size(s_nav, kRailW, kHeight);
  lv_obj_set_style_bg_color(s_nav, C_RAIL, 0);
  lv_obj_set_style_bg_opa(s_nav, LV_OPA_COVER, 0);
  lv_obj_clear_flag(s_nav, LV_OBJ_FLAG_SCROLLABLE);

  nav_part(s_nav, 0, 0, 1, kHeight, C_LINE);

  const int active = section_for_screen(active_screen);
  const int size = 72;
  const int count = 4;
  const int top = 20;
  const int span = kHeight - 2 * top - size;

  for (int i = 0; i < count; ++i) {
    const bool is_active = (i == active);
    const lv_color_t color = is_active ? C_ACCENT : C_MUTED;

    lv_obj_t *tab = lv_obj_create(s_nav);
    lv_obj_remove_style_all(tab);
    lv_obj_set_pos(tab, 8, top + (i * span) / (count - 1));
    lv_obj_set_size(tab, size, size);
    lv_obj_set_style_radius(tab, 18, 0);
    lv_obj_set_style_bg_color(tab, is_active ? C_PANEL2 : C_RAIL, 0);
    lv_obj_set_style_bg_opa(tab, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(tab, C_PANEL, LV_STATE_PRESSED);
    lv_obj_add_flag(tab, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(tab, on_tab, LV_EVENT_CLICKED,
                        (void *)(intptr_t)section_home(i));

    if (i == 0) {
      lv_obj_t *sym = make_label(tab, LV_SYMBOL_PLAY, &lv_font_montserrat_24,
                                 color);
      lv_obj_set_width(sym, size);
      lv_obj_set_style_text_align(sym, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_set_pos(sym, 0, 22);
    } else if (i == 1) {
      nav_icon_sliders(tab, size / 2, size / 2, color);
    } else if (i == 2) {
      lv_obj_t *sym = make_label(tab, LV_SYMBOL_WIFI, &lv_font_montserrat_24,
                                 color);
      lv_obj_set_width(sym, size);
      lv_obj_set_style_text_align(sym, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_set_pos(sym, 0, 22);
    } else {
      nav_icon_chip(tab, size / 2, size / 2, color);
    }
  }
}

/* ── screen construction ───────────────────────────────────────────────── */

lv_obj_t *make_screen(void) {
  lv_obj_t *s = lv_obj_create(lv_screen_active());
  lv_obj_remove_style_all(s);
  lv_obj_set_pos(s, 0, kStatusH);
  lv_obj_set_size(s, kScreenW, kScreenH);
  lv_obj_set_style_bg_opa(s, LV_OPA_TRANSP, 0);
  lv_obj_clear_flag(s, LV_OBJ_FLAG_SCROLLABLE);
  return s;
}

void build_status_strip(void) {
  lv_obj_t *strip = lv_obj_create(lv_screen_active());
  lv_obj_remove_style_all(strip);
  lv_obj_set_pos(strip, 0, 0);
  lv_obj_set_size(strip, kScreenW, kStatusH);
  lv_obj_set_style_bg_color(strip, C_BG, 0);
  lv_obj_set_style_bg_opa(strip, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(strip, 1, 0);
  lv_obj_set_style_border_side(strip, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_color(strip, C_LINE, 0);
  lv_obj_clear_flag(strip, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *title = make_label(strip, "SUSPENDED SPHERES", &lv_font_montserrat_12,
                               C_MUTED);
  lv_obj_set_pos(title, 12, 7);

  s_link_label = make_label(strip, "", &lv_font_montserrat_12, C_MUTED);
  lv_obj_align(s_link_label, LV_ALIGN_TOP_RIGHT, -12, 7);
}

void build_show(void) {
  lv_obj_t *s = s_screens[UI_SCREEN_SHOW];

  lv_obj_t *now = make_panel(s, 12, 8, 688, 96);
  make_label(now, "NOW PLAYING", &lv_font_montserrat_12, C_MUTED);
  s_show_pattern = make_label(now, "NO ANIMATION", &lv_font_montserrat_28,
                              C_TEXT);
  lv_obj_set_pos(s_show_pattern, 0, 20);
  lv_obj_set_width(s_show_pattern, 470);
  lv_label_set_long_mode(s_show_pattern, LV_LABEL_LONG_CLIP);
  s_show_state = make_label(now, "IDLE", &lv_font_montserrat_12, C_MUTED);
  lv_obj_align(s_show_state, LV_ALIGN_TOP_RIGHT, -2, 4);
  s_show_elapsed = make_label(now, "0:00", &lv_font_montserrat_20, C_MUTED);
  lv_obj_align(s_show_elapsed, LV_ALIGN_TOP_RIGHT, -2, 30);
  s_show_progress = lv_bar_create(now);
  lv_obj_set_pos(s_show_progress, 0, 64);
  lv_obj_set_size(s_show_progress, 640, 4);
  lv_bar_set_range(s_show_progress, 0, 1000);
  lv_bar_set_value(s_show_progress, 0, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(s_show_progress, C_OFF, 0);
  lv_obj_set_style_bg_color(s_show_progress, C_ACCENT, LV_PART_INDICATOR);
  lv_obj_set_style_radius(s_show_progress, 3, 0);
  lv_obj_set_style_radius(s_show_progress, 3, LV_PART_INDICATOR);

  lv_obj_t *transport = make_panel(s, 12, 112, 688, 64);
  s_show_play = make_button(transport, "PLAY", 92, 40, on_anim_load, nullptr);
  lv_obj_set_style_bg_color(s_show_play, C_ACCENT, 0);
  lv_obj_set_style_border_color(s_show_play, C_ACCENT, 0);
  lv_obj_set_pos(s_show_play, 8, 0);
  s_show_pause = make_button(transport, "PAUSE", 96, 40, on_anim_pause,
                             nullptr);
  lv_obj_set_pos(s_show_pause, 108, 0);
  s_show_stop = make_button(transport, "STOP", 84, 40, on_anim_stop,
                            nullptr);
  lv_obj_set_pos(s_show_stop, 212, 0);
  s_show_cycle = make_button(transport, "CYCLE OFF", 104, 40, on_anim_cycle,
                             nullptr);
  lv_obj_set_pos(s_show_cycle, 304, 0);
  s_show_transport = make_label(transport, "idle", &lv_font_montserrat_12,
                                C_MUTED);
  lv_obj_set_pos(s_show_transport, 424, 12);
  lv_obj_set_width(s_show_transport, 228);
  lv_label_set_long_mode(s_show_transport, LV_LABEL_LONG_WRAP);

  lv_obj_t *playlist = make_panel(s, 12, 184, 332, 220);
  make_label(playlist, "PLAYLIST", &lv_font_montserrat_14, C_MUTED);
  s_show_anim_summary = make_label(playlist, "", &lv_font_montserrat_12,
                                   C_MUTED);
  lv_obj_align(s_show_anim_summary, LV_ALIGN_TOP_RIGHT, -2, 0);
  s_show_list = lv_obj_create(playlist);
  lv_obj_remove_style_all(s_show_list);
  lv_obj_set_pos(s_show_list, 0, 30);
  lv_obj_set_size(s_show_list, 260, 154);
  lv_obj_set_style_bg_opa(s_show_list, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_row(s_show_list, 4, 0);
  lv_obj_set_flex_flow(s_show_list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(s_show_list, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(s_show_list, LV_SCROLLBAR_MODE_OFF);
  add_scroll_buttons(playlist, s_show_list, 268, 30, 32, 34, 8, true);

  lv_obj_t *library = make_panel(s, 356, 184, 344, 220);
  make_label(library, "LIBRARY", &lv_font_montserrat_14, C_MUTED);
  s_show_library_summary = make_label(library, "", &lv_font_montserrat_12,
                                      C_MUTED);
  lv_obj_align(s_show_library_summary, LV_ALIGN_TOP_RIGHT, -2, 0);
  s_show_library = lv_obj_create(library);
  lv_obj_remove_style_all(s_show_library);
  lv_obj_set_pos(s_show_library, 0, 30);
  lv_obj_set_size(s_show_library, 272, 154);
  lv_obj_set_style_bg_opa(s_show_library, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_row(s_show_library, 4, 0);
  lv_obj_set_flex_flow(s_show_library, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(s_show_library, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(s_show_library, LV_SCROLLBAR_MODE_OFF);
  add_scroll_buttons(library, s_show_library, 280, 30, 34, 34, 8, true);
  s_show_list_hash = 0;
  s_show_playlist_panel = playlist;
  s_show_library_panel = library;

  /* While the show runs, the safety stop takes the library slot.  The
   * playlist on the left never moves, so the selected animation stays
   * visible and the target is still large and square. */
  s_show_big_stop = lv_button_create(s);
  lv_obj_set_pos(s_show_big_stop, 356, 184);
  lv_obj_set_size(s_show_big_stop, 344, 220);
  lv_obj_set_style_bg_color(s_show_big_stop, C_ERR, 0);
  lv_obj_set_style_bg_opa(s_show_big_stop, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(s_show_big_stop, 0, 0);
  lv_obj_set_style_radius(s_show_big_stop, 12, 0);
  lv_obj_set_style_shadow_width(s_show_big_stop, 0, 0);
  lv_obj_add_event_cb(s_show_big_stop, on_anim_stop_now, LV_EVENT_CLICKED,
                      nullptr);
  lv_obj_t *stop_lbl = make_label(s_show_big_stop, "STOP",
                                  &lv_font_montserrat_48, lv_color_white());
  lv_obj_set_width(stop_lbl, 320);
  lv_obj_set_style_text_align(stop_lbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(stop_lbl, LV_ALIGN_CENTER, 0, -12);
  lv_obj_t *stop_sub = make_label(s_show_big_stop, "IMMEDIATE STOP",
                                  &lv_font_montserrat_14, lv_color_white());
  lv_obj_set_width(stop_sub, 320);
  lv_obj_set_style_text_align(stop_sub, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(stop_sub, LV_ALIGN_CENTER, 0, 40);
  lv_obj_add_flag(s_show_big_stop, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t *status = make_panel(s, 12, 412, 688, 32);
  lv_obj_set_style_pad_all(status, 8, 0);
  s_show_status = make_label(status, "", &lv_font_montserrat_12, C_MUTED);
  lv_obj_set_pos(s_show_status, 0, 4);
  lv_obj_set_width(s_show_status, 656);
  lv_label_set_long_mode(s_show_status, LV_LABEL_LONG_CLIP);
  s_show_bus = s_show_status;
  s_show_hold = make_label(status, "", &lv_font_montserrat_12, C_ERR);
  lv_obj_set_pos(s_show_hold, 0, 4);
  lv_obj_add_flag(s_show_hold, LV_OBJ_FLAG_HIDDEN);
}

void build_animations(void) {
  lv_obj_t *s = s_screens[UI_SCREEN_ANIMATIONS];
  lv_obj_t *head = make_panel(s, 12, 8, 688, 48);
  make_label(head, "LIBRARY", &lv_font_montserrat_20, C_TEXT);
  s_anim_summary = make_label(head, "", &lv_font_montserrat_12, C_MUTED);
  lv_obj_align(s_anim_summary, LV_ALIGN_RIGHT_MID, -12, 0);

  lv_obj_t *controls = make_panel(s, 12, 64, 688, 84);
  lv_obj_t *refresh = make_button(controls, "REFRESH", 140, 44,
                                  on_anim_refresh, nullptr);
  lv_obj_set_pos(refresh, 8, 8);
  lv_obj_t *load = make_button(controls, "LOAD SELECTED", 190, 44,
                               on_anim_load, nullptr);
  lv_obj_set_pos(load, 156, 8);
  s_anim_status = make_label(controls, "idle", &lv_font_montserrat_12, C_MUTED);
  lv_obj_set_pos(s_anim_status, 356, 10);
  lv_label_set_long_mode(s_anim_status, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(s_anim_status, 200);

  lv_obj_t *list = lv_obj_create(s);
  lv_obj_remove_style_all(list);
  lv_obj_set_pos(list, 12, 156);
  lv_obj_set_size(list, 688, 288);
  lv_obj_set_style_bg_color(list, C_PANEL, 0);
  lv_obj_set_style_bg_opa(list, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(list, 8, 0);
  lv_obj_set_style_border_width(list, 1, 0);
  lv_obj_set_style_border_color(list, C_LINE, 0);
  lv_obj_set_style_pad_all(list, 8, 0);
  lv_obj_set_style_pad_row(list, 6, 0);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_ACTIVE);
  s_anim_list = list;
  s_anim_shown_hash = 0;
  add_scroll_buttons(controls, list, 568, 8, 48, 48, 8, false);
}

void build_grid(void) {
  lv_obj_t *s = s_screens[UI_SCREEN_GRID];
  lv_obj_t *head = make_panel(s, 12, 8, 688, 58);
  make_back_button(head, UI_SCREEN_SETUP);
  lv_obj_t *title = make_label(head, "GRID", &lv_font_montserrat_20, C_TEXT);
  lv_obj_set_pos(title, 44, 0);
  s_grid_summary = make_label(head, "", &lv_font_montserrat_14, C_MUTED);
  lv_obj_set_pos(s_grid_summary, 150, 8);
  lv_obj_t *hint = make_label(head, "row 0 = back, column 0 = left",
                              &lv_font_montserrat_12, C_MUTED);
  lv_obj_align(hint, LV_ALIGN_RIGHT_MID, -12, 0);

  lv_obj_t *body = lv_obj_create(s);
  lv_obj_remove_style_all(body);
  lv_obj_set_pos(body, 12, 72);
  lv_obj_set_size(body, 688, 372);
  lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);
  s_grid_cols = 0;
  s_grid_rows = 0;
}

void rebuild_grid(int cols, int rows) {
  if (cols == s_grid_cols && rows == s_grid_rows) return;
  s_grid_cols = cols;
  s_grid_rows = rows;

  lv_obj_t *body = lv_obj_get_child(s_screens[UI_SCREEN_GRID], 1);
  lv_obj_clean(body);

  if (cols < 1 || rows < 1) return;
  const int gap = 4;
  const int cw = (660 - (cols - 1) * gap) / cols;
  const int ch = (344 - (rows - 1) * gap) / rows;
  const int gw = cols * cw + (cols - 1) * gap;
  const int gh = rows * ch + (rows - 1) * gap;
  const int x0 = (688 - gw) / 2;
  const int y0 = (372 - gh) / 2;

  for (int i = 0; i < UI_MAX_POSITIONS; ++i) s_grid_cells[i] = nullptr;
  const int cap = cols * rows;
  for (int r = 0; r < rows; ++r) {
    for (int c = 0; c < cols; ++c) {
      const int pos = r * cols + c;
      if (pos >= cap) continue;
      lv_obj_t *cell = make_button(body, "", cw, ch, on_grid_cell,
                                   (void *)(intptr_t)pos);
      lv_obj_set_pos(cell, x0 + c * (cw + gap), y0 + r * (ch + gap));
      lv_obj_set_style_radius(cell, 4, 0);
      lv_obj_set_style_bg_color(cell, C_OFF, 0);
      lv_obj_t *l = lv_obj_get_child(cell, 0);
      lv_label_set_text_fmt(l, "%d", pos);
      lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
      lv_obj_set_style_text_color(l, C_MUTED, 0);
      s_grid_cells[pos] = cell;
    }
  }
}

void build_nodes(void) {
  lv_obj_t *s = s_screens[UI_SCREEN_NODES];
  lv_obj_t *head = make_panel(s, 12, 8, 688, 58);
  make_back_button(head, UI_SCREEN_SETUP);
  lv_obj_t *title = make_label(head, "BOARDS", &lv_font_montserrat_20, C_TEXT);
  lv_obj_set_pos(title, 44, 0);
  s_nodes_summary = make_label(head, "", &lv_font_montserrat_12, C_MUTED);
  lv_obj_align(s_nodes_summary, LV_ALIGN_RIGHT_MID, -120, 0);

  lv_obj_t *list = lv_obj_create(s);
  lv_obj_remove_style_all(list);
  lv_obj_set_pos(list, 12, 72);
  lv_obj_set_size(list, 688, 372);
  lv_obj_set_style_bg_color(list, C_PANEL, 0);
  lv_obj_set_style_bg_opa(list, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(list, 8, 0);
  lv_obj_set_style_border_width(list, 1, 0);
  lv_obj_set_style_border_color(list, C_LINE, 0);
  lv_obj_set_style_pad_all(list, 8, 0);
  lv_obj_set_style_pad_row(list, 6, 0);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_ACTIVE);
  s_nodes_list = list;
  s_nodes_shown_hash = 0;
  add_scroll_buttons(head, list, 560, 2, 44, 30, 12, false);
}

void build_motor(void) {
  lv_obj_t *s = s_screens[UI_SCREEN_MOTOR];
  lv_obj_t *head = make_panel(s, 12, 8, 688, 64);
  make_back_button(head, UI_SCREEN_SETUP);
  lv_obj_t *title = make_label(head, "ALIGN MOTORS", &lv_font_montserrat_20,
                               C_TEXT);
  lv_obj_set_pos(title, 44, 0);
  s_motor_node = make_label(head, "no board", &lv_font_montserrat_14, C_TEXT);
  lv_obj_set_pos(s_motor_node, 12, 34);
  s_motor_flash = make_label(head, "", &lv_font_montserrat_12, C_MUTED);
  lv_obj_set_pos(s_motor_flash, 200, 36);
  s_motor_board = make_label(head, "", &lv_font_montserrat_12, C_MUTED);
  lv_obj_set_pos(s_motor_board, 440, 36);
  lv_obj_t *prev = make_button(head, "<", 36, 36, on_node_prev, nullptr);
  lv_obj_set_pos(prev, 580, 8);
  lv_obj_t *next = make_button(head, ">", 36, 36, on_node_next, nullptr);
  lv_obj_set_pos(next, 622, 8);

  lv_obj_t *axis = make_panel(s, 12, 80, 332, 112);
  make_label(axis, "green = assigned; ring = selected",
             &lv_font_montserrat_12, C_MUTED);
  for (int i = 0; i < 4; ++i) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "AX %d", i);
    s_motor_axis_btn[i] = make_button(axis, buf, 68, 44, on_axis,
                                      (void *)(intptr_t)i);
    lv_obj_set_pos(s_motor_axis_btn[i], 8 + i * 75, 42);
  }

  lv_obj_t *jog = make_panel(s, 356, 80, 344, 112);
  make_label(jog, "JOG / HOME", &lv_font_montserrat_14, C_MUTED);
  lv_obj_t *home = make_button(jog, "HOME", 72, 46, on_motor, (void *)0);
  lv_obj_set_pos(home, 8, 42);
  lv_obj_t *up = make_button(jog, "UP", 72, 46, on_motor, (void *)1);
  lv_obj_set_pos(up, 86, 42);
  lv_obj_t *down = make_button(jog, "DOWN", 72, 46, on_motor, (void *)2);
  lv_obj_set_pos(down, 164, 42);
  lv_obj_t *stop = make_button(jog, "STOP", 72, 46, on_motor, (void *)3);
  lv_obj_set_pos(stop, 242, 42);

  lv_obj_t *assign = make_panel(s, 12, 200, 688, 244);
  make_label(assign, "ASSIGN TO GRID POSITION", &lv_font_montserrat_14, C_MUTED);

  lv_obj_t *grid = lv_obj_create(assign);
  lv_obj_remove_style_all(grid);
  lv_obj_set_pos(grid, 0, 26);
  lv_obj_set_size(grid, 404, 166);
  lv_obj_set_style_bg_color(grid, C_BG, 0);
  lv_obj_set_style_bg_opa(grid, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(grid, 6, 0);
  lv_obj_set_style_border_width(grid, 1, 0);
  lv_obj_set_style_border_color(grid, C_LINE, 0);
  lv_obj_set_scroll_dir(grid, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(grid, LV_SCROLLBAR_MODE_ACTIVE);
  s_motor_grid = grid;
  s_motor_cols = 0;
  s_motor_rows = 0;
  add_scroll_buttons(assign, grid, 0, 196, 52, 24, 8, false);

  lv_obj_t *pos_lbl = make_label(assign, "POSITION", &lv_font_montserrat_12,
                                 C_MUTED);
  lv_obj_set_pos(pos_lbl, 420, 12);
  s_motor_assign_pos = make_label(assign, "0", &lv_font_montserrat_28, C_TEXT);
  lv_obj_set_pos(s_motor_assign_pos, 420, 26);
  s_motor_result = make_label(assign, "", &lv_font_montserrat_12, C_MUTED);
  lv_obj_set_pos(s_motor_result, 420, 72);
  lv_label_set_long_mode(s_motor_result, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(s_motor_result, 236);
  lv_obj_t *set = make_button(assign, "ASSIGN", 236, 40, on_assign, nullptr);
  lv_obj_set_pos(set, 420, 128);
  lv_obj_t *unset = make_button(assign, "UNASSIGN", 236, 40, on_unassign,
                                nullptr);
  lv_obj_set_pos(unset, 420, 176);
}

void rebuild_motor_grid(int cols, int rows) {
  if (cols == s_motor_cols && rows == s_motor_rows) return;
  s_motor_cols = cols;
  s_motor_rows = rows;

  lv_obj_clean(s_motor_grid);
  for (int i = 0; i < UI_MAX_POSITIONS; ++i) s_motor_cells[i] = nullptr;
  if (cols < 1 || rows < 1) return;

  const int gap = 3;
  const int cw = 36;
  const int ch = 26;
  const int gw = cols * cw + (cols - 1) * gap;
  const int x0 = (404 - gw) / 2;
  const int cap = cols * rows;
  for (int r = 0; r < rows; ++r) {
    for (int c = 0; c < cols; ++c) {
      const int pos = r * cols + c;
      if (pos >= cap) continue;
      lv_obj_t *cell = make_button(s_motor_grid, "", cw, ch, on_motor_pos,
                                   (void *)(intptr_t)pos);
      lv_obj_set_pos(cell, x0 + c * (cw + gap), r * (ch + gap));
      lv_obj_set_style_radius(cell, 3, 0);
      lv_obj_t *l = lv_obj_get_child(cell, 0);
      lv_label_set_text_fmt(l, "%d", pos);
      lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
      s_motor_cells[pos] = cell;
    }
  }
}

void build_config(void) {
  lv_obj_t *s = s_screens[UI_SCREEN_CONFIG];
  lv_obj_t *head = make_panel(s, 12, 8, 688, 48);
  make_back_button(head, UI_SCREEN_SETUP);
  lv_obj_t *title = make_label(head, "TEST & CONFIG", &lv_font_montserrat_20,
                               C_TEXT);
  lv_obj_set_pos(title, 44, 0);
  lv_obj_t *hint = make_label(head, "saved to SPIFFS", &lv_font_montserrat_12,
                              C_MUTED);
  lv_obj_align(hint, LV_ALIGN_RIGHT_MID, -12, 0);

  lv_obj_t *grid = make_panel(s, 12, 64, 332, 178);
  make_label(grid, "GRID SIZE", &lv_font_montserrat_14, C_MUTED);

  lv_obj_t *cols_lbl = make_label(grid, "columns", &lv_font_montserrat_12,
                                  C_MUTED);
  lv_obj_set_pos(cols_lbl, 0, 38);
  lv_obj_t *cm = make_button(grid, "-", 42, 36, on_grid_minus, nullptr);
  lv_obj_set_pos(cm, 96, 30);
  s_cfg_cols = make_label(grid, "10", &lv_font_montserrat_20, C_TEXT);
  lv_obj_set_width(s_cfg_cols, 88);
  lv_obj_set_style_text_align(s_cfg_cols, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_pos(s_cfg_cols, 150, 32);
  lv_obj_t *cp = make_button(grid, "+", 42, 36, on_grid_plus, nullptr);
  lv_obj_set_pos(cp, 250, 30);

  lv_obj_t *rows_lbl = make_label(grid, "rows", &lv_font_montserrat_12,
                                  C_MUTED);
  lv_obj_set_pos(rows_lbl, 0, 76);
  lv_obj_t *rm = make_button(grid, "-", 42, 36, on_rows_minus, nullptr);
  lv_obj_set_pos(rm, 96, 68);
  s_cfg_rows = make_label(grid, "12", &lv_font_montserrat_20, C_TEXT);
  lv_obj_set_width(s_cfg_rows, 88);
  lv_obj_set_style_text_align(s_cfg_rows, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_pos(s_cfg_rows, 150, 70);
  lv_obj_t *rp = make_button(grid, "+", 42, 36, on_rows_plus, nullptr);
  lv_obj_set_pos(rp, 250, 68);

  lv_obj_t *presets = make_label(grid, "PRESETS", &lv_font_montserrat_12,
                                 C_MUTED);
  lv_obj_set_pos(presets, 0, 112);
  lv_obj_t *p10 = make_button(grid, "10 x 10", 100, 30, on_preset_10x10,
                              nullptr);
  lv_obj_set_pos(p10, 0, 128);
  lv_obj_t *p12 = make_button(grid, "10 x 12", 100, 30, on_preset_10x12,
                              nullptr);
  lv_obj_set_pos(p12, 108, 128);

  lv_obj_t *test = make_panel(s, 356, 64, 344, 178);
  make_label(test, "COMMISSIONING TEST RUN", &lv_font_montserrat_14, C_MUTED);
  static const struct {
    const char *label;
    int y;
  } rows_ui[3] = {{"drop mm", 26}, {"speed mm/s", 58}, {"gap ms", 90}};
  lv_obj_t *drop_lbl = make_label(test, rows_ui[0].label,
                                  &lv_font_montserrat_12, C_MUTED);
  lv_obj_set_pos(drop_lbl, 0, rows_ui[0].y + 4);
  lv_obj_t *dm = make_button(test, "-", 40, 30, on_drop_minus, nullptr);
  lv_obj_set_pos(dm, 96, rows_ui[0].y);
  s_cfg_drop = make_label(test, "100", &lv_font_montserrat_16, C_TEXT);
  lv_obj_set_width(s_cfg_drop, 88);
  lv_obj_set_style_text_align(s_cfg_drop, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_pos(s_cfg_drop, 144, rows_ui[0].y + 2);
  lv_obj_t *dp = make_button(test, "+", 40, 30, on_drop_plus, nullptr);
  lv_obj_set_pos(dp, 240, rows_ui[0].y);

  lv_obj_t *speed_lbl = make_label(test, rows_ui[1].label,
                                   &lv_font_montserrat_12, C_MUTED);
  lv_obj_set_pos(speed_lbl, 0, rows_ui[1].y + 4);
  lv_obj_t *sm = make_button(test, "-", 40, 30, on_speed_minus, nullptr);
  lv_obj_set_pos(sm, 96, rows_ui[1].y);
  s_cfg_speed = make_label(test, "20", &lv_font_montserrat_16, C_TEXT);
  lv_obj_set_width(s_cfg_speed, 88);
  lv_obj_set_style_text_align(s_cfg_speed, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_pos(s_cfg_speed, 144, rows_ui[1].y + 2);
  lv_obj_t *sp = make_button(test, "+", 40, 30, on_speed_plus, nullptr);
  lv_obj_set_pos(sp, 240, rows_ui[1].y);

  lv_obj_t *gap_lbl = make_label(test, rows_ui[2].label,
                                 &lv_font_montserrat_12, C_MUTED);
  lv_obj_set_pos(gap_lbl, 0, rows_ui[2].y + 4);
  lv_obj_t *gm = make_button(test, "-", 40, 30, on_delay_minus, nullptr);
  lv_obj_set_pos(gm, 96, rows_ui[2].y);
  s_cfg_delay = make_label(test, "300", &lv_font_montserrat_16, C_TEXT);
  lv_obj_set_width(s_cfg_delay, 88);
  lv_obj_set_style_text_align(s_cfg_delay, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_pos(s_cfg_delay, 144, rows_ui[2].y + 2);
  lv_obj_t *gp = make_button(test, "+", 40, 30, on_delay_plus, nullptr);
  lv_obj_set_pos(gp, 240, rows_ui[2].y);

  s_cfg_serp = make_button(test, "SERPENTINE ON", 190, 30, on_serp, nullptr);
  lv_obj_set_pos(s_cfg_serp, 0, 124);
  lv_obj_set_style_text_font(lv_obj_get_child(s_cfg_serp, 0),
                             &lv_font_montserrat_12, 0);

  lv_obj_t *actions = make_panel(s, 12, 250, 688, 180);
  lv_obj_t *save = make_button(actions, "SAVE TO SPIFFS", 180, 42, on_save,
                               nullptr);
  lv_obj_set_pos(save, 8, 8);
  lv_obj_t *reset = make_button(actions, "RESET DEFAULTS", 170, 42, on_reset,
                                nullptr);
  lv_obj_set_pos(reset, 196, 8);
  lv_obj_t *clear = make_button(actions, "CLEAR MAPPING", 160, 42, on_clear_map,
                                nullptr);
  lv_obj_set_pos(clear, 374, 8);
  s_cfg_status = make_label(actions, "", &lv_font_montserrat_12, C_MUTED);
  lv_obj_set_pos(s_cfg_status, 548, 22);
  lv_obj_t *test_head = make_label(actions, "COMMISSIONING TEST",
                                   &lv_font_montserrat_12, C_MUTED);
  lv_obj_set_pos(test_head, 8, 62);
  lv_obj_t *run = make_button(actions, "RUN TEST", 140, 40, on_test_start,
                              nullptr);
  lv_obj_set_pos(run, 8, 84);
  lv_obj_t *stop = make_button(actions, "STOP TEST", 120, 40, on_test_stop,
                               nullptr);
  lv_obj_set_pos(stop, 156, 84);
}

void build_log(void) {
  lv_obj_t *s = s_screens[UI_SCREEN_LOG];
  lv_obj_t *head = make_panel(s, 12, 8, 688, 48);
  make_back_button(head, UI_SCREEN_SERVICE);
  lv_obj_t *title = make_label(head, "LOG", &lv_font_montserrat_20, C_TEXT);
  lv_obj_set_pos(title, 44, 0);
  lv_obj_t *clear = make_button(head, "CLEAR", 100, 36, on_log_clear, nullptr);
  lv_obj_align(clear, LV_ALIGN_RIGHT_MID, -12, 0);
  s_log_status = make_label(head, "", &lv_font_montserrat_12, C_MUTED);
  lv_obj_align(s_log_status, LV_ALIGN_LEFT_MID, 120, 0);

  s_log_text = lv_textarea_create(s);
  lv_obj_set_pos(s_log_text, 12, 64);
  lv_obj_set_size(s_log_text, 624, 380);
  lv_textarea_set_text(s_log_text, "");
  lv_obj_set_style_bg_color(s_log_text, C_PANEL, 0);
  lv_obj_set_style_bg_opa(s_log_text, LV_OPA_COVER, 0);
  lv_obj_set_style_text_color(s_log_text, C_TEXT, 0);
  lv_obj_set_style_text_font(s_log_text, &lv_font_montserrat_12, 0);
  lv_obj_set_style_pad_left(s_log_text, 8, 0);
  lv_obj_set_style_pad_top(s_log_text, 8, 0);
  lv_obj_set_style_border_color(s_log_text, C_LINE, 0);
  lv_obj_set_style_border_width(s_log_text, 1, 0);
  lv_obj_clear_flag(s_log_text, LV_OBJ_FLAG_CLICKABLE);
  add_scroll_buttons(s, s_log_text, 644, 196, 48, 48, 18, true);
}

void build_can(void) {
  lv_obj_t *s = s_screens[UI_SCREEN_CAN];
  lv_obj_t *head = make_panel(s, 12, 8, 688, 48);
  make_back_button(head, UI_SCREEN_SERVICE);
  lv_obj_t *title = make_label(head, "CAN SCOPE", &lv_font_montserrat_20,
                               C_TEXT);
  lv_obj_set_pos(title, 44, 0);
  s_can_bus = make_label(head, "BUS --", &lv_font_montserrat_12, C_MUTED);
  lv_obj_align(s_can_bus, LV_ALIGN_LEFT_MID, 196, 0);
  s_can_summary = make_label(head, "", &lv_font_montserrat_12, C_MUTED);
  lv_obj_align(s_can_summary, LV_ALIGN_LEFT_MID, 272, 0);
  lv_obj_t *clear = make_button(head, "CLEAR", 100, 36, on_can_clear, nullptr);
  lv_obj_align(clear, LV_ALIGN_RIGHT_MID, -12, 0);

  /* Fixed label columns. A proportional font cannot keep space-padded
   * columns aligned, so every field has its own x and the rows are a small
   * pool of labels that is refilled from the first visible entry. */
  const lv_font_t *can_font = &lv_font_montserrat_16;
  lv_obj_t *header = make_panel(s, 12, 62, 624, 32);
  lv_obj_set_style_pad_all(header, 0, 0);
  lv_obj_set_style_bg_color(header, C_PANEL2, 0);
  lv_obj_t *h_id = make_label(header, "ID", can_font, C_MUTED);
  lv_obj_set_pos(h_id, kCanIdX, 5);
  lv_obj_t *h_type = make_label(header, "TYPE", can_font, C_MUTED);
  lv_obj_set_pos(h_type, kCanTypeX, 5);
  lv_obj_t *h_cnt = make_label(header, "CNT", can_font, C_MUTED);
  lv_obj_set_width(h_cnt, kCanCntW);
  lv_obj_set_style_text_align(h_cnt, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_pos(h_cnt, kCanCntX, 5);
  lv_obj_t *h_data = make_label(header, "DATA", can_font, C_MUTED);
  lv_obj_set_pos(h_data, kCanDataX, 5);

  lv_obj_t *body = lv_obj_create(s);
  lv_obj_remove_style_all(body);
  lv_obj_set_pos(body, 12, 98);
  lv_obj_set_size(body, 624, 346);
  lv_obj_set_style_bg_color(body, C_PANEL, 0);
  lv_obj_set_style_bg_opa(body, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(body, 6, 0);
  lv_obj_set_style_border_width(body, 1, 0);
  lv_obj_set_style_border_color(body, C_LINE, 0);
  lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);

  s_can_empty = make_label(body, "no CAN frames yet", can_font, C_MUTED);
  lv_obj_align(s_can_empty, LV_ALIGN_CENTER, 0, 0);
  lv_obj_add_flag(s_can_empty, LV_OBJ_FLAG_HIDDEN);

  for (int i = 0; i < kCanRows; ++i) {
    const int y = 4 + i * kCanRowH;
    s_can_row_id[i] = make_label(body, "", can_font, C_TEXT);
    lv_obj_set_pos(s_can_row_id[i], kCanIdX, y);
    s_can_row_type[i] = make_label(body, "", can_font, C_MUTED);
    lv_obj_set_pos(s_can_row_type[i], kCanTypeX, y);
    s_can_row_cnt[i] = make_label(body, "", can_font, C_TEXT);
    lv_obj_set_width(s_can_row_cnt[i], kCanCntW);
    lv_obj_set_style_text_align(s_can_row_cnt[i], LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(s_can_row_cnt[i], kCanCntX, y);
    s_can_row_data[i] = make_label(body, "", can_font, C_TEXT);
    lv_obj_set_pos(s_can_row_data[i], kCanDataX, y);
    lv_obj_add_flag(s_can_row_id[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_can_row_type[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_can_row_cnt[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_can_row_data[i], LV_OBJ_FLAG_HIDDEN);
  }
  s_can_first = 0;

  lv_obj_t *up = make_button(s, LV_SYMBOL_UP, 48, 48, on_can_page_up, nullptr);
  lv_obj_set_pos(up, 644, 196);
  lv_obj_t *down = make_button(s, LV_SYMBOL_DOWN, 48, 48, on_can_page_down,
                               nullptr);
  lv_obj_set_pos(down, 644, 262);
}

void build_upgrade(void) {
  lv_obj_t *s = s_screens[UI_SCREEN_UPGRADE];
  lv_obj_t *head = make_panel(s, 12, 8, 688, 48);
  make_back_button(head, UI_SCREEN_SERVICE);
  lv_obj_t *title = make_label(head, "FIRMWARE", &lv_font_montserrat_20,
                               C_TEXT);
  lv_obj_set_pos(title, 44, 0);
  s_upgrade_summary = make_label(head, "", &lv_font_montserrat_12, C_MUTED);
  lv_obj_align(s_upgrade_summary, LV_ALIGN_RIGHT_MID, -12, 0);

  lv_obj_t *controls = make_panel(s, 12, 62, 688, 92);
  lv_obj_t *query = make_button(controls, "QUERY ALL", 116, 40,
                                on_version_query, nullptr);
  lv_obj_set_pos(query, 8, 8);
  lv_obj_t *all = make_button(controls, "UPGRADE ALL OUTDATED", 200, 40,
                              on_upgrade_all, nullptr);
  lv_obj_set_pos(all, 132, 8);
  lv_obj_t *sel = make_button(controls, "UPGRADE SELECTED", 180, 40,
                              on_upgrade_start, nullptr);
  lv_obj_set_style_bg_color(sel, C_ACCENT, 0);
  lv_obj_set_style_border_color(sel, C_ACCENT, 0);
  lv_obj_set_pos(sel, 340, 8);
  s_upgrade_stop = make_button(controls, "STOP", 80, 40, on_upgrade_stop,
                               nullptr);
  lv_obj_set_pos(s_upgrade_stop, 528, 8);
  s_upgrade_status = make_label(controls, "idle", &lv_font_montserrat_12, C_MUTED);
  lv_obj_set_pos(s_upgrade_status, 8, 54);
  lv_label_set_long_mode(s_upgrade_status, LV_LABEL_LONG_CLIP);
  lv_obj_set_width(s_upgrade_status, 400);
  s_upgrade_progress = lv_bar_create(controls);
  lv_obj_set_pos(s_upgrade_progress, 416, 58);
  lv_obj_set_size(s_upgrade_progress, 240, 8);
  lv_bar_set_range(s_upgrade_progress, 0, 100);
  lv_bar_set_value(s_upgrade_progress, 0, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(s_upgrade_progress, C_OFF, 0);
  lv_obj_set_style_bg_color(s_upgrade_progress, C_ACCENT, LV_PART_INDICATOR);
  lv_obj_set_style_radius(s_upgrade_progress, 4, 0);
  lv_obj_set_style_radius(s_upgrade_progress, 4, LV_PART_INDICATOR);

  /* One row for both firmware sources: the panel image on SD and the
   * GitHub release.  A panel update belongs here, with the node image,
   * not under SYSTEM. */
  lv_obj_t *src = make_panel(s, 12, 158, 688, 44);
  lv_obj_set_style_pad_all(src, 4, 0);
  lv_obj_t *pl = make_label(src, "PANEL", &lv_font_montserrat_12, C_MUTED);
  lv_obj_set_pos(pl, 0, 10);
  s_upg_panel_update = make_button(src, "FROM SD", 88, 28, on_panel_update,
                                   nullptr);
  lv_obj_set_pos(s_upg_panel_update, 44, 0);
  lv_obj_set_style_text_font(s_upg_panel_update, &lv_font_montserrat_12, 0);
  s_upg_panel_status = make_label(src, "", &lv_font_montserrat_12, C_MUTED);
  lv_obj_set_pos(s_upg_panel_status, 138, 6);
  lv_obj_set_width(s_upg_panel_status, 156);
  lv_label_set_long_mode(s_upg_panel_status, LV_LABEL_LONG_CLIP);

  lv_obj_t *gl = make_label(src, "GITHUB", &lv_font_montserrat_12, C_MUTED);
  lv_obj_set_pos(gl, 304, 10);
  s_upg_github_check = make_button(src, "CHECK", 64, 28, on_github_check,
                                   nullptr);
  lv_obj_set_pos(s_upg_github_check, 356, 0);
  lv_obj_set_style_text_font(s_upg_github_check, &lv_font_montserrat_12, 0);
  s_upg_github_install = make_button(src, "INSTALL", 70, 28, on_github_install,
                                     nullptr);
  lv_obj_set_pos(s_upg_github_install, 426, 0);
  lv_obj_set_style_text_font(s_upg_github_install, &lv_font_montserrat_12, 0);
  s_upg_github_status = make_label(src, "not queried",
                                   &lv_font_montserrat_12, C_MUTED);
  lv_obj_set_pos(s_upg_github_status, 502, 6);
  lv_obj_set_width(s_upg_github_status, 166);
  lv_label_set_long_mode(s_upg_github_status, LV_LABEL_LONG_CLIP);

  /* 30 tiles on one page: 6 columns by 5 rows, no second page and no
   * scrolling list (firmware_release_whitepaper.md §4.10). */
  lv_obj_t *tiles = lv_obj_create(s);
  lv_obj_remove_style_all(tiles);
  lv_obj_set_pos(tiles, 12, 206);
  lv_obj_set_size(tiles, 688, 242);
  lv_obj_set_style_bg_opa(tiles, LV_OPA_TRANSP, 0);
  lv_obj_clear_flag(tiles, LV_OBJ_FLAG_SCROLLABLE);
  s_upgrade_tiles = tiles;
  s_upgrade_shown_hash = 0;
}

void build_wifi(void) {
  lv_obj_t *s = s_screens[UI_SCREEN_WIFI];
  lv_obj_t *head = make_panel(s, 12, 8, 688, 48);
  make_label(head, "NETWORK", &lv_font_montserrat_20, C_TEXT);
  s_wifi_status = make_label(head, "OFF", &lv_font_montserrat_12, C_MUTED);
  lv_obj_align(s_wifi_status, LV_ALIGN_RIGHT_MID, -12, 0);
  lv_obj_set_width(s_wifi_status, 420);
  lv_obj_set_style_text_align(s_wifi_status, LV_TEXT_ALIGN_RIGHT, 0);

  lv_obj_t *mode = make_panel(s, 12, 62, 688, 64);
  make_label(mode, "MODE", &lv_font_montserrat_12, C_MUTED);
  lv_obj_set_pos(lv_obj_get_child(mode, 0), 0, 8);
  s_wifi_mode_off = make_button(mode, "OFF", 86, 30, on_wifi_off, nullptr);
  lv_obj_set_pos(s_wifi_mode_off, 54, 2);
  s_wifi_mode_connect = make_button(mode, "CONNECT", 116, 30, on_wifi_join,
                                    nullptr);
  lv_obj_set_pos(s_wifi_mode_connect, 146, 2);
  s_wifi_mode_ap = make_button(mode, "AP", 76, 30, on_wifi_ap, nullptr);
  lv_obj_set_pos(s_wifi_mode_ap, 268, 2);
  lv_obj_t *mode_hint = make_label(
      mode, "OFF at reset; CONNECT retries; AP uploads.",
      &lv_font_montserrat_12, C_MUTED);
  lv_obj_set_pos(mode_hint, 360, 7);
  lv_obj_set_width(mode_hint, 292);

  lv_obj_t *list = make_panel(s, 12, 132, 300, 312);
  s_wifi_list_panel = list;
  s_wifi_list_title = make_label(list, "NETWORKS", &lv_font_montserrat_14,
                                 C_MUTED);
  s_wifi_scan_btn = make_button(list, "SCAN", 92, 30, on_wifi_scan, nullptr);
  lv_obj_set_pos(s_wifi_scan_btn, 184, -2);
  s_wifi_list = lv_obj_create(list);
  lv_obj_remove_style_all(s_wifi_list);
  lv_obj_set_pos(s_wifi_list, 0, 32);
  lv_obj_set_size(s_wifi_list, 276, 244);
  lv_obj_set_style_bg_opa(s_wifi_list, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_row(s_wifi_list, 4, 0);
  lv_obj_set_flex_flow(s_wifi_list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(s_wifi_list, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(s_wifi_list, LV_SCROLLBAR_MODE_ON);
  lv_obj_set_style_bg_color(s_wifi_list, C_LINE, LV_PART_SCROLLBAR);
  lv_obj_set_style_bg_opa(s_wifi_list, LV_OPA_COVER, LV_PART_SCROLLBAR);
  lv_obj_set_style_width(s_wifi_list, 4, LV_PART_SCROLLBAR);
  lv_obj_set_style_radius(s_wifi_list, 2, LV_PART_SCROLLBAR);

  lv_obj_t *right = make_panel(s, 324, 132, 364, 184);
  s_wifi_connect_panel = right;
  make_label(right, "SELECTED", &lv_font_montserrat_12, C_MUTED);
  s_wifi_selected = make_label(right, "--", &lv_font_montserrat_14, C_TEXT);
  lv_obj_set_pos(s_wifi_selected, 80, 0);
  lv_obj_set_width(s_wifi_selected, 230);
  lv_label_set_long_mode(s_wifi_selected, LV_LABEL_LONG_CLIP);

  lv_obj_t *pass_lbl = make_label(right, "PASSWORD", &lv_font_montserrat_12,
                                  C_MUTED);
  lv_obj_set_pos(pass_lbl, 0, 30);
  s_wifi_pass = lv_textarea_create(right);
  lv_obj_set_pos(s_wifi_pass, 0, 48);
  lv_obj_set_size(s_wifi_pass, 272, 40);
  lv_textarea_set_one_line(s_wifi_pass, true);
  lv_textarea_set_password_mode(s_wifi_pass, true);
  lv_textarea_set_placeholder_text(s_wifi_pass, "WiFi password");
  lv_obj_add_event_cb(s_wifi_pass, on_wifi_pass_focus, LV_EVENT_FOCUSED, nullptr);
  lv_obj_add_event_cb(s_wifi_pass, on_wifi_pass_focus, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *kbd = make_button(right, "KEY", 60, 40, on_wifi_pass_focus,
                              nullptr);
  lv_obj_set_pos(kbd, 280, 48);
  lv_obj_set_style_text_font(kbd, &lv_font_montserrat_12, 0);
  s_wifi_connect = make_button(right, "CONNECT", 170, 40, on_wifi_join, nullptr);
  lv_obj_set_pos(s_wifi_connect, 0, 102);
  s_wifi_connect_hint = make_label(
      right, "No selection uses the stored pair.", &lv_font_montserrat_12,
      C_MUTED);
  lv_obj_set_pos(s_wifi_connect_hint, 0, 150);

  lv_obj_t *service = make_panel(s, 324, 322, 364, 122);
  s_wifi_service_panel = service;
  make_label(service, "SERVICE / UPLOAD", &lv_font_montserrat_12, C_MUTED);
  s_wifi_service = make_button(service, "SRV", 82, 32, on_service_toggle,
                               nullptr);
  lv_obj_set_pos(s_wifi_service, 0, 18);
  lv_obj_set_style_text_font(s_wifi_service, &lv_font_montserrat_12, 0);
  s_wifi_forget = make_button(service, "FORGET", 82, 32, on_wifi_forget_btn,
                              nullptr);
  lv_obj_set_pos(s_wifi_forget, 90, 18);
  lv_obj_set_style_text_font(s_wifi_forget, &lv_font_montserrat_12, 0);
  s_wifi_admin_btn = make_button(service, "ADMIN", 82, 32, on_wifi_admin_focus,
                                 nullptr);
  lv_obj_set_pos(s_wifi_admin_btn, 180, 18);
  lv_obj_set_style_text_font(s_wifi_admin_btn, &lv_font_montserrat_12, 0);
  s_wifi_ap_info = make_label(service, "", &lv_font_montserrat_12, C_MUTED);
  lv_obj_set_pos(s_wifi_ap_info, 0, 56);
  lv_obj_set_width(s_wifi_ap_info, 340);
  lv_label_set_long_mode(s_wifi_ap_info, LV_LABEL_LONG_CLIP);
  s_wifi_reason = make_label(service, "", &lv_font_montserrat_12, C_MUTED);
  lv_obj_set_pos(s_wifi_reason, 0, 76);
  lv_obj_set_width(s_wifi_reason, 340);
  lv_label_set_long_mode(s_wifi_reason, LV_LABEL_LONG_CLIP);

  /* AP mode uses the full content area: join QR left, configuration centre,
   * browser QR right.  The scan/STA panels and service panel are hidden. */
  s_wifi_ap_panel = lv_obj_create(s);
  lv_obj_remove_style_all(s_wifi_ap_panel);
  lv_obj_set_pos(s_wifi_ap_panel, 12, 132);
  lv_obj_set_size(s_wifi_ap_panel, 688, 312);
  lv_obj_set_style_bg_opa(s_wifi_ap_panel, LV_OPA_TRANSP, 0);
  lv_obj_clear_flag(s_wifi_ap_panel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(s_wifi_ap_panel, LV_OBJ_FLAG_HIDDEN);

  s_wifi_qr_card = lv_obj_create(s_wifi_ap_panel);
  lv_obj_remove_style_all(s_wifi_qr_card);
  lv_obj_set_pos(s_wifi_qr_card, 0, 0);
  lv_obj_set_size(s_wifi_qr_card, 184, 312);
  lv_obj_set_style_bg_color(s_wifi_qr_card, lv_color_white(), 0);
  lv_obj_set_style_bg_opa(s_wifi_qr_card, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(s_wifi_qr_card, 10, 0);
  lv_obj_clear_flag(s_wifi_qr_card, LV_OBJ_FLAG_SCROLLABLE);
  s_wifi_qr = lv_qrcode_create(s_wifi_qr_card);
  lv_qrcode_set_size(s_wifi_qr, 160);
  lv_qrcode_set_dark_color(s_wifi_qr, C_BG);
  lv_qrcode_set_light_color(s_wifi_qr, lv_color_white());
  lv_obj_set_pos(s_wifi_qr, 12, 8);
  lv_obj_t *join_lbl = make_label(s_wifi_qr_card, "JOIN WIFI",
                                  &lv_font_montserrat_14, C_BG);
  lv_obj_set_width(join_lbl, 184);
  lv_obj_set_style_text_align(join_lbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_pos(join_lbl, 0, 180);
  lv_obj_t *ap_name = make_label(s_wifi_qr_card, "", &lv_font_montserrat_12,
                                 C_BG);
  lv_obj_set_width(ap_name, 160);
  lv_obj_set_style_text_align(ap_name, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_pos(ap_name, 12, 206);
  lv_label_set_long_mode(ap_name, LV_LABEL_LONG_WRAP);
  s_wifi_ap_name = ap_name;

  lv_obj_t *middle = make_panel(s_wifi_ap_panel, 196, 0, 296, 312);
  make_label(middle, "AP UPLOAD", &lv_font_montserrat_14, C_MUTED);
  lv_obj_t *pw = make_label(middle, "PASSWORD", &lv_font_montserrat_12,
                            C_MUTED);
  lv_obj_set_pos(pw, 0, 26);
  s_wifi_ap_key = make_label(middle, "--------", &lv_font_montserrat_20,
                             C_TEXT);
  lv_obj_set_pos(s_wifi_ap_key, 0, 40);
  s_wifi_new_key_btn = make_button(middle, "NEW KEY", 128, 36, on_wifi_new_key,
                                   nullptr);
  lv_obj_set_pos(s_wifi_new_key_btn, 0, 78);
  s_wifi_set_key_btn = make_button(middle, "SET KEY", 128, 36,
                                   on_wifi_set_ap_key, nullptr);
  lv_obj_set_pos(s_wifi_set_key_btn, 136, 78);
  lv_obj_t *url = make_label(middle, "http://192.168.4.1",
                             &lv_font_montserrat_14, C_ACCENT);
  lv_obj_set_pos(url, 0, 126);
  lv_obj_t *help = make_label(middle,
                              "Scan left to join; scan right to open the browser.",
                              &lv_font_montserrat_12, C_TEXT);
  lv_obj_set_pos(help, 0, 150);
  lv_obj_set_width(help, 272);
  lv_label_set_long_mode(help, LV_LABEL_LONG_WRAP);
  s_wifi_ap_service = make_button(middle, "SRV", 82, 36, on_service_toggle,
                                  nullptr);
  lv_obj_set_pos(s_wifi_ap_service, 0, 204);
  lv_obj_set_style_text_font(s_wifi_ap_service, &lv_font_montserrat_12, 0);
  s_wifi_ap_forget = make_button(middle, "FORGET", 82, 36, on_wifi_forget_btn,
                                 nullptr);
  lv_obj_set_pos(s_wifi_ap_forget, 90, 204);
  lv_obj_set_style_text_font(s_wifi_ap_forget, &lv_font_montserrat_12, 0);
  s_wifi_ap_admin = make_button(middle, "ADMIN", 82, 36, on_wifi_admin_focus,
                                nullptr);
  lv_obj_set_pos(s_wifi_ap_admin, 180, 204);
  lv_obj_set_style_text_font(s_wifi_ap_admin, &lv_font_montserrat_12, 0);
  s_wifi_ap_reason = make_label(middle, "", &lv_font_montserrat_12, C_MUTED);
  lv_obj_set_pos(s_wifi_ap_reason, 0, 252);
  lv_obj_set_width(s_wifi_ap_reason, 272);
  lv_label_set_long_mode(s_wifi_ap_reason, LV_LABEL_LONG_WRAP);

  s_wifi_url_qr_card = lv_obj_create(s_wifi_ap_panel);
  lv_obj_remove_style_all(s_wifi_url_qr_card);
  lv_obj_set_pos(s_wifi_url_qr_card, 504, 0);
  lv_obj_set_size(s_wifi_url_qr_card, 184, 312);
  lv_obj_set_style_bg_color(s_wifi_url_qr_card, lv_color_white(), 0);
  lv_obj_set_style_bg_opa(s_wifi_url_qr_card, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(s_wifi_url_qr_card, 10, 0);
  lv_obj_clear_flag(s_wifi_url_qr_card, LV_OBJ_FLAG_SCROLLABLE);
  s_wifi_url_qr = lv_qrcode_create(s_wifi_url_qr_card);
  lv_qrcode_set_size(s_wifi_url_qr, 160);
  lv_qrcode_set_dark_color(s_wifi_url_qr, C_BG);
  lv_qrcode_set_light_color(s_wifi_url_qr, lv_color_white());
  lv_obj_set_pos(s_wifi_url_qr, 12, 8);
  lv_obj_t *url_lbl = make_label(s_wifi_url_qr_card, "OPEN BROWSER",
                                 &lv_font_montserrat_14, C_BG);
  lv_obj_set_width(url_lbl, 184);
  lv_obj_set_style_text_align(url_lbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_pos(url_lbl, 0, 180);
  lv_obj_t *url_hint = make_label(s_wifi_url_qr_card, "192.168.4.1",
                                  &lv_font_montserrat_12, C_BG);
  lv_obj_set_width(url_hint, 184);
  lv_obj_set_style_text_align(url_hint, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_pos(url_hint, 0, 206);
  s_wifi_qr_hash = 0;
  s_wifi_url_qr_hash = 0;

  /* Full-screen entry modal.  The field and Cancel/Done buttons are above a
   * hand-built keypad so the layout is ours on the panel and in the host PNG. */
  s_wifi_modal = lv_obj_create(lv_screen_active());
  lv_obj_remove_style_all(s_wifi_modal);
  lv_obj_set_pos(s_wifi_modal, 0, 0);
  lv_obj_set_size(s_wifi_modal, kWidth, kHeight);
  lv_obj_set_style_bg_color(s_wifi_modal, C_BG, 0);
  lv_obj_set_style_bg_opa(s_wifi_modal, LV_OPA_COVER, 0);
  lv_obj_clear_flag(s_wifi_modal, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(s_wifi_modal, LV_OBJ_FLAG_HIDDEN);

  s_wifi_modal_title = make_label(s_wifi_modal, "WiFi password",
                                  &lv_font_montserrat_20, C_TEXT);
  lv_obj_set_pos(s_wifi_modal_title, 16, 10);

  lv_obj_t *done = make_button(s_wifi_modal, "DONE", 92, 40, on_wifi_modal_done,
                               nullptr);
  lv_obj_set_style_bg_color(done, C_ACCENT, 0);
  lv_obj_set_style_border_color(done, C_ACCENT, 0);
  lv_obj_set_pos(done, 576, 8);
  lv_obj_t *cancel = make_button(s_wifi_modal, "CANCEL", 100, 40,
                                 on_wifi_modal_cancel, nullptr);
  lv_obj_set_pos(cancel, 684, 8);

  s_wifi_modal_ta = lv_textarea_create(s_wifi_modal);
  lv_obj_set_pos(s_wifi_modal_ta, 12, 58);
  lv_obj_set_size(s_wifi_modal_ta, 776, 56);
  lv_obj_set_style_text_font(s_wifi_modal_ta, &lv_font_montserrat_20, 0);
  lv_textarea_set_one_line(s_wifi_modal_ta, true);
  lv_textarea_set_password_mode(s_wifi_modal_ta, true);
  lv_textarea_set_max_length(s_wifi_modal_ta, 63);
  lv_textarea_set_placeholder_text(s_wifi_modal_ta, "Enter password");

  s_wifi_modal_kb = lv_obj_create(s_wifi_modal);
  lv_obj_remove_style_all(s_wifi_modal_kb);
  lv_obj_set_pos(s_wifi_modal_kb, 12, 126);
  lv_obj_set_size(s_wifi_modal_kb, 776, 344);
  lv_obj_set_style_bg_opa(s_wifi_modal_kb, LV_OPA_TRANSP, 0);
  lv_obj_clear_flag(s_wifi_modal_kb, LV_OBJ_FLAG_SCROLLABLE);
  wifi_build_keypad();
  s_wifi_shown_hash = 0;
}

void build_settings(void) {
  lv_obj_t *s = s_screens[UI_SCREEN_SETTINGS];
  lv_obj_t *head = make_panel(s, 12, 8, 688, 48);
  make_back_button(head, UI_SCREEN_SERVICE);
  lv_obj_t *title = make_label(head, "SYSTEM", &lv_font_montserrat_20,
                               C_TEXT);
  lv_obj_set_pos(title, 44, 0);
  lv_obj_t *hint = make_label(head, "build, runtime and display",
                              &lv_font_montserrat_12, C_MUTED);
  lv_obj_align(hint, LV_ALIGN_RIGHT_MID, -12, 0);

  lv_obj_t *sys = make_panel(s, 12, 64, 332, 180);
  make_label(sys, "BUILD", &lv_font_montserrat_14, C_MUTED);
  s_set_firmware = make_kv(sys, "Firmware", 30);
  s_set_boot = make_kv(sys, "Boot", 52);
  s_set_uptime = make_kv(sys, "Uptime", 74);
  s_set_capacity = make_kv(sys, "Capacity", 96);
  s_set_watch = make_kv(sys, "Watchpoint", 118);
  s_set_can = make_kv(sys, "CAN", 140);

  lv_obj_t *state = make_panel(s, 356, 64, 344, 180);
  make_label(state, "RUNTIME", &lv_font_montserrat_14, C_MUTED);
  s_set_wifi = make_kv(state, "WiFi", 30);
  s_set_ip = make_kv(state, "IP", 52);
  s_set_boards = make_kv(state, "Boards", 74);
  s_set_positions = make_kv(state, "Positions", 96);
  s_set_node_image = make_kv(state, "Node image", 118);

  lv_obj_t *display = make_panel(s, 12, 256, 688, 188);
  make_label(display, "DISPLAY", &lv_font_montserrat_14, C_MUTED);
  lv_obj_t *display_text = make_label(display, "Backlight off after",
                                      &lv_font_montserrat_14, C_TEXT);
  lv_obj_set_pos(display_text, 0, 34);
  static const uint16_t kPresets[4] = {0, 30, 60, 300};
  static const char *kPresetText[4] = {"OFF", "30 s", "60 s", "300 s"};
  for (int i = 0; i < 4; ++i) {
    s_disp_btn[i] = make_button(display, kPresetText[i], 110, 40,
                                on_display_preset,
                                (void *)(intptr_t)kPresets[i]);
    lv_obj_set_pos(s_disp_btn[i], i * 120, 62);
  }
  lv_obj_t *display_hint = make_label(
      display,
      "The first touch only wakes the display; it is consumed and does not "
      "press the control underneath.",
      &lv_font_montserrat_12, C_MUTED);
  lv_obj_set_pos(display_hint, 0, 112);
  lv_obj_set_width(display_hint, 640);
  lv_label_set_long_mode(display_hint, LV_LABEL_LONG_WRAP);
  lv_obj_t *display_hint2 = make_label(
      display, "A running test or firmware queue keeps the screen on.",
      &lv_font_montserrat_12, C_MUTED);
  lv_obj_set_pos(display_hint2, 0, 148);
}

lv_obj_t *hub_card(lv_obj_t *parent, int x, int y, int w, int h,
                   const char *title, const char *desc, int target) {
  lv_obj_t *card = lv_obj_create(parent);
  lv_obj_remove_style_all(card);
  lv_obj_set_pos(card, x, y);
  lv_obj_set_size(card, w, h);
  lv_obj_set_style_bg_color(card, C_PANEL, 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(card, C_PANEL2, LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_set_style_radius(card, 8, 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_border_color(card, C_LINE, 0);
  lv_obj_set_style_pad_all(card, 12, 0);
  lv_obj_set_style_shadow_width(card, 0, 0);
  lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(card, on_tab, LV_EVENT_CLICKED,
                      (void *)(intptr_t)target);

  lv_obj_t *t = make_label(card, title, &lv_font_montserrat_16, C_TEXT);
  lv_obj_set_pos(t, 0, 0);
  lv_obj_t *d = make_label(card, desc, &lv_font_montserrat_12, C_MUTED);
  lv_obj_set_pos(d, 0, 30);
  lv_label_set_long_mode(d, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(d, w - 54);
  lv_obj_t *arrow = make_label(card, LV_SYMBOL_RIGHT, &lv_font_montserrat_16,
                               C_ACCENT);
  lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -2, 0);
  return card;
}

void build_setup_hub(void) {
  lv_obj_t *s = s_screens[UI_SCREEN_SETUP];
  lv_obj_t *head = make_panel(s, 12, 8, 688, 48);
  make_label(head, "BUILD", &lv_font_montserrat_20, C_TEXT);
  lv_obj_t *hint = make_label(head, "grid, boards, motors and test",
                              &lv_font_montserrat_14, C_MUTED);
  lv_obj_align(hint, LV_ALIGN_RIGHT_MID, -12, 0);

  struct setup_item {
    const char *title;
    const char *desc;
    int target;
  };
  static const setup_item items[4] = {
      {"GRID & LAYOUT", "Set columns and rows; inspect every position.",
       UI_SCREEN_GRID},
      {"BOARDS", "Discover boards, IDs and health.", UI_SCREEN_NODES},
      {"ALIGN MOTORS", "Jog an axis and assign it to a position.",
       UI_SCREEN_MOTOR},
      {"TEST & CONFIG", "Drop, speed, gap; save or clear the map.",
       UI_SCREEN_CONFIG},
  };
  const int first_y = 64;
  const int row_h = 64;
  const int step = 72;
  for (int i = 0; i < 4; ++i) {
    lv_obj_t *row = lv_obj_create(s);
    lv_obj_remove_style_all(row);
    lv_obj_set_pos(row, 12, first_y + i * step);
    lv_obj_set_size(row, 688, row_h);
    lv_obj_set_style_bg_color(row, C_PANEL, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(row, C_PANEL2, LV_STATE_PRESSED);
    lv_obj_set_style_radius(row, 8, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(row, C_LINE, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(row, on_tab, LV_EVENT_CLICKED,
                        (void *)(intptr_t)items[i].target);

    char num[4];
    std::snprintf(num, sizeof(num), "%02d", i + 1);
    lv_obj_t *n = make_label(row, num, &lv_font_montserrat_20, C_ACCENT);
    lv_obj_set_pos(n, 14, 18);
    lv_obj_t *title = make_label(row, items[i].title,
                                 &lv_font_montserrat_20, C_TEXT);
    lv_obj_set_pos(title, 58, 8);
    lv_obj_t *desc = make_label(row, items[i].desc,
                                &lv_font_montserrat_14, C_MUTED);
    lv_obj_set_pos(desc, 58, 36);
    lv_obj_set_width(desc, 540);
    lv_obj_t *arrow = make_label(row, LV_SYMBOL_RIGHT,
                                 &lv_font_montserrat_20, C_ACCENT);
    lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -16, 0);
  }

  lv_obj_t *foot = make_panel(s, 12, 352, 688, 92);
  lv_obj_t *text = make_label(
      foot,
      "Boards, then grid, then motors, then test.  Tap BUILD again to return.",
      &lv_font_montserrat_14, C_TEXT);
  lv_obj_set_pos(text, 12, 12);
  lv_obj_set_width(text, 640);
  lv_label_set_long_mode(text, LV_LABEL_LONG_WRAP);
}

void build_service_hub(void) {
  lv_obj_t *s = s_screens[UI_SCREEN_SERVICE];
  lv_obj_t *head = make_panel(s, 12, 8, 688, 48);
  lv_obj_t *title = make_label(head, "SERVICE", &lv_font_montserrat_20,
                               C_TEXT);
  lv_obj_set_pos(title, 0, 0);
  lv_obj_t *hint = make_label(head, "firmware, diagnostics and system",
                              &lv_font_montserrat_12, C_MUTED);
  lv_obj_align(hint, LV_ALIGN_RIGHT_MID, -12, 0);

  struct service_item {
    const char *title;
    const char *desc;
    int target;
  };
  static const service_item items[4] = {
      {"FIRMWARE", "Node images, GitHub release and fleet upgrade.",
       UI_SCREEN_UPGRADE},
      {"CAN SCOPE", "Live traffic by identifier.", UI_SCREEN_CAN},
      {"LOG", "Boot, CAN and commissioning events.", UI_SCREEN_LOG},
      {"SYSTEM", "Build, display timeout and panel update.",
       UI_SCREEN_SETTINGS},
  };
  for (int i = 0; i < 4; ++i) {
    lv_obj_t *row = lv_obj_create(s);
    lv_obj_remove_style_all(row);
    lv_obj_set_pos(row, 12, 64 + i * 72);
    lv_obj_set_size(row, 688, 64);
    lv_obj_set_style_bg_color(row, C_PANEL, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(row, C_PANEL2, LV_STATE_PRESSED);
    lv_obj_set_style_radius(row, 8, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(row, C_LINE, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(row, on_tab, LV_EVENT_CLICKED,
                        (void *)(intptr_t)items[i].target);

    char num[4];
    std::snprintf(num, sizeof(num), "%02d", i + 1);
    lv_obj_t *n = make_label(row, num, &lv_font_montserrat_20, C_ACCENT);
    lv_obj_set_pos(n, 14, 18);
    lv_obj_t *t = make_label(row, items[i].title, &lv_font_montserrat_20,
                             C_TEXT);
    lv_obj_set_pos(t, 58, 8);
    lv_obj_t *d = make_label(row, items[i].desc, &lv_font_montserrat_14,
                             C_MUTED);
    lv_obj_set_pos(d, 58, 36);
    lv_obj_set_width(d, 540);
    lv_obj_t *arrow = make_label(row, LV_SYMBOL_RIGHT,
                                 &lv_font_montserrat_20, C_ACCENT);
    lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -16, 0);
  }

  lv_obj_t *status = make_panel(s, 12, 352, 688, 92);
  s_diag_can = make_label(status, "", &lv_font_montserrat_14, C_TEXT);
  lv_obj_set_pos(s_diag_can, 0, 6);
  s_diag_radio = make_label(status, "", &lv_font_montserrat_14, C_TEXT);
  lv_obj_set_pos(s_diag_radio, 0, 30);
  s_diag_fly = make_label(status, "", &lv_font_montserrat_14, C_MUTED);
  lv_obj_set_pos(s_diag_fly, 0, 54);
}

}  // namespace

void ui_init(const ui_actions_t *actions) {
  if (actions != nullptr) s_actions = *actions;
  if (s_snap == nullptr) {
#ifdef BOARD_HAS_PSRAM
    s_snap = (ui_snapshot_t *)heap_caps_malloc(sizeof(ui_snapshot_t),
                                               MALLOC_CAP_SPIRAM);
#endif
    if (s_snap == nullptr)
      s_snap = (ui_snapshot_t *)std::malloc(sizeof(ui_snapshot_t));
  }
  std::memset(s_snap, 0, sizeof(*s_snap));

  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, C_BG, 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  build_status_strip();
  for (int i = 0; i < UI_SCREEN_COUNT; ++i) s_screens[i] = make_screen();
  build_show();
  build_animations();
  build_grid();
  build_nodes();
  build_motor();
  build_config();
  build_log();
  build_can();
  build_upgrade();
  build_settings();
  build_wifi();
  build_setup_hub();
  build_service_hub();

  s_screen = UI_SCREEN_SHOW;
  ui_show_screen(UI_SCREEN_SHOW);
}

void ui_show_screen(int index) {
  if (index < 0) index = 0;
  if (index >= UI_SCREEN_COUNT) index = UI_SCREEN_COUNT - 1;
  s_screen = index;
  if (index == UI_SCREEN_LOG) s_log_last_ms = 0;
  if (index == UI_SCREEN_CAN) {
    s_can_last_ms = 0;
    s_can_first = 0;
  }
  for (int i = 0; i < UI_SCREEN_COUNT; ++i) {
    set_hidden(s_screens[i], i != index);
  }
  if (s_wifi_modal != nullptr)
    lv_obj_add_flag(s_wifi_modal, LV_OBJ_FLAG_HIDDEN);
  if (s_nav != nullptr) lv_obj_delete(s_nav);
  s_nav = nullptr;
  build_nav(index);
}

int ui_current_screen(void) { return s_screen; }

void ui_debug_open_wifi_modal(int target) { wifi_open_modal(target); }

void ui_debug_set_playing(int playing) {
  s_show_playing = playing != 0;
  if (s_snap != nullptr) ui_update(s_snap);
}

void ui_update(const ui_snapshot_t *snapshot) {
  if (snapshot == nullptr || s_snap == nullptr) return;
  *s_snap = *snapshot;

  if (s_selected_axis < 0) s_selected_axis = 0;
  if (s_selected_axis > 3) s_selected_axis = 3;
  const int cap = (s_snap->capacity > 0) ? (int)s_snap->capacity : UI_MAX_POSITIONS;
  if (s_assign_position < 0) s_assign_position = 0;
  if (s_assign_position >= cap) s_assign_position = cap - 1;

  /* Status strip link state. */
  lv_label_set_text_fmt(s_link_label, "%s  %s  boot %lu  uptime %lus",
                        s_snap->can_running ? "CAN" : "CAN OFF",
                        s_snap->can_mock ? "MOCK" : "HW",
                        (unsigned long)s_snap->boot_count,
                        (unsigned long)s_snap->uptime_s);

  /* SHOW — now playing, transport, playlist and library.  A file that fails
   * validation is kept out of both lists and counted as skipped. */
  uint32_t source_hash = 2166136261u;
  for (int i = 0; i < s_snap->animation_count; ++i) {
    const ui_animation_t *a = &s_snap->animations[i];
    source_hash = (source_hash ^ (uint32_t)a->size_bytes) * 16777619u;
    source_hash = (source_hash ^ a->duration_ms) * 16777619u;
    source_hash = (source_hash ^ (a->valid ? 1u : 0u)) * 16777619u;
    for (const char *p = a->name; *p != '\0'; ++p)
      source_hash = (source_hash ^ (uint8_t)*p) * 16777619u;
  }
  source_hash = (source_hash ^ (uint32_t)s_snap->animation_count) * 16777619u;
  if (source_hash != s_playlist_source_hash) {
    s_playlist_source_hash = source_hash;
    for (int i = 0; i < UI_MAX_ANIMATIONS; ++i)
      s_playlist_flags[i] = i < s_snap->animation_count
                                ? s_snap->animations[i].in_playlist
                                : false;
  }

  int valid_count = 0;
  int invalid_count = 0;
  int playlist_count = 0;
  int first_playlist = -1;
  for (int i = 0; i < s_snap->animation_count; ++i) {
    const ui_animation_t *a = &s_snap->animations[i];
    if (!a->valid) {
      ++invalid_count;
      continue;
    }
    ++valid_count;
    if (s_playlist_flags[i]) {
      ++playlist_count;
      if (first_playlist < 0) first_playlist = i;
    }
  }
  const bool selected_valid =
      s_snap->selected_animation >= 0 &&
      s_snap->selected_animation < s_snap->animation_count &&
      s_snap->animations[s_snap->selected_animation].valid &&
      s_playlist_flags[s_snap->selected_animation];

  const char *playing_name = s_snap->pattern_name;
  if ((playing_name == nullptr || playing_name[0] == '\0' ||
       std::strcmp(playing_name, "NO ANIMATION") == 0) &&
      first_playlist >= 0) {
    playing_name = s_snap->animations[first_playlist].name;
  }
  lv_label_set_text(s_show_pattern,
                    (playing_name != nullptr && playing_name[0] != '\0')
                        ? playing_name
                        : "NO ANIMATION");
  lv_label_set_text(s_show_state, s_snap->run_state);
  lv_color_t run_color = C_MUTED;
  if (std::strcmp(s_snap->run_state, "TEST RUN") == 0) run_color = C_OK;
  else if (std::strcmp(s_snap->run_state, "SAFE HOLD") == 0) run_color = C_ERR;
  lv_obj_set_style_text_color(s_show_state, run_color, 0);
  lv_label_set_text_fmt(s_show_elapsed, "%lu:%02lu",
                        (unsigned long)(s_snap->elapsed_ms / 60000),
                        (unsigned long)((s_snap->elapsed_ms / 1000) % 60));
  uint32_t show_ms = 0;
  if (selected_valid) {
    show_ms = s_snap->animations[s_snap->selected_animation].duration_ms;
  }
  int progress = 0;
  if (show_ms != 0 && s_snap->elapsed_ms <= show_ms) {
    progress = (int)((uint64_t)s_snap->elapsed_ms * 1000u / show_ms);
  }
  lv_bar_set_value(s_show_progress, progress, LV_ANIM_OFF);

  const bool loading = s_snap->animation_loading;
  lv_obj_t *play_lbl = lv_obj_get_child(s_show_play, 0);
  if (play_lbl != nullptr) lv_label_set_text(play_lbl, loading ? "LOADING" : "PLAY");
  lv_obj_clear_state(s_show_play, LV_STATE_DISABLED);
  if (!selected_valid || loading) lv_obj_add_state(s_show_play, LV_STATE_DISABLED);
  if (!selected_valid) lv_obj_add_state(s_show_pause, LV_STATE_DISABLED);
  else lv_obj_clear_state(s_show_pause, LV_STATE_DISABLED);
  if (!selected_valid) lv_obj_add_state(s_show_stop, LV_STATE_DISABLED);
  else lv_obj_clear_state(s_show_stop, LV_STATE_DISABLED);
  lv_obj_t *cycle_lbl = lv_obj_get_child(s_show_cycle, 0);
  if (cycle_lbl != nullptr)
    lv_label_set_text(cycle_lbl, s_show_cycle_on ? "CYCLE ON" : "CYCLE OFF");
  lv_obj_set_style_bg_color(s_show_cycle,
                            s_show_cycle_on ? C_ACCENT : C_PANEL2, 0);
  lv_label_set_text(s_show_transport,
                    s_snap->animation_status[0] ? s_snap->animation_status
                                                : (loading ? "loading" : "idle"));
  lv_obj_set_style_text_color(s_show_transport, loading ? C_WARN : C_MUTED, 0);

  /* The playlist stays; only the library yields to the safety target. */
  set_hidden(s_show_playlist_panel, false);
  set_hidden(s_show_library_panel, s_show_playing);
  set_hidden(s_show_big_stop, !s_show_playing);

  uint32_t show_hash = 2166136261u;
  show_hash = (show_hash ^ (uint32_t)s_snap->animation_count) * 16777619u;
  show_hash = (show_hash ^ (uint32_t)s_snap->selected_animation) * 16777619u;
  for (int i = 0; i < s_snap->animation_count; ++i) {
    const ui_animation_t *a = &s_snap->animations[i];
    show_hash = (show_hash ^ (uint32_t)a->size_bytes) * 16777619u;
    show_hash = (show_hash ^ a->duration_ms) * 16777619u;
    show_hash = (show_hash ^ (a->valid ? 1u : 0u)) * 16777619u;
    show_hash = (show_hash ^ (s_playlist_flags[i] ? 1u : 0u)) * 16777619u;
    for (const char *p = a->name; *p != '\0'; ++p)
      show_hash = (show_hash ^ (uint8_t)*p) * 16777619u;
  }
  if (show_hash != s_show_list_hash) {
    s_show_list_hash = show_hash;
    lv_obj_clean(s_show_list);
    lv_obj_clean(s_show_library);
    if (playlist_count == 0) {
      lv_obj_t *l = make_label(s_show_list,
                               "Playlist empty. Add from Library.",
                               &lv_font_montserrat_12, C_MUTED);
      lv_obj_set_style_pad_all(l, 8, 0);
    }
    if (valid_count - playlist_count == 0) {
      lv_obj_t *l = make_label(s_show_library, "All animations are in the playlist.",
                               &lv_font_montserrat_12, C_MUTED);
      lv_obj_set_style_pad_all(l, 8, 0);
    }
    for (int i = 0; i < s_snap->animation_count; ++i) {
      const ui_animation_t *a = &s_snap->animations[i];
      if (!a->valid || !s_playlist_flags[i]) continue;
      const bool selected = i == s_snap->selected_animation;
      lv_obj_t *row = lv_button_create(s_show_list);
      lv_obj_set_size(row, 260, 30);
      lv_obj_set_style_radius(row, 6, 0);
      lv_obj_set_style_bg_color(row, selected ? C_PANEL2 : C_PANEL, 0);
      lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
      lv_obj_set_style_border_width(row, 1, 0);
      lv_obj_set_style_border_color(row, selected ? C_ACCENT : C_LINE, 0);
      lv_obj_set_style_shadow_width(row, 0, 0);
      lv_obj_set_style_pad_left(row, 6, 0);
      lv_obj_set_style_pad_right(row, 4, 0);
      lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
      lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                            LV_FLEX_ALIGN_CENTER);
      lv_obj_set_style_pad_column(row, 5, 0);
      lv_obj_add_event_cb(row, on_anim_row, LV_EVENT_CLICKED,
                          (void *)(intptr_t)i);
      lv_obj_t *mark = make_label(row, selected ? LV_SYMBOL_PLAY : "",
                                  &lv_font_montserrat_12,
                                  selected ? C_ACCENT : C_MUTED);
      lv_obj_set_width(mark, 12);
      lv_obj_t *name = make_label(row, a->name, &lv_font_montserrat_12, C_TEXT);
      lv_obj_set_width(name, 138);
      lv_obj_t *duration = make_label(row, "", &lv_font_montserrat_12, C_MUTED);
      lv_obj_set_width(duration, 46);
      lv_obj_set_style_text_align(duration, LV_TEXT_ALIGN_RIGHT, 0);
      lv_label_set_text_fmt(duration, "%lu s",
                            (unsigned long)(a->duration_ms / 1000u));
      lv_obj_t *remove = make_button(row, LV_SYMBOL_MINUS, 24, 22,
                                     on_anim_remove, (void *)(intptr_t)i);
      lv_obj_set_style_text_font(remove, &lv_font_montserrat_14, 0);
    }
    for (int i = 0; i < s_snap->animation_count; ++i) {
      const ui_animation_t *a = &s_snap->animations[i];
      if (!a->valid || s_playlist_flags[i]) continue;
      lv_obj_t *row = lv_button_create(s_show_library);
      lv_obj_set_size(row, 272, 30);
      lv_obj_set_style_radius(row, 6, 0);
      lv_obj_set_style_bg_color(row, C_PANEL, 0);
      lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
      lv_obj_set_style_border_width(row, 1, 0);
      lv_obj_set_style_border_color(row, C_LINE, 0);
      lv_obj_set_style_shadow_width(row, 0, 0);
      lv_obj_set_style_pad_left(row, 8, 0);
      lv_obj_set_style_pad_right(row, 4, 0);
      lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
      lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                            LV_FLEX_ALIGN_CENTER);
      lv_obj_set_style_pad_column(row, 5, 0);
      lv_obj_add_event_cb(row, on_anim_row, LV_EVENT_CLICKED,
                          (void *)(intptr_t)i);
      lv_obj_t *name = make_label(row, a->name, &lv_font_montserrat_12, C_TEXT);
      lv_obj_set_width(name, 154);
      lv_obj_t *duration = make_label(row, "", &lv_font_montserrat_12, C_MUTED);
      lv_obj_set_width(duration, 46);
      lv_obj_set_style_text_align(duration, LV_TEXT_ALIGN_RIGHT, 0);
      lv_label_set_text_fmt(duration, "%lu s",
                            (unsigned long)(a->duration_ms / 1000u));
      lv_obj_t *add = make_button(row, LV_SYMBOL_PLUS, 24, 22, on_anim_add,
                                  (void *)(intptr_t)i);
      lv_obj_set_style_text_font(add, &lv_font_montserrat_14, 0);
    }
  }
  lv_label_set_text_fmt(s_show_anim_summary, "%d in playlist", playlist_count);
  lv_label_set_text_fmt(s_show_library_summary, "%d available  %d skipped",
                        valid_count - playlist_count, invalid_count);

  int max_temp = -128;
  bool any_health = false;
  for (int i = 0; i < s_snap->node_count; ++i) {
    const ui_node_t *n = &s_snap->nodes[i];
    if (!n->health_seen || n->health_age_ms > 3000u) continue;
    any_health = true;
    if (n->mcu_temp_c > max_temp) max_temp = n->mcu_temp_c;
  }
  char thermal[40];
  if (!any_health) {
    std::snprintf(thermal, sizeof(thermal), "thermal --");
  } else {
    std::snprintf(thermal, sizeof(thermal), "max %dC all boards", max_temp);
  }
  lv_label_set_text_fmt(s_show_status,
                        "CAN %s  |  %lu/%lu boards  |  %lu/%lu positions  |  %s",
                        s_snap->can_running
                            ? (s_snap->can_mock ? "MOCK" : "OK")
                            : "OFF",
                        (unsigned long)s_snap->online_boards,
                        (unsigned long)s_snap->assigned_boards,
                        (unsigned long)s_snap->assigned_positions,
                        (unsigned long)s_snap->capacity, thermal);
  lv_obj_set_style_text_color(s_show_status,
                              s_snap->bus_ok ? C_MUTED : C_ERR, 0);
  if (s_snap->safe_hold) {
    lv_label_set_text(s_show_hold, s_snap->hold_reason);
    lv_obj_clear_flag(s_show_hold, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_show_status, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(s_show_hold, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_show_status, LV_OBJ_FLAG_HIDDEN);
  }

  /* Animations */
  uint32_t anim_hash = 2166136261u;
  for (int i = 0; i < s_snap->animation_count; ++i) {
    const ui_animation_t *a = &s_snap->animations[i];
    anim_hash = (anim_hash ^ (uint32_t)a->size_bytes) * 16777619u;
    anim_hash = (anim_hash ^ a->duration_ms) * 16777619u;
    anim_hash = (anim_hash ^ (a->valid ? 1u : 0u)) * 16777619u;
    for (const char *p = a->name; *p != '\0'; ++p) {
      anim_hash = (anim_hash ^ (uint8_t)*p) * 16777619u;
    }
  }
  anim_hash = (anim_hash ^ (uint32_t)s_snap->selected_animation) * 16777619u;
  if (anim_hash != s_anim_shown_hash) {
    s_anim_shown_hash = anim_hash;
    lv_obj_clean(s_anim_list);
    if (s_snap->animation_count == 0) {
      lv_obj_t *l = make_label(s_anim_list,
                               "No animations found. Insert an SD card with "
                               ".hbp files.",
                               &lv_font_montserrat_16, C_MUTED);
      lv_obj_set_style_pad_all(l, 12, 0);
    }
    for (int i = 0; i < s_snap->animation_count; ++i) {
      const ui_animation_t *a = &s_snap->animations[i];
      lv_obj_t *row = lv_button_create(s_anim_list);
      lv_obj_set_size(row, 664, 48);
      lv_obj_set_style_radius(row, 6, 0);
      lv_obj_set_style_bg_color(
          row, (i == s_snap->selected_animation) ? C_PANEL2 : C_PANEL, 0);
      lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
      lv_obj_set_style_border_width(row, 1, 0);
      lv_obj_set_style_border_color(
          row, (i == s_snap->selected_animation) ? C_ACCENT : C_LINE, 0);
      lv_obj_set_style_shadow_width(row, 0, 0);
      lv_obj_set_style_pad_left(row, 10, 0);
      lv_obj_set_style_pad_right(row, 10, 0);
      lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
      lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                            LV_FLEX_ALIGN_CENTER);
      lv_obj_set_style_pad_column(row, 14, 0);
      lv_obj_add_event_cb(row, on_anim_row, LV_EVENT_CLICKED,
                          (void *)(intptr_t)i);

      lv_obj_t *name = make_label(row, a->name, &lv_font_montserrat_14, C_TEXT);
      lv_obj_set_width(name, 300);
      lv_obj_t *size = make_label(row, "", &lv_font_montserrat_12, C_MUTED);
      lv_obj_set_width(size, 100);
      lv_obj_set_style_text_align(size, LV_TEXT_ALIGN_RIGHT, 0);
      lv_label_set_text_fmt(size, "%lu KB",
                            (unsigned long)(a->size_bytes / 1024u));
      lv_obj_t *duration = make_label(row, "", &lv_font_montserrat_12, C_MUTED);
      lv_obj_set_width(duration, 90);
      lv_obj_set_style_text_align(duration, LV_TEXT_ALIGN_RIGHT, 0);
      lv_label_set_text_fmt(duration, "%lu s",
                            (unsigned long)(a->duration_ms / 1000u));
      lv_obj_t *state = make_label(row, a->valid ? "ok" : "invalid",
                                   &lv_font_montserrat_12,
                                   a->valid ? C_OK : C_ERR);
      lv_obj_set_width(state, 80);
      lv_obj_set_style_text_align(state, LV_TEXT_ALIGN_RIGHT, 0);
    }
  }
  lv_label_set_text_fmt(s_anim_summary, "%d files  %s",
                        s_snap->animation_count, s_snap->animation_source);
  lv_label_set_text(s_anim_status,
                    s_snap->animation_status[0] ? s_snap->animation_status
                                               : "idle");
  lv_obj_set_style_text_color(s_anim_status,
                              s_snap->animation_loading ? C_WARN : C_MUTED, 0);

  /* Grid */
  rebuild_grid(s_snap->grid_cols, s_snap->grid_rows);
  lv_label_set_text_fmt(s_grid_summary, "%d x %d, %lu assigned, %lu free",
                        s_snap->grid_cols, s_snap->grid_rows,
                        (unsigned long)s_snap->assigned_positions,
                        (unsigned long)s_snap->unassigned_positions);
  const int grid_cap = s_snap->grid_cols * s_snap->grid_rows;
  for (int i = 0; i < grid_cap && i < UI_MAX_POSITIONS; ++i) {
    lv_obj_t *cell = s_grid_cells[i];
    if (cell == nullptr) continue;
    const ui_cell_t *c = &s_snap->cells[i];
    lv_color_t bg = c->assigned ? C_OK : C_OFF;
    const ui_node_t *n = nullptr;
    if (c->node_index >= 0 && c->node_index < s_snap->node_count) {
      n = &s_snap->nodes[c->node_index];
    }
    const bool has_fault =
        n != nullptr && n->status_seen && n->fault_flags != 0;
    if (has_fault) bg = C_ERR;
    if (n != nullptr && n->health_seen && n->health_age_ms <= 3000u &&
        (n->health_valid & UI_HEALTH_VALID_DRV_STATUS) != 0) {
      if (n->ot_mask != 0)
        bg = C_ERR;
      else if (n->otpw_mask != 0 && !has_fault)
        bg = C_WARN;
    }
    lv_obj_set_style_bg_color(cell, bg, 0);
    lv_obj_set_style_border_width(cell, (i == s_grid_selected) ? 2 : 1, 0);
    lv_obj_set_style_border_color(
        cell, (i == s_grid_selected) ? C_ACCENT : C_LINE, 0);
    lv_obj_t *l = lv_obj_get_child(cell, 0);
    if (l != nullptr) {
      lv_obj_set_style_text_color(l, c->assigned ? C_BG : C_MUTED, 0);
    }
  }

  /* Nodes list: rebuild only when the visible data or selection changes. */
  uint32_t hash = 2166136261u;
  for (int i = 0; i < s_snap->node_count; ++i) {
    const ui_node_t *n = &s_snap->nodes[i];
    hash = (hash ^ n->board_id) * 16777619u;
    hash = (hash ^ n->state) * 16777619u;
    hash = (hash ^ n->fault_flags) * 16777619u;
    hash = (hash ^ (n->online ? 1u : 0u)) * 16777619u;
    hash = (hash ^ (uint32_t)n->assigned_positions) * 16777619u;
    hash = (hash ^ (n->health_seen ? 1u : 0u)) * 16777619u;
    hash = (hash ^ (uint32_t)(n->health_age_ms / 1000u)) * 16777619u;
    hash = (hash ^ (uint32_t)(uint8_t)n->mcu_temp_c) * 16777619u;
    hash = (hash ^ n->otpw_mask) * 16777619u;
    hash = (hash ^ n->ot_mask) * 16777619u;
    hash = (hash ^ n->diag_mask) * 16777619u;
    hash = (hash ^ n->health_valid) * 16777619u;
  }
  hash = (hash ^ (uint32_t)s_snap->selected_node) * 16777619u;
  if (hash != s_nodes_shown_hash) {
    s_nodes_shown_hash = hash;
    lv_obj_clean(s_nodes_list);
    if (s_snap->node_count == 0) {
      lv_obj_t *l = make_label(s_nodes_list, "No boards discovered yet.",
                               &lv_font_montserrat_16, C_MUTED);
      lv_obj_set_style_pad_all(l, 12, 0);
    }
    for (int i = 0; i < s_snap->node_count; ++i) {
      const ui_node_t *n = &s_snap->nodes[i];
      lv_obj_t *row = lv_button_create(s_nodes_list);
      lv_obj_set_size(row, 664, 48);
      lv_obj_set_style_radius(row, 6, 0);
      lv_obj_set_style_bg_color(
          row, (i == s_snap->selected_node) ? C_PANEL2 : C_PANEL, 0);
      lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
      lv_obj_set_style_border_width(row, 1, 0);
      lv_obj_set_style_border_color(
          row, (i == s_snap->selected_node) ? C_ACCENT : C_LINE, 0);
      lv_obj_set_style_shadow_width(row, 0, 0);
      lv_obj_set_style_pad_left(row, 10, 0);
      lv_obj_set_style_pad_right(row, 10, 0);
      lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
      lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                            LV_FLEX_ALIGN_CENTER);
      lv_obj_set_style_pad_column(row, 12, 0);
      lv_obj_add_event_cb(row, on_node_row, LV_EVENT_CLICKED,
                          (void *)(intptr_t)i);
      char flash[24];
      std::snprintf(flash, sizeof(flash), "%02x%02x%02x%02x%02x%02x",
                    n->uuid[0], n->uuid[1], n->uuid[2],
                    n->uuid[3], n->uuid[4], n->uuid[5]);
      char board[24];
      if (n->board_id >= 0) {
        std::snprintf(board, sizeof(board), "board %d", n->board_id);
      } else {
        std::snprintf(board, sizeof(board), "unassigned");
      }
      char state[40];
      std::snprintf(state, sizeof(state), "%s%s", n->online ? "online" : "offline",
                    n->status_seen ? "" : " (no status)");
      char pos[24];
      std::snprintf(pos, sizeof(pos), "%d pos", n->assigned_positions);
      char health[40];
      lv_color_t health_color = C_MUTED;
      if (!n->health_seen) {
        std::snprintf(health, sizeof(health), "health --");
      } else if (n->health_age_ms > 3000u) {
        std::snprintf(health, sizeof(health), "health stale");
        health_color = C_WARN;
      } else {
        const bool drv =
            (n->health_valid & UI_HEALTH_VALID_DRV_STATUS) != 0;
        const char *tmc =
            !drv ? "--" : (n->ot_mask ? "OT" : (n->otpw_mask ? "OTPW" : "OK"));
        std::snprintf(health, sizeof(health), "%dC TMC %s %lu.%lus",
                      (int)n->mcu_temp_c, tmc,
                      (unsigned long)(n->health_age_ms / 1000u),
                      (unsigned long)((n->health_age_ms % 1000u) / 100u));
        health_color = (drv && n->ot_mask)
                           ? C_ERR
                           : ((drv && n->otpw_mask) ? C_WARN : C_MUTED);
      }
      make_label(row, flash, &lv_font_montserrat_12, C_TEXT);
      make_label(row, board, &lv_font_montserrat_14,
                 n->board_id >= 0 ? C_OK : C_WARN);
      make_label(row, state, &lv_font_montserrat_12, C_MUTED);
      make_label(row, pos, &lv_font_montserrat_12, C_MUTED);
      make_label(row, health, &lv_font_montserrat_12, health_color);
    }
  }
  lv_label_set_text_fmt(s_nodes_summary, "%lu discovered, %lu online, %lu assigned",
                        (unsigned long)s_snap->discovered_boards,
                        (unsigned long)s_snap->online_boards,
                        (unsigned long)s_snap->assigned_boards);

  /* Motor */
  const ui_node_t *sel = nullptr;
  if (s_snap->selected_node >= 0 && s_snap->selected_node < s_snap->node_count) {
    sel = &s_snap->nodes[s_snap->selected_node];
  }
  if (sel != nullptr) {
    char flash[24];
    std::snprintf(flash, sizeof(flash), "%02x%02x%02x%02x%02x%02x",
                  sel->uuid[0], sel->uuid[1], sel->uuid[2],
                  sel->uuid[3], sel->uuid[4], sel->uuid[5]);
    lv_label_set_text_fmt(s_motor_node, "#%d  %s", s_snap->selected_node,
                          sel->online ? "online" : "offline");
    lv_label_set_text(s_motor_flash, flash);
    if (sel->board_id >= 0) {
      lv_label_set_text_fmt(s_motor_board, "board %d", sel->board_id);
    } else {
      lv_label_set_text(s_motor_board, "board unassigned");
    }
    char health[64];
    if (!sel->health_seen) {
      std::snprintf(health, sizeof(health), "no health frame");
    } else if (sel->health_age_ms > 3000u) {
      std::snprintf(health, sizeof(health), "health stale %lus",
                    (unsigned long)(sel->health_age_ms / 1000u));
    } else {
      const bool drv = (sel->health_valid & UI_HEALTH_VALID_DRV_STATUS) != 0;
      const char *tmc =
          !drv ? "--" : (sel->ot_mask ? "OT" : (sel->otpw_mask ? "OTPW" : "OK"));
      const bool ntc0 = (sel->health_valid & UI_HEALTH_VALID_NTC_T0) != 0;
      const bool ntc1 = (sel->health_valid & UI_HEALTH_VALID_NTC_BED) != 0;
      const unsigned long age_s =
          (unsigned long)(sel->health_age_ms / 1000u);
      const unsigned long age_t =
          (unsigned long)((sel->health_age_ms % 1000u) / 100u);
      if (ntc0 || ntc1) {
        std::snprintf(health, sizeof(health),
                      "MCU %dC TMC %s %lu.%lus\nDIAG 0x%x NTC %d/%d",
                      (int)sel->mcu_temp_c, tmc, age_s, age_t,
                      (unsigned)sel->diag_mask,
                      ntc0 ? (int)sel->ntc_t0_c : -128,
                      ntc1 ? (int)sel->ntc_bed_c : -128);
      } else {
        std::snprintf(health, sizeof(health),
                      "MCU %dC TMC %s %lu.%lus\nDIAG 0x%x",
                      (int)sel->mcu_temp_c, tmc, age_s, age_t,
                      (unsigned)sel->diag_mask);
      }
    }
    lv_label_set_text_fmt(s_motor_result,
                          "%d pos, buf %u, faults 0x%02x\n%s",
                          sel->assigned_positions, (unsigned)sel->buffer_depth,
                          (unsigned)sel->fault_flags, health);
  } else {
    lv_label_set_text(s_motor_node, "no board selected");
    lv_label_set_text(s_motor_flash, "");
    lv_label_set_text(s_motor_board, "");
    lv_label_set_text(s_motor_result, "Discover boards on the Nodes screen first.");
  }
  rebuild_motor_grid(s_snap->grid_cols, s_snap->grid_rows);
  bool axis_assigned[4] = {false, false, false, false};
  const int motor_cap = s_snap->grid_cols * s_snap->grid_rows;
  for (int i = 0; i < motor_cap && i < UI_MAX_POSITIONS; ++i) {
    const ui_cell_t *c = &s_snap->cells[i];
    if (c->assigned && c->node_index == s_snap->selected_node && c->axis < 4) {
      axis_assigned[c->axis] = true;
    }
  }
  for (int i = 0; i < 4; ++i) {
    lv_obj_t *btn = s_motor_axis_btn[i];
    lv_obj_set_style_bg_color(btn, axis_assigned[i] ? C_OK : C_PANEL2, 0);
    lv_obj_set_style_border_width(btn, (i == s_selected_axis) ? 2 : 1, 0);
    lv_obj_set_style_border_color(
        btn, (i == s_selected_axis) ? C_ACCENT : C_LINE, 0);
    lv_obj_t *l = lv_obj_get_child(btn, 0);
    if (l != nullptr) {
      lv_obj_set_style_text_color(l, axis_assigned[i] ? C_BG : C_TEXT, 0);
    }
  }
  for (int i = 0; i < motor_cap && i < UI_MAX_POSITIONS; ++i) {
    lv_obj_t *cell = s_motor_cells[i];
    if (cell == nullptr) continue;
    const ui_cell_t *c = &s_snap->cells[i];
    lv_obj_set_style_bg_color(cell, c->assigned ? C_OK : C_OFF, 0);
    lv_obj_set_style_border_width(cell, (i == s_assign_position) ? 2 : 1, 0);
    lv_obj_set_style_border_color(
        cell, (i == s_assign_position) ? C_ACCENT : C_LINE, 0);
    lv_obj_t *l = lv_obj_get_child(cell, 0);
    if (l != nullptr) {
      lv_obj_set_style_text_color(l, c->assigned ? C_BG : C_MUTED, 0);
    }
  }
  lv_label_set_text_fmt(s_motor_assign_pos, "%d", s_assign_position);

  /* Config */
  lv_label_set_text_fmt(s_cfg_cols, "%d", s_snap->grid_cols);
  lv_label_set_text_fmt(s_cfg_rows, "%d", s_snap->grid_rows);
  lv_label_set_text_fmt(s_cfg_drop, "%u", (unsigned)s_snap->test_drop_mm);
  lv_label_set_text_fmt(s_cfg_speed, "%u", (unsigned)s_snap->test_speed_mm_s);
  lv_label_set_text_fmt(s_cfg_delay, "%u", (unsigned)s_snap->test_step_delay_ms);
  lv_obj_t *serp_lbl = lv_obj_get_child(s_cfg_serp, 0);
  lv_label_set_text(serp_lbl, s_snap->test_serpentine ? "SERPENTINE ON"
                                                      : "SERPENTINE OFF");
  lv_obj_set_style_text_color(serp_lbl,
                              s_snap->test_serpentine ? C_BG : C_TEXT, 0);
  lv_obj_set_style_bg_color(s_cfg_serp,
                            s_snap->test_serpentine ? C_OK : C_PANEL2, 0);
  lv_label_set_text(s_cfg_status,
                    s_snap->config_dirty ? "UNSAVED CHANGES" : "saved");
  lv_obj_set_style_text_color(s_cfg_status,
                              s_snap->config_dirty ? C_WARN : C_OK, 0);

  /* CAN scope */
  if (!s_snap->can_running) {
    lv_label_set_text(s_can_bus, "STOPPED");
    lv_obj_set_style_text_color(s_can_bus, C_MUTED, 0);
  } else if (s_snap->can_bus_off) {
    lv_label_set_text(s_can_bus, "BUS OFF");
    lv_obj_set_style_text_color(s_can_bus, C_ERR, 0);
  } else {
    lv_label_set_text(s_can_bus, "BUS OK");
    lv_obj_set_style_text_color(s_can_bus, C_OK, 0);
  }
  lv_label_set_text_fmt(s_can_summary,
                        "rx %lu/s  %lu ids  tx %lu sent  %lu rej  %lu drop",
                        (unsigned long)s_snap->can_rate_1s,
                        (unsigned long)s_snap->can_unique_ids,
                        (unsigned long)s_snap->can_tx_frames,
                        (unsigned long)s_snap->can_tx_failed,
                        (unsigned long)s_snap->can_rx_dropped);
  uint32_t can_hash = 2166136261u;
  for (int i = 0; i < s_snap->can_entry_count; ++i) {
    const ui_can_entry_t *e = &s_snap->can_entries[i];
    can_hash = (can_hash ^ e->identifier) * 16777619u;
    can_hash = (can_hash ^ e->frame_count) * 16777619u;
    can_hash = (can_hash ^ e->interval_ms) * 16777619u;
    for (int b = 0; b < e->data_len; ++b) {
      can_hash = (can_hash ^ e->data[b]) * 16777619u;
    }
  }
  can_hash = (can_hash ^ s_snap->can_rate_1s) * 16777619u;
  can_hash = (can_hash ^ s_snap->can_dropped) * 16777619u;
  if (s_screen == UI_SCREEN_CAN && can_hash != s_can_shown_hash) {
    const uint32_t can_now = lv_tick_get();
    if (s_can_last_ms == 0 || (uint32_t)(can_now - s_can_last_ms) >= 1000u) {
      s_can_shown_hash = can_hash;
      s_can_last_ms = can_now;
      can_refresh_rows();
    }
  }

  /* Upgrade — one page of 30 tiles, 6 columns by 5 rows. */
  int versioned = 0;
  int done = 0;
  uint32_t up_hash = 2166136261u;
  up_hash = (up_hash ^ s_snap->upgrade_image_build) * 16777619u;
  up_hash = (up_hash ^ (uint32_t)s_snap->upgrade_done) * 16777619u;
  up_hash = (up_hash ^ (uint32_t)s_snap->upgrade_yellow_board) * 16777619u;
  for (const char *p = s_snap->upgrade_image_version; *p != '\0'; ++p)
    up_hash = (up_hash ^ (uint8_t)*p) * 16777619u;
  for (int i = 0; i < s_snap->node_count; ++i) {
    const ui_node_t *n = &s_snap->nodes[i];
    if (n->version_seen) versioned++;
    if (n->upgrade_state == UI_UPG_DONE) done++;
    up_hash = (up_hash ^ n->board_id) * 16777619u;
    up_hash = (up_hash ^ n->upgrade_state) * 16777619u;
    up_hash = (up_hash ^ n->upgrade_color) * 16777619u;
    up_hash = (up_hash ^ n->upgrade_progress) * 16777619u;
  }
  if (up_hash != s_upgrade_shown_hash) {
    s_upgrade_shown_hash = up_hash;
    lv_obj_clean(s_upgrade_tiles);
    const int cols = 6;
    const int tw = 107, th = 44, gap = 3;
    const int x0 = (688 - (cols * tw + (cols - 1) * gap)) / 2;
    for (int slot = 0; slot < 30; ++slot) {
      const ui_node_t *n =
          (slot < s_snap->node_count) ? &s_snap->nodes[slot] : nullptr;
      const int col = slot % cols;
      const int row = slot / cols;
      lv_obj_t *tile = lv_obj_create(s_upgrade_tiles);
      lv_obj_remove_style_all(tile);
      lv_obj_set_pos(tile, x0 + col * (tw + gap), row * (th + gap));
      lv_obj_set_size(tile, tw, th);
      lv_obj_set_style_radius(tile, 6, 0);
      lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
      lv_obj_set_style_border_width(tile, 1, 0);
      lv_obj_set_style_pad_all(tile, 0, 0);
      lv_obj_set_style_shadow_width(tile, 0, 0);
      lv_color_t bg = C_PANEL2;
      lv_color_t border = C_LINE;
      lv_color_t fg = C_TEXT;
      const char *phase = "";
      if (n != nullptr) {
        switch (n->upgrade_color) {
          case UI_UPG_COLOR_RED:
            /* Fleet core calls an outdated, queued board "red".  On the
             * panel red means fault, so queued is neutral until work starts. */
            bg = C_PANEL2; border = C_LINE; fg = C_TEXT; phase = "queued"; break;
          case UI_UPG_COLOR_YELLOW:
            bg = C_WARN; border = C_WARN; fg = C_BG;
            switch (n->upgrade_state) {
              case UI_UPG_ENTERING: phase = "enter"; break;
              case UI_UPG_CONNECTING: phase = "connect"; break;
              case UI_UPG_WRITING: phase = "write"; break;
              case UI_UPG_VERIFYING: phase = "verify"; break;
              case UI_UPG_REBOOTING: phase = "reboot"; break;
              default: phase = "wait"; break;
            }
            break;
          case UI_UPG_COLOR_GREEN:
            bg = C_OK; border = C_OK; fg = C_BG; phase = "done"; break;
          case UI_UPG_COLOR_FAILED:
            bg = C_ERR; border = C_ERR; fg = lv_color_white();
            phase = "failed"; break;
          case UI_UPG_COLOR_DIM:
          default:
            bg = C_OFF; border = C_LINE; fg = C_MUTED;
            phase = "skipped";
            if (n->upgrade_result[0] &&
                std::strcmp(n->upgrade_result, "version unknown") != 0)
              phase = n->upgrade_result;
            break;
        }
        lv_obj_add_event_cb(tile, on_upgrade_row, LV_EVENT_CLICKED,
                            (void *)(intptr_t)slot);
      } else {
        bg = C_OFF; fg = C_MUTED;
      }
      lv_obj_set_style_bg_color(tile, bg, 0);
      lv_obj_set_style_border_color(tile, border, 0);
      char id[8];
      if (n != nullptr && n->board_id >= 0)
        std::snprintf(id, sizeof(id), "%02d", n->board_id);
      else
        std::snprintf(id, sizeof(id), "--");
      lv_obj_t *idlabel = make_label(tile, id, &lv_font_montserrat_16, fg);
      lv_obj_set_pos(idlabel, 8, 4);
      char phase_txt[20];
      if (n != nullptr && n->upgrade_state == UI_UPG_WRITING &&
          n->upgrade_color == UI_UPG_COLOR_YELLOW) {
        std::snprintf(phase_txt, sizeof(phase_txt), "write %u%%",
                      (unsigned)n->upgrade_progress);
        phase = phase_txt;
      }
      lv_obj_t *pl = make_label(tile, phase, &lv_font_montserrat_12, fg);
      lv_obj_set_pos(pl, 8, 23);
      lv_obj_set_width(pl, 91);
      lv_label_set_long_mode(pl, LV_LABEL_LONG_CLIP);
    }
  }
  char head[128];
  if (s_snap->upgrade_image_build != 0) {
    std::snprintf(head, sizeof(head), "%s  %lu  %s  %d/30  %s",
                  s_snap->upgrade_image_version[0]
                      ? s_snap->upgrade_image_version
                      : "--",
                  (unsigned long)s_snap->upgrade_image_build,
                  s_snap->upgrade_source[0] ? s_snap->upgrade_source : "SD",
                  s_snap->upgrade_done, s_snap->upgrade_status);
  } else {
    std::snprintf(head, sizeof(head),
                  "no image selected   %d/30   %s",
                  s_snap->upgrade_done,
                  s_snap->upgrade_status[0] ? s_snap->upgrade_status : "idle");
  }
  lv_label_set_text(s_upgrade_summary, head);
  lv_label_set_text(s_upgrade_status,
                    s_snap->upgrade_status[0] ? s_snap->upgrade_status : "idle");
  lv_obj_set_style_text_color(s_upgrade_status,
                              s_snap->upgrade_active ? C_WARN : C_MUTED, 0);
  lv_bar_set_value(s_upgrade_progress, s_snap->upgrade_progress, LV_ANIM_OFF);
  const char *gh = s_snap->github_status[0] ? s_snap->github_status
                                            : "not queried";
  lv_label_set_text(s_upg_github_status, gh);
  lv_color_t gh_color = C_MUTED;
  if (std::strncmp(gh, "newer", 5) == 0) gh_color = C_WARN;
  else if (std::strncmp(gh, "same", 4) == 0) gh_color = C_OK;
  else if (std::strncmp(gh, "older", 5) == 0) gh_color = C_OK;
  else if (std::strncmp(gh, "error", 5) == 0) gh_color = C_ERR;
  lv_obj_set_style_text_color(s_upg_github_status, gh_color, 0);
  const bool github_newer = std::strncmp(gh, "newer", 5) == 0;
  if (s_snap->wifi_connected) {
    lv_obj_clear_state(s_upg_github_check, LV_STATE_DISABLED);
    if (github_newer) lv_obj_clear_state(s_upg_github_install, LV_STATE_DISABLED);
    else lv_obj_add_state(s_upg_github_install, LV_STATE_DISABLED);
  } else {
    lv_obj_add_state(s_upg_github_check, LV_STATE_DISABLED);
    lv_obj_add_state(s_upg_github_install, LV_STATE_DISABLED);
  }
  if (s_upgrade_stop != nullptr) {
    if (s_snap->upgrade_active) lv_obj_clear_state(s_upgrade_stop, LV_STATE_DISABLED);
    else lv_obj_add_state(s_upgrade_stop, LV_STATE_DISABLED);
  }

  /* Panel image source, next to the node image: one firmware screen owns
   * both.  The SD path needs no network; the GitHub path needs CONNECTED. */
  const bool panel_ready = s_snap->panel_image_ready;
  if (panel_ready) lv_obj_clear_state(s_upg_panel_update, LV_STATE_DISABLED);
  else lv_obj_add_state(s_upg_panel_update, LV_STATE_DISABLED);
  const char *panel_text = s_snap->panel_status;
  if (!panel_ready) {
    panel_text = "no image on SD";
  } else if (panel_text[0] == '\0' || std::strcmp(panel_text, "idle") == 0) {
    panel_text = "ready to load";
  }
  lv_label_set_text(s_upg_panel_status, panel_text);
  lv_color_t panel_color = panel_ready ? C_OK : C_MUTED;
  if (std::strncmp(panel_text, "error", 5) == 0 ||
      std::strstr(panel_text, "failed") != nullptr)
    panel_color = C_ERR;
  lv_obj_set_style_text_color(s_upg_panel_status, panel_color, 0);

  /* Network screen */
  uint32_t wifi_hash = 2166136261u;
  for (const char *p = s_snap->wifi_state; *p != '\0'; ++p)
    wifi_hash = (wifi_hash ^ (uint8_t)*p) * 16777619u;
  wifi_hash = (wifi_hash ^ (uint32_t)s_snap->wifi_scan_count) * 16777619u;
  wifi_hash = (wifi_hash ^ (uint32_t)s_wifi_selected_index) * 16777619u;
  for (int i = 0; i < s_snap->wifi_scan_count; ++i)
    for (const char *p = s_snap->wifi_scan_ssid[i]; *p != '\0'; ++p)
      wifi_hash = (wifi_hash ^ (uint8_t)*p) * 16777619u;
  if (s_wifi_selected_index >= s_snap->wifi_scan_count)
    s_wifi_selected_index = -1;
  if (wifi_hash != s_wifi_shown_hash) {
    s_wifi_shown_hash = wifi_hash;
    lv_obj_clean(s_wifi_list);
    for (int i = 0; i < s_snap->wifi_scan_count && i < UI_MAX_WIFI_SCAN; ++i) {
      lv_obj_t *b = make_button(s_wifi_list, s_snap->wifi_scan_ssid[i], 264,
                                44, on_wifi_row, (void *)(intptr_t)i);
      lv_obj_set_style_text_font(b, &lv_font_montserrat_14, 0);
    }
  }
  const bool wifi_connected = s_snap->wifi_connected;
  const bool wifi_ap = std::strcmp(s_snap->wifi_state, "AP_SETUP") == 0;
  const bool wifi_off = std::strcmp(s_snap->wifi_state, "OFF") == 0;
  const bool wifi_connecting =
      std::strcmp(s_snap->wifi_state, "CONNECTING") == 0;
  const bool scanning = s_snap->wifi_scanning;

  if (s_wifi_selected_index >= 0 &&
      s_wifi_selected_index < s_snap->wifi_scan_count) {
    lv_label_set_text(s_wifi_selected,
                      s_snap->wifi_scan_ssid[s_wifi_selected_index]);
  } else if (!wifi_ap && s_snap->wifi_has_credentials &&
             s_snap->wifi_ssid[0] != '\0') {
    lv_label_set_text(s_wifi_selected, s_snap->wifi_ssid);
  } else {
    lv_label_set_text(s_wifi_selected, "--");
  }
  if (!s_snap->wifi_has_credentials) s_wifi_sta_seeded = false;
  if (!wifi_ap && !s_wifi_sta_seeded && s_snap->wifi_has_credentials) {
    lv_textarea_set_text(s_wifi_pass, s_snap->wifi_password);
    s_wifi_sta_seeded = true;
  }

  /* AP mode uses the whole content area: join QR, configuration, browser QR. */
  if (wifi_ap) {
    set_hidden(s_wifi_ap_panel, false);
    set_hidden(s_wifi_list_panel, true);
    set_hidden(s_wifi_connect_panel, true);
    set_hidden(s_wifi_service_panel, true);
    char payload[192];
    const char *ssid = s_snap->wifi_ap_ssid[0] ? s_snap->wifi_ap_ssid : "";
    const char *pass = s_snap->wifi_ap_password[0] ? s_snap->wifi_ap_password
                                                   : "";
    std::snprintf(payload, sizeof(payload), "WIFI:T:WPA;S:%s;P:%s;H:false;;",
                  ssid, pass);
    uint32_t qr_hash = 2166136261u;
    for (const char *p = payload; *p != '\0'; ++p)
      qr_hash = (qr_hash ^ (uint8_t)*p) * 16777619u;
    if (qr_hash != s_wifi_qr_hash && s_wifi_qr != nullptr) {
      s_wifi_qr_hash = qr_hash;
      lv_qrcode_update(s_wifi_qr, payload, (uint32_t)std::strlen(payload));
    }
    const char *url = "http://192.168.4.1";
    uint32_t url_hash = 2166136261u;
    for (const char *p = url; *p != '\0'; ++p)
      url_hash = (url_hash ^ (uint8_t)*p) * 16777619u;
    if (url_hash != s_wifi_url_qr_hash && s_wifi_url_qr != nullptr) {
      s_wifi_url_qr_hash = url_hash;
      lv_qrcode_update(s_wifi_url_qr, url, (uint32_t)std::strlen(url));
    }
    lv_label_set_text(s_wifi_ap_key,
                      s_snap->wifi_ap_password[0]
                          ? s_snap->wifi_ap_password
                          : "--------");
    lv_label_set_text(s_wifi_ap_name, ssid[0] ? ssid : "SuspendedSpheres");
    lv_label_set_text(s_wifi_ap_reason,
                      s_snap->wifi_reason[0] ? s_snap->wifi_reason : "");
    lv_obj_set_style_bg_color(
        s_wifi_ap_service, s_snap->service_on ? C_OK : C_PANEL2, 0);
    lv_obj_clear_state(s_wifi_ap_forget, LV_STATE_DISABLED);
    lv_obj_clear_state(s_wifi_ap_admin, LV_STATE_DISABLED);
    if (!s_snap->wifi_has_credentials)
      lv_obj_add_state(s_wifi_ap_forget, LV_STATE_DISABLED);
  } else {
    set_hidden(s_wifi_ap_panel, true);
    set_hidden(s_wifi_list_panel, false);
    set_hidden(s_wifi_connect_panel, false);
    set_hidden(s_wifi_service_panel, false);
  }
  const char *pass_text = lv_textarea_get_text(s_wifi_pass);
  const bool has_new_pair =
      s_wifi_selected_index >= 0 && pass_text != nullptr && pass_text[0] != '\0';
  const bool can_connect = s_snap->wifi_has_credentials || has_new_pair;

  auto set_disabled = [](lv_obj_t *o, bool disabled) {
    if (o == nullptr) return;
    if (disabled) lv_obj_add_state(o, LV_STATE_DISABLED);
    else lv_obj_clear_state(o, LV_STATE_DISABLED);
  };
  auto set_active = [](lv_obj_t *o, bool active) {
    if (o == nullptr) return;
    lv_obj_set_style_bg_color(o, active ? C_ACCENT : C_PANEL2, 0);
  };

  set_active(s_wifi_mode_off, wifi_off);
  set_active(s_wifi_mode_connect, wifi_connecting || wifi_connected);
  set_active(s_wifi_mode_ap, wifi_ap);
  set_disabled(s_wifi_mode_connect, !can_connect);
  set_disabled(s_wifi_scan_btn, scanning || wifi_connecting);
  set_disabled(s_wifi_connect, !can_connect || scanning || wifi_connecting);
  set_disabled(s_wifi_service, !(wifi_connected || wifi_ap));
  set_disabled(s_wifi_forget, !s_snap->wifi_has_credentials);
  set_disabled(s_wifi_admin_btn, !wifi_ap);

  lv_obj_t *sl = lv_obj_get_child(s_wifi_scan_btn, 0);
  if (sl != nullptr) lv_label_set_text(sl, scanning ? "SCANNING" : "SCAN");
  lv_obj_set_style_bg_color(s_wifi_service,
                            s_snap->service_on ? C_OK : C_PANEL2, 0);

  lv_label_set_text_fmt(s_wifi_status, "%s  %s  service %s",
                        s_snap->wifi_state[0] ? s_snap->wifi_state : "OFF",
                        s_snap->wifi_ip[0] ? s_snap->wifi_ip : "--",
                        s_snap->service_on ? "on" : "off");
  lv_color_t wifi_color = C_MUTED;
  if (wifi_connected) wifi_color = C_OK;
  else if (wifi_ap) wifi_color = C_WARN;
  else if (wifi_connecting) wifi_color = C_WARN;
  lv_obj_set_style_text_color(s_wifi_status, wifi_color, 0);

  lv_label_set_text(
      s_wifi_ap_info,
      wifi_connected
          ? "CONNECT for downloads; AP for browser upload."
          : (wifi_off ? "WiFi off. Connect for downloads or start AP to upload."
                      : "AP mode uploads through the browser."));
  lv_obj_t *connect_lbl = lv_obj_get_child(s_wifi_mode_connect, 0);
  if (connect_lbl != nullptr)
    lv_label_set_text(connect_lbl, wifi_connecting ? "CONNECTING" : "CONNECT");
  if (s_wifi_connect_hint != nullptr) {
    if (wifi_connecting) {
      lv_label_set_text(s_wifi_connect_hint,
                        "Connecting... retrying automatically.");
      lv_obj_set_style_text_color(s_wifi_connect_hint, C_WARN, 0);
    } else if (wifi_connected) {
      lv_label_set_text(s_wifi_connect_hint, "Connected.");
      lv_obj_set_style_text_color(s_wifi_connect_hint, C_OK, 0);
    } else {
      lv_label_set_text(s_wifi_connect_hint,
                        "No selection uses the stored pair.");
      lv_obj_set_style_text_color(s_wifi_connect_hint, C_MUTED, 0);
    }
  }
  lv_label_set_text(s_wifi_reason,
                    s_snap->wifi_reason[0]
                        ? s_snap->wifi_reason
                        : (wifi_off ? "WiFi is off. Nothing starts until you choose a mode."
                                    : ""));

  /* System */
  const bool wifi_present = s_snap->wifi_present;
  const char *wifi_state =
      s_snap->wifi_state[0] ? s_snap->wifi_state : "OFF";
  lv_label_set_text(s_set_wifi, wifi_present ? wifi_state : "not compiled");
  lv_color_t state_color = C_MUTED;
  if (std::strcmp(wifi_state, "CONNECTED") == 0) state_color = C_OK;
  else if (std::strcmp(wifi_state, "FAILED") == 0) state_color = C_ERR;
  else if (wifi_present && std::strcmp(wifi_state, "OFF") != 0)
    state_color = C_WARN;
  lv_obj_set_style_text_color(s_set_wifi, state_color, 0);
  lv_label_set_text(s_set_ip, s_snap->wifi_ip[0] ? s_snap->wifi_ip : "--");
  lv_label_set_text_fmt(s_set_boards, "%lu/%lu online",
                        (unsigned long)s_snap->online_boards,
                        (unsigned long)s_snap->discovered_boards);
  lv_label_set_text_fmt(s_set_positions, "%lu/%lu assigned",
                        (unsigned long)s_snap->assigned_positions,
                        (unsigned long)s_snap->capacity);
  if (s_snap->upgrade_image_version[0]) {
    lv_label_set_text_fmt(s_set_node_image, "%s  %s",
                          s_snap->upgrade_image_version,
                          s_snap->upgrade_source[0] ? s_snap->upgrade_source
                                                    : "SD");
  } else {
    lv_label_set_text(s_set_node_image, "--");
  }
  lv_label_set_text_fmt(s_set_can, "%s %s",
                        s_snap->can_running ? "running" : "stopped",
                        s_snap->can_mock ? "MOCK" : "HW");
  lv_label_set_text(s_set_firmware, s_snap->firmware_version);
  lv_label_set_text_fmt(s_set_boot, "%lu", (unsigned long)s_snap->boot_count);
  lv_label_set_text_fmt(s_set_uptime, "%lu s", (unsigned long)s_snap->uptime_s);
  lv_label_set_text_fmt(s_set_capacity, "%lu positions",
                        (unsigned long)s_snap->capacity);
  lv_label_set_text_fmt(s_set_watch, "%u", (unsigned)s_snap->last_watchpoint);

  /* Display timeout is a four-way choice, not a hidden cycle: show which
   * value is active instead of only its text. */
  static const uint16_t kDisplayValues[4] = {0, 30, 60, 300};
  for (int i = 0; i < 4; ++i) {
    const bool active = (s_snap->display_timeout_s == kDisplayValues[i]);
    lv_obj_set_style_bg_color(s_disp_btn[i], active ? C_ACCENT : C_PANEL2, 0);
    lv_obj_set_style_border_color(s_disp_btn[i], active ? C_ACCENT : C_LINE,
                                  0);
  }

  /* SERVICE hub footer: the three facts that decide what to do next. */
  lv_label_set_text_fmt(s_diag_can, "CAN %s %s  |  %lu/%lu boards  |  %lu/%lu positions",
                        s_snap->can_running ? "running" : "stopped",
                        s_snap->can_mock ? "MOCK" : "HW",
                        (unsigned long)s_snap->online_boards,
                        (unsigned long)s_snap->discovered_boards,
                        (unsigned long)s_snap->assigned_positions,
                        (unsigned long)s_snap->capacity);
  lv_label_set_text_fmt(s_diag_radio, "WiFi %s  %s  |  panel %s",
                        wifi_state,
                        s_snap->wifi_ip[0] ? s_snap->wifi_ip : "--",
                        s_snap->firmware_version);
  lv_obj_set_style_text_color(
      s_diag_radio,
      s_snap->wifi_connected ? C_OK
                             : (std::strcmp(wifi_state, "OFF") == 0 ? C_MUTED
                                                                    : C_WARN),
      0);
  if (s_snap->upgrade_image_version[0]) {
    lv_label_set_text_fmt(s_diag_fly, "Fly image %s  build %lu  %s",
                          s_snap->upgrade_image_version,
                          (unsigned long)s_snap->upgrade_image_build,
                          s_snap->upgrade_source[0] ? s_snap->upgrade_source
                                                    : "SD");
  } else {
    lv_label_set_text(s_diag_fly, "Fly image --");
  }

  /* Log */
  uint32_t log_hash = 2166136261u;
  for (const char *p = s_snap->log_text; *p != '\0'; ++p) {
    log_hash = (log_hash ^ (uint8_t)*p) * 16777619u;
  }
  const uint32_t now = lv_tick_get();
  if (log_hash != s_log_hash &&
      (s_log_last_ms == 0 || (uint32_t)(now - s_log_last_ms) >= 500u)) {
    s_log_hash = log_hash;
    s_log_last_ms = now;
    lv_textarea_set_text(s_log_text, s_snap->log_text);
  }
  lv_label_set_text_fmt(s_log_status, "boot #%lu  last watchpoint %u  %s",
                        (unsigned long)s_snap->boot_count,
                        (unsigned)s_snap->last_watchpoint,
                        s_snap->firmware_version);
}
