#include "ui.h"

#include <cstdio>
#include <cstring>

#include <lvgl.h>

namespace {

constexpr int kWidth = 800;
constexpr int kHeight = 480;
constexpr int kRailW = 88;
constexpr int kRailX = kWidth - kRailW;
constexpr int kStatusH = 28;
constexpr int kScreenW = kRailX;
constexpr int kScreenH = kHeight - kStatusH;

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
ui_snapshot_t s_snap;
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
lv_obj_t *s_show_hold;
lv_obj_t *s_show_run_btn;

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
lv_obj_t *s_cfg_grid;
lv_obj_t *s_cfg_drop;
lv_obj_t *s_cfg_speed;
lv_obj_t *s_cfg_delay;
lv_obj_t *s_cfg_serp;
lv_obj_t *s_cfg_status;

/* Log screen */
lv_obj_t *s_log_text;
lv_obj_t *s_log_status;

/* CAN scope screen */
lv_obj_t *s_can_text;
lv_obj_t *s_can_summary;
lv_obj_t *s_can_bus;
uint32_t s_can_shown_hash = 0;
uint32_t s_can_last_ms = 0;
char s_can_buf[UI_MAX_CAN_IDS * 96];

/* Upgrade screen */
lv_obj_t *s_upgrade_list;
lv_obj_t *s_upgrade_summary;
lv_obj_t *s_upgrade_status;
lv_obj_t *s_upgrade_progress;
uint32_t s_upgrade_shown_hash = 0;

/* Settings screen */
lv_obj_t *s_set_wifi;
lv_obj_t *s_set_ssid;
lv_obj_t *s_set_ip;
lv_obj_t *s_set_rssi;
lv_obj_t *s_set_can;
lv_obj_t *s_set_firmware;
lv_obj_t *s_set_boot;
lv_obj_t *s_set_uptime;
lv_obj_t *s_set_capacity;
lv_obj_t *s_set_watch;
lv_obj_t *s_set_service;

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
  ui_update(&s_snap);
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
  ui_update(&s_snap);
}

void on_node_prev(lv_event_t *e) {
  (void)e;
  if (s_actions.node_select != nullptr) {
    s_actions.node_select(s_snap.selected_node - 1);
  }
}

void on_node_next(lv_event_t *e) {
  (void)e;
  if (s_actions.node_select != nullptr) {
    s_actions.node_select(s_snap.selected_node + 1);
  }
}

void on_grid_minus(lv_event_t *e) {
  (void)e;
  if (s_actions.config_grid != nullptr) {
    s_actions.config_grid(s_snap.grid_cols - 1, s_snap.grid_rows);
  }
}

void on_grid_plus(lv_event_t *e) {
  (void)e;
  if (s_actions.config_grid != nullptr) {
    s_actions.config_grid(s_snap.grid_cols + 1, s_snap.grid_rows);
  }
}

void on_rows_minus(lv_event_t *e) {
  (void)e;
  if (s_actions.config_grid != nullptr) {
    s_actions.config_grid(s_snap.grid_cols, s_snap.grid_rows - 1);
  }
}

void on_rows_plus(lv_event_t *e) {
  (void)e;
  if (s_actions.config_grid != nullptr) {
    s_actions.config_grid(s_snap.grid_cols, s_snap.grid_rows + 1);
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
    s_actions.config_test(s_snap.test_drop_mm - 10, s_snap.test_speed_mm_s,
                          s_snap.test_step_delay_ms, s_snap.test_serpentine);
  }
}

void on_drop_plus(lv_event_t *e) {
  (void)e;
  if (s_actions.config_test != nullptr) {
    s_actions.config_test(s_snap.test_drop_mm + 10, s_snap.test_speed_mm_s,
                          s_snap.test_step_delay_ms, s_snap.test_serpentine);
  }
}

void on_speed_minus(lv_event_t *e) {
  (void)e;
  if (s_actions.config_test != nullptr) {
    s_actions.config_test(s_snap.test_drop_mm, s_snap.test_speed_mm_s - 5,
                          s_snap.test_step_delay_ms, s_snap.test_serpentine);
  }
}

void on_speed_plus(lv_event_t *e) {
  (void)e;
  if (s_actions.config_test != nullptr) {
    s_actions.config_test(s_snap.test_drop_mm, s_snap.test_speed_mm_s + 5,
                          s_snap.test_step_delay_ms, s_snap.test_serpentine);
  }
}

void on_delay_minus(lv_event_t *e) {
  (void)e;
  if (s_actions.config_test != nullptr) {
    s_actions.config_test(s_snap.test_drop_mm, s_snap.test_speed_mm_s,
                          s_snap.test_step_delay_ms - 50, s_snap.test_serpentine);
  }
}

void on_delay_plus(lv_event_t *e) {
  (void)e;
  if (s_actions.config_test != nullptr) {
    s_actions.config_test(s_snap.test_drop_mm, s_snap.test_speed_mm_s,
                          s_snap.test_step_delay_ms + 50, s_snap.test_serpentine);
  }
}

void on_serp(lv_event_t *e) {
  (void)e;
  if (s_actions.config_test != nullptr) {
    s_actions.config_test(s_snap.test_drop_mm, s_snap.test_speed_mm_s,
                          s_snap.test_step_delay_ms, !s_snap.test_serpentine);
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

void on_can_toggle(lv_event_t *e) {
  (void)e;
  if (s_snap.can_running) {
    if (s_actions.can_stop != nullptr) s_actions.can_stop();
  } else {
    if (s_actions.can_start != nullptr) s_actions.can_start();
  }
}

void on_mock_inject(lv_event_t *e) {
  (void)e;
  if (s_actions.mock_inject != nullptr) s_actions.mock_inject();
}

void on_anim_row(lv_event_t *e) {
  const int index = (int)(intptr_t)lv_event_get_user_data(e);
  if (s_actions.animation_select != nullptr) s_actions.animation_select(index);
}

void on_anim_refresh(lv_event_t *e) {
  (void)e;
  if (s_actions.animation_refresh != nullptr) s_actions.animation_refresh();
}

void on_anim_load(lv_event_t *e) {
  (void)e;
  if (s_actions.animation_load != nullptr) s_actions.animation_load();
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

void build_nav(int active) {
  s_nav = lv_obj_create(lv_screen_active());
  lv_obj_remove_style_all(s_nav);
  lv_obj_set_pos(s_nav, kRailX, 0);
  lv_obj_set_size(s_nav, kRailW, kHeight);
  lv_obj_set_style_bg_color(s_nav, C_RAIL, 0);
  lv_obj_set_style_bg_opa(s_nav, LV_OPA_COVER, 0);
  lv_obj_clear_flag(s_nav, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *edge = nav_part(s_nav, 0, 0, 1, kHeight, C_LINE);

  static const char *names[UI_SCREEN_COUNT] = {
      "SHOW", "ANIM", "GRID",  "NODES", "MOTOR",
      "CONFIG", "LOG", "CAN", "UPGRADE", "SETTINGS"};
  const int tab_h = kHeight / UI_SCREEN_COUNT;
  const int icon_cy = 16;

  for (int i = 0; i < UI_SCREEN_COUNT; ++i) {
    const bool is_active = (i == active);
    const lv_color_t color = is_active ? C_TEXT : C_MUTED;

    lv_obj_t *tab = lv_obj_create(s_nav);
    lv_obj_remove_style_all(tab);
    lv_obj_set_pos(tab, 0, i * tab_h);
    lv_obj_set_size(tab, kRailW, tab_h);
    lv_obj_set_style_bg_color(tab, C_PANEL, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(tab, LV_OPA_40, LV_STATE_PRESSED);
    lv_obj_add_flag(tab, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(tab, on_tab, LV_EVENT_CLICKED, (void *)(intptr_t)i);

    switch (i) {
      case UI_SCREEN_SHOW:
        nav_icon_symbol(tab, LV_SYMBOL_PLAY, color);
        break;
      case UI_SCREEN_ANIMATIONS:
        nav_icon_symbol(tab, LV_SYMBOL_DIRECTORY, color);
        break;
      case UI_SCREEN_GRID:
        nav_icon_grid(tab, kRailW / 2, icon_cy, color);
        break;
      case UI_SCREEN_NODES:
        nav_icon_nodes(tab, kRailW / 2, icon_cy, color);
        break;
      case UI_SCREEN_MOTOR:
        nav_icon_motor(tab, kRailW / 2, icon_cy, color);
        break;
      case UI_SCREEN_CONFIG:
        nav_icon_sliders(tab, kRailW / 2, icon_cy, color);
        break;
      case UI_SCREEN_LOG:
        nav_icon_symbol(tab, LV_SYMBOL_LIST, color);
        break;
      case UI_SCREEN_UPGRADE:
        nav_icon_symbol(tab, LV_SYMBOL_DOWNLOAD, color);
        break;
      default:
        nav_icon_symbol(tab, LV_SYMBOL_SETTINGS, color);
        break;
    }

    lv_obj_t *label = make_label(tab, names[i], &lv_font_montserrat_12, color);
    lv_obj_set_width(label, kRailW);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(label, 0, 32);

    if (is_active) {
      lv_obj_t *rule = nav_part(tab, 0, 10, 3, tab_h - 20, C_ACCENT);
      lv_obj_set_style_radius(rule, 1, 0);
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
  lv_obj_set_width(s_link_label, kScreenW - 24);
  lv_obj_set_style_text_align(s_link_label, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_pos(s_link_label, 12, 7);
}

void build_show(void) {
  lv_obj_t *s = s_screens[UI_SCREEN_SHOW];

  lv_obj_t *title = make_panel(s, 12, 8, 688, 92);
  s_show_pattern = make_label(title, "NO PATTERN", &lv_font_montserrat_28,
                              C_TEXT);
  lv_obj_set_pos(s_show_pattern, 12, 8);
  s_show_state = make_label(title, "idle", &lv_font_montserrat_16, C_MUTED);
  lv_obj_set_pos(s_show_state, 12, 52);
  s_show_elapsed = make_label(title, "0:00", &lv_font_montserrat_20, C_MUTED);
  lv_obj_align(s_show_elapsed, LV_ALIGN_RIGHT_MID, -12, 0);

  lv_obj_t *bus = make_panel(s, 12, 108, 332, 118);
  make_label(bus, "CAN BUS", &lv_font_montserrat_14, C_MUTED);
  s_show_bus = make_label(bus, "--", &lv_font_montserrat_20, C_TEXT);
  lv_obj_set_pos(s_show_bus, 12, 28);
  s_show_hold = make_label(bus, "", &lv_font_montserrat_12, C_ERR);
  lv_obj_set_pos(s_show_hold, 12, 66);
  lv_label_set_long_mode(s_show_hold, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(s_show_hold, 300);

  lv_obj_t *comm = make_panel(s, 356, 108, 344, 118);
  make_label(comm, "COMMISSIONING", &lv_font_montserrat_14, C_MUTED);
  s_show_discovered = make_label(comm, "boards --", &lv_font_montserrat_16, C_TEXT);
  lv_obj_set_pos(s_show_discovered, 12, 28);
  s_show_assigned_boards = make_label(comm, "assigned --", &lv_font_montserrat_16, C_TEXT);
  lv_obj_set_pos(s_show_assigned_boards, 180, 28);
  s_show_online = make_label(comm, "online --", &lv_font_montserrat_16, C_TEXT);
  lv_obj_set_pos(s_show_online, 12, 56);
  s_show_positions = make_label(comm, "positions --", &lv_font_montserrat_16,
                                C_TEXT);
  lv_obj_set_pos(s_show_positions, 180, 56);

  lv_obj_t *stat = make_panel(s, 12, 234, 688, 68);
  s_show_unassigned = make_label(stat, "", &lv_font_montserrat_16, C_WARN);
  lv_obj_set_pos(s_show_unassigned, 12, 8);
  s_show_testrun = make_label(stat, "", &lv_font_montserrat_12, C_MUTED);
  lv_obj_set_pos(s_show_testrun, 12, 34);

  lv_obj_t *actions = make_panel(s, 12, 310, 688, 82);
  s_show_run_btn = make_button(actions, "START TEST RUN", 200, 46, on_test_start,
                               nullptr);
  lv_obj_set_pos(s_show_run_btn, 8, 6);
  lv_obj_t *stop = make_button(actions, "STOP", 110, 46, on_test_stop, nullptr);
  lv_obj_set_pos(stop, 216, 6);
  lv_obj_t *can = make_button(actions, "CAN START/STOP", 170, 46,
                              on_can_toggle, nullptr);
  lv_obj_set_pos(can, 334, 6);
  if (s_actions.mock_inject != nullptr) {
    lv_obj_t *mock = make_button(actions, "MOCK NODE", 150, 46,
                                 on_mock_inject, nullptr);
    lv_obj_set_pos(mock, 512, 6);
  }
  (void)can;
}

void build_animations(void) {
  lv_obj_t *s = s_screens[UI_SCREEN_ANIMATIONS];
  lv_obj_t *head = make_panel(s, 12, 8, 688, 48);
  make_label(head, "ANIMATIONS", &lv_font_montserrat_20, C_TEXT);
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
  lv_obj_set_width(s_anim_status, 300);

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
}

void build_grid(void) {
  lv_obj_t *s = s_screens[UI_SCREEN_GRID];
  lv_obj_t *head = make_panel(s, 12, 8, 688, 58);
  make_label(head, "GRID", &lv_font_montserrat_20, C_TEXT);
  s_grid_summary = make_label(head, "", &lv_font_montserrat_14, C_MUTED);
  lv_obj_set_pos(s_grid_summary, 120, 8);
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
  make_label(head, "DISCOVERED BOARDS", &lv_font_montserrat_20, C_TEXT);
  s_nodes_summary = make_label(head, "", &lv_font_montserrat_12, C_MUTED);
  lv_obj_align(s_nodes_summary, LV_ALIGN_RIGHT_MID, -12, 0);

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
}

void build_motor(void) {
  lv_obj_t *s = s_screens[UI_SCREEN_MOTOR];
  lv_obj_t *head = make_panel(s, 12, 8, 688, 64);
  make_label(head, "MOTOR TEST", &lv_font_montserrat_20, C_TEXT);
  s_motor_node = make_label(head, "no board", &lv_font_montserrat_14, C_TEXT);
  lv_obj_set_pos(s_motor_node, 12, 34);
  s_motor_flash = make_label(head, "", &lv_font_montserrat_12, C_MUTED);
  lv_obj_set_pos(s_motor_flash, 200, 36);
  s_motor_board = make_label(head, "", &lv_font_montserrat_12, C_MUTED);
  lv_obj_set_pos(s_motor_board, 440, 36);
  lv_obj_t *prev = make_button(head, "<", 36, 36, on_node_prev, nullptr);
  lv_obj_set_pos(prev, 596, 8);
  lv_obj_t *next = make_button(head, ">", 36, 36, on_node_next, nullptr);
  lv_obj_set_pos(next, 638, 8);

  lv_obj_t *axis = make_panel(s, 12, 80, 332, 112);
  make_label(axis, "AXIS (green = assigned)", &lv_font_montserrat_12, C_MUTED);
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
  lv_obj_set_pos(grid, 8, 28);
  lv_obj_set_size(grid, 430, 198);
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

  lv_obj_t *pos_lbl = make_label(assign, "POSITION", &lv_font_montserrat_12,
                                 C_MUTED);
  lv_obj_set_pos(pos_lbl, 456, 10);
  s_motor_assign_pos = make_label(assign, "0", &lv_font_montserrat_28, C_TEXT);
  lv_obj_set_pos(s_motor_assign_pos, 456, 28);
  lv_obj_t *set = make_button(assign, "ASSIGN", 180, 42, on_assign, nullptr);
  lv_obj_set_pos(set, 456, 76);
  lv_obj_t *unset = make_button(assign, "UNASSIGN", 180, 42, on_unassign,
                                nullptr);
  lv_obj_set_pos(unset, 456, 126);
  s_motor_result = make_label(assign, "", &lv_font_montserrat_12, C_MUTED);
  lv_obj_set_pos(s_motor_result, 456, 176);
  lv_label_set_long_mode(s_motor_result, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(s_motor_result, 200);
}

void rebuild_motor_grid(int cols, int rows) {
  if (cols == s_motor_cols && rows == s_motor_rows) return;
  s_motor_cols = cols;
  s_motor_rows = rows;

  lv_obj_clean(s_motor_grid);
  for (int i = 0; i < UI_MAX_POSITIONS; ++i) s_motor_cells[i] = nullptr;
  if (cols < 1 || rows < 1) return;

  const int gap = 3;
  const int cw = 40;
  const int ch = 28;
  const int gw = cols * cw + (cols - 1) * gap;
  const int x0 = (430 - gw) / 2;
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
  make_label(head, "CONFIGURATION", &lv_font_montserrat_20, C_TEXT);
  lv_obj_t *hint = make_label(head, "saved to SPIFFS", &lv_font_montserrat_12,
                              C_MUTED);
  lv_obj_align(hint, LV_ALIGN_RIGHT_MID, -12, 0);

  lv_obj_t *grid = make_panel(s, 12, 64, 332, 178);
  make_label(grid, "GRID SIZE", &lv_font_montserrat_14, C_MUTED);
  lv_obj_t *cols_lbl = make_label(grid, "columns", &lv_font_montserrat_12,
                                  C_MUTED);
  lv_obj_set_pos(cols_lbl, 12, 38);
  lv_obj_t *cm = make_button(grid, "-", 42, 38, on_grid_minus, nullptr);
  lv_obj_set_pos(cm, 84, 30);
  s_cfg_grid = make_label(grid, "10 x 12", &lv_font_montserrat_20, C_TEXT);
  lv_obj_set_pos(s_cfg_grid, 138, 34);
  lv_obj_t *cp = make_button(grid, "+", 42, 38, on_grid_plus, nullptr);
  lv_obj_set_pos(cp, 278, 30);
  lv_obj_t *rows_lbl = make_label(grid, "rows", &lv_font_montserrat_12, C_MUTED);
  lv_obj_set_pos(rows_lbl, 12, 82);
  lv_obj_t *rm = make_button(grid, "-", 42, 38, on_rows_minus, nullptr);
  lv_obj_set_pos(rm, 84, 74);
  lv_obj_t *rp = make_button(grid, "+", 42, 38, on_rows_plus, nullptr);
  lv_obj_set_pos(rp, 278, 74);
  lv_obj_t *p10 = make_button(grid, "10 x 10", 100, 30, on_preset_10x10,
                              nullptr);
  lv_obj_set_pos(p10, 12, 122);
  lv_obj_t *p12 = make_button(grid, "10 x 12", 100, 30, on_preset_10x12,
                              nullptr);
  lv_obj_set_pos(p12, 122, 122);

  lv_obj_t *test = make_panel(s, 356, 64, 344, 178);
  make_label(test, "COMMISSIONING TEST RUN", &lv_font_montserrat_14, C_MUTED);
  lv_obj_t *drop_lbl = make_label(test, "drop mm", &lv_font_montserrat_12,
                                  C_MUTED);
  lv_obj_set_pos(drop_lbl, 12, 36);
  lv_obj_t *dm = make_button(test, "-", 40, 32, on_drop_minus, nullptr);
  lv_obj_set_pos(dm, 88, 30);
  s_cfg_drop = make_label(test, "100", &lv_font_montserrat_16, C_TEXT);
  lv_obj_set_pos(s_cfg_drop, 138, 34);
  lv_obj_t *dp = make_button(test, "+", 40, 32, on_drop_plus, nullptr);
  lv_obj_set_pos(dp, 184, 30);
  lv_obj_t *speed_lbl = make_label(test, "speed mm/s", &lv_font_montserrat_12,
                                   C_MUTED);
  lv_obj_set_pos(speed_lbl, 12, 76);
  lv_obj_t *sm = make_button(test, "-", 40, 32, on_speed_minus, nullptr);
  lv_obj_set_pos(sm, 88, 70);
  s_cfg_speed = make_label(test, "20", &lv_font_montserrat_16, C_TEXT);
  lv_obj_set_pos(s_cfg_speed, 138, 74);
  lv_obj_t *sp = make_button(test, "+", 40, 32, on_speed_plus, nullptr);
  lv_obj_set_pos(sp, 184, 70);
  lv_obj_t *gap_lbl = make_label(test, "gap ms", &lv_font_montserrat_12, C_MUTED);
  lv_obj_set_pos(gap_lbl, 12, 116);
  lv_obj_t *gm = make_button(test, "-", 40, 32, on_delay_minus, nullptr);
  lv_obj_set_pos(gm, 88, 110);
  s_cfg_delay = make_label(test, "300", &lv_font_montserrat_16, C_TEXT);
  lv_obj_set_pos(s_cfg_delay, 138, 114);
  lv_obj_t *gp = make_button(test, "+", 40, 32, on_delay_plus, nullptr);
  lv_obj_set_pos(gp, 184, 110);
  s_cfg_serp = make_button(test, "SERP ON", 96, 72, on_serp, nullptr);
  lv_obj_set_pos(s_cfg_serp, 232, 30);

  lv_obj_t *actions = make_panel(s, 12, 250, 688, 96);
  lv_obj_t *save = make_button(actions, "SAVE TO SPIFFS", 180, 46, on_save,
                               nullptr);
  lv_obj_set_pos(save, 8, 12);
  lv_obj_t *reset = make_button(actions, "RESET DEFAULTS", 170, 46, on_reset,
                                nullptr);
  lv_obj_set_pos(reset, 196, 12);
  lv_obj_t *clear = make_button(actions, "CLEAR MAPPING", 160, 46, on_clear_map,
                                nullptr);
  lv_obj_set_pos(clear, 374, 12);
  s_cfg_status = make_label(actions, "", &lv_font_montserrat_12, C_MUTED);
  lv_obj_set_pos(s_cfg_status, 548, 26);
}

void build_log(void) {
  lv_obj_t *s = s_screens[UI_SCREEN_LOG];
  lv_obj_t *head = make_panel(s, 12, 8, 688, 48);
  make_label(head, "LOG", &lv_font_montserrat_20, C_TEXT);
  lv_obj_t *clear = make_button(head, "CLEAR", 100, 36, on_log_clear, nullptr);
  lv_obj_align(clear, LV_ALIGN_RIGHT_MID, -12, 0);
  s_log_status = make_label(head, "", &lv_font_montserrat_12, C_MUTED);
  lv_obj_align(s_log_status, LV_ALIGN_LEFT_MID, 120, 0);

  s_log_text = lv_textarea_create(s);
  lv_obj_set_pos(s_log_text, 12, 64);
  lv_obj_set_size(s_log_text, 688, 380);
  lv_textarea_set_text(s_log_text, "");
  lv_obj_set_style_bg_color(s_log_text, C_PANEL, 0);
  lv_obj_set_style_bg_opa(s_log_text, LV_OPA_COVER, 0);
  lv_obj_set_style_text_color(s_log_text, C_TEXT, 0);
  lv_obj_set_style_text_font(s_log_text, &lv_font_montserrat_12, 0);
  lv_obj_set_style_border_color(s_log_text, C_LINE, 0);
  lv_obj_set_style_border_width(s_log_text, 1, 0);
  lv_obj_clear_flag(s_log_text, LV_OBJ_FLAG_CLICKABLE);
}

void build_can(void) {
  lv_obj_t *s = s_screens[UI_SCREEN_CAN];
  lv_obj_t *head = make_panel(s, 12, 8, 688, 48);
  make_label(head, "CAN SCOPE", &lv_font_montserrat_20, C_TEXT);
  s_can_bus = make_label(head, "BUS --", &lv_font_montserrat_12, C_MUTED);
  lv_obj_align(s_can_bus, LV_ALIGN_LEFT_MID, 160, 0);
  s_can_summary = make_label(head, "", &lv_font_montserrat_12, C_MUTED);
  lv_obj_align(s_can_summary, LV_ALIGN_LEFT_MID, 236, 0);
  lv_obj_t *clear = make_button(head, "CLEAR", 100, 36, on_can_clear, nullptr);
  lv_obj_align(clear, LV_ALIGN_RIGHT_MID, -12, 0);

  char header[96];
  std::snprintf(header, sizeof(header), "%-3s %-4s %5s %s", "ID", "TYPE",
                "CNT", "DATA");
  lv_obj_t *s_can_header = lv_textarea_create(s);
  lv_obj_set_pos(s_can_header, 12, 62);
  lv_obj_set_size(s_can_header, 688, 30);
  lv_textarea_set_text(s_can_header, header);
  lv_obj_set_style_bg_color(s_can_header, C_PANEL2, 0);
  lv_obj_set_style_bg_opa(s_can_header, LV_OPA_COVER, 0);
  lv_obj_set_style_text_color(s_can_header, C_MUTED, 0);
  lv_obj_set_style_text_font(s_can_header, &lv_font_unscii_16, 0);
  lv_obj_set_style_pad_left(s_can_header, 8, 0);
  lv_obj_set_style_pad_top(s_can_header, 4, 0);
  lv_obj_set_style_pad_bottom(s_can_header, 4, 0);
  lv_obj_set_style_border_color(s_can_header, C_LINE, 0);
  lv_obj_set_style_border_width(s_can_header, 1, 0);
  lv_obj_set_style_radius(s_can_header, 6, 0);
  lv_obj_clear_flag(s_can_header, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(s_can_header, LV_OBJ_FLAG_SCROLLABLE);

  s_can_text = lv_textarea_create(s);
  lv_obj_set_pos(s_can_text, 12, 96);
  lv_obj_set_size(s_can_text, 688, 348);
  lv_textarea_set_text(s_can_text, "");
  lv_obj_set_style_bg_color(s_can_text, C_PANEL, 0);
  lv_obj_set_style_bg_opa(s_can_text, LV_OPA_COVER, 0);
  lv_obj_set_style_text_color(s_can_text, C_TEXT, 0);
  lv_obj_set_style_text_font(s_can_text, &lv_font_unscii_16, 0);
  lv_obj_set_style_pad_left(s_can_text, 8, 0);
  lv_obj_set_style_border_color(s_can_text, C_LINE, 0);
  lv_obj_set_style_border_width(s_can_text, 1, 0);
  lv_obj_clear_flag(s_can_text, LV_OBJ_FLAG_CLICKABLE);
}

void build_upgrade(void) {
  lv_obj_t *s = s_screens[UI_SCREEN_UPGRADE];
  lv_obj_t *head = make_panel(s, 12, 8, 688, 48);
  make_label(head, "NODE FIRMWARE", &lv_font_montserrat_20, C_TEXT);
  s_upgrade_summary = make_label(head, "", &lv_font_montserrat_12, C_MUTED);
  lv_obj_align(s_upgrade_summary, LV_ALIGN_RIGHT_MID, -12, 0);

  lv_obj_t *controls = make_panel(s, 12, 64, 688, 84);
  lv_obj_t *query = make_button(controls, "QUERY ALL", 140, 44,
                                on_version_query, nullptr);
  lv_obj_set_pos(query, 8, 8);
  lv_obj_t *upgrade = make_button(controls, "UPGRADE SELECTED", 190, 44,
                                  on_upgrade_start, nullptr);
  lv_obj_set_pos(upgrade, 156, 8);
  s_upgrade_status = make_label(controls, "idle", &lv_font_montserrat_12, C_MUTED);
  lv_obj_set_pos(s_upgrade_status, 356, 10);
  lv_label_set_long_mode(s_upgrade_status, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(s_upgrade_status, 300);
  s_upgrade_progress = lv_bar_create(controls);
  lv_obj_set_pos(s_upgrade_progress, 356, 44);
  lv_obj_set_size(s_upgrade_progress, 300, 8);
  lv_bar_set_range(s_upgrade_progress, 0, 100);
  lv_bar_set_value(s_upgrade_progress, 0, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(s_upgrade_progress, C_OFF, 0);
  lv_obj_set_style_bg_color(s_upgrade_progress, C_ACCENT, LV_PART_INDICATOR);
  lv_obj_set_style_radius(s_upgrade_progress, 4, 0);
  lv_obj_set_style_radius(s_upgrade_progress, 4, LV_PART_INDICATOR);

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
  s_upgrade_list = list;
  s_upgrade_shown_hash = 0;
}

void build_settings(void) {
  lv_obj_t *s = s_screens[UI_SCREEN_SETTINGS];
  lv_obj_t *head = make_panel(s, 12, 8, 688, 48);
  make_label(head, "SETTINGS", &lv_font_montserrat_20, C_TEXT);
  lv_obj_t *hint = make_label(head, "read-only status", &lv_font_montserrat_12,
                              C_MUTED);
  lv_obj_align(hint, LV_ALIGN_RIGHT_MID, -12, 0);

  lv_obj_t *net = make_panel(s, 12, 64, 332, 178);
  make_label(net, "NETWORK", &lv_font_montserrat_14, C_MUTED);
  s_set_wifi = make_kv(net, "WiFi", 34);
  s_set_ssid = make_kv(net, "SSID", 58);
  s_set_ip = make_kv(net, "IP", 82);
  s_set_rssi = make_kv(net, "RSSI", 106);
  s_set_can = make_kv(net, "CAN", 130);

  lv_obj_t *sys = make_panel(s, 356, 64, 344, 178);
  make_label(sys, "SYSTEM", &lv_font_montserrat_14, C_MUTED);
  s_set_firmware = make_kv(sys, "Firmware", 34);
  s_set_boot = make_kv(sys, "Boot", 58);
  s_set_uptime = make_kv(sys, "Uptime", 82);
  s_set_capacity = make_kv(sys, "Capacity", 106);
  s_set_watch = make_kv(sys, "Watchpoint", 130);

  lv_obj_t *service = make_panel(s, 12, 252, 688, 110);
  make_label(service, "SERVICE", &lv_font_montserrat_14, C_MUTED);
  s_set_service = make_label(
      service, "WiFi / OTA are not part of this build. Use the CH9102 UART.",
      &lv_font_montserrat_12, C_TEXT);
  lv_obj_set_pos(s_set_service, 12, 28);
  lv_label_set_long_mode(s_set_service, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(s_set_service, 620);
  lv_obj_t *log = make_button(service, "OPEN LOG", 130, 40,
                              on_tab, (void *)(intptr_t)UI_SCREEN_LOG);
  lv_obj_set_pos(log, 12, 56);
  lv_obj_t *cfg = make_button(service, "OPEN CONFIG", 150, 40,
                              on_tab, (void *)(intptr_t)UI_SCREEN_CONFIG);
  lv_obj_set_pos(cfg, 152, 56);
}

}  // namespace

void ui_init(const ui_actions_t *actions) {
  if (actions != nullptr) s_actions = *actions;
  std::memset(&s_snap, 0, sizeof(s_snap));

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

  s_screen = UI_SCREEN_SHOW;
  ui_show_screen(UI_SCREEN_SHOW);
}

void ui_show_screen(int index) {
  if (index < 0) index = 0;
  if (index >= UI_SCREEN_COUNT) index = UI_SCREEN_COUNT - 1;
  s_screen = index;
  if (index == UI_SCREEN_LOG) s_log_last_ms = 0;
  if (index == UI_SCREEN_CAN) s_can_last_ms = 0;
  for (int i = 0; i < UI_SCREEN_COUNT; ++i) {
    set_hidden(s_screens[i], i != index);
  }
  if (s_nav != nullptr) lv_obj_delete(s_nav);
  s_nav = nullptr;
  build_nav(index);
}

int ui_current_screen(void) { return s_screen; }

void ui_update(const ui_snapshot_t *snapshot) {
  if (snapshot == nullptr) return;
  s_snap = *snapshot;

  if (s_selected_axis < 0) s_selected_axis = 0;
  if (s_selected_axis > 3) s_selected_axis = 3;
  const int cap = (s_snap.capacity > 0) ? (int)s_snap.capacity : UI_MAX_POSITIONS;
  if (s_assign_position < 0) s_assign_position = 0;
  if (s_assign_position >= cap) s_assign_position = cap - 1;

  /* Status strip link state. */
  lv_label_set_text_fmt(s_link_label, "%s  %s  boot %lu  uptime %lus",
                        s_snap.can_running ? "CAN" : "CAN OFF",
                        s_snap.can_mock ? "MOCK" : "HW",
                        (unsigned long)s_snap.boot_count,
                        (unsigned long)s_snap.uptime_s);

  /* Show */
  lv_label_set_text(s_show_pattern,
                    s_snap.pattern_name[0] ? s_snap.pattern_name : "NO PATTERN");
  lv_label_set_text(s_show_state, s_snap.run_state);
  lv_label_set_text_fmt(s_show_elapsed, "%lu.%lus",
                        (unsigned long)(s_snap.elapsed_ms / 1000),
                        (unsigned long)((s_snap.elapsed_ms % 1000) / 100));
  lv_label_set_text(s_show_bus,
                    s_snap.bus_ok ? (s_snap.can_mock ? "MOCK OK" : "1 Mbit/s OK")
                                  : "FAULT / SILENT");
  lv_obj_set_style_text_color(s_show_bus,
                              s_snap.bus_ok ? C_OK : C_ERR, 0);
  lv_label_set_text(s_show_hold,
                    s_snap.safe_hold ? s_snap.hold_reason : "");
  lv_label_set_text_fmt(s_show_discovered, "boards %lu",
                        (unsigned long)s_snap.discovered_boards);
  lv_label_set_text_fmt(s_show_assigned_boards, "assigned %lu",
                        (unsigned long)s_snap.assigned_boards);
  lv_label_set_text_fmt(s_show_online, "online %lu",
                        (unsigned long)s_snap.online_boards);
  lv_label_set_text_fmt(s_show_positions, "positions %lu / %lu",
                        (unsigned long)s_snap.assigned_positions,
                        (unsigned long)s_snap.capacity);
  lv_label_set_text_fmt(s_show_unassigned,
                        "%lu steppers unassigned, %lu positions free",
                        (unsigned long)s_snap.unassigned_steppers,
                        (unsigned long)s_snap.unassigned_positions);
  lv_label_set_text_fmt(
      s_show_testrun, "test run: %s%s  |  rx %lu tx %lu err %lu",
      s_snap.test_run_active ? "ACTIVE " : "idle",
      s_snap.test_run_active ? "" : " ",
      (unsigned long)s_snap.can_rx_frames, (unsigned long)s_snap.can_tx_frames,
      (unsigned long)s_snap.can_errors);
  lv_obj_t *lbl = lv_obj_get_child(s_show_run_btn, 0);
  lv_label_set_text(lbl, s_snap.test_run_active ? "TEST RUNNING" : "START TEST RUN");
  lv_obj_set_style_bg_color(s_show_run_btn,
                            s_snap.test_run_active ? C_WARN : C_PANEL2, 0);

  /* Animations */
  uint32_t anim_hash = 2166136261u;
  for (int i = 0; i < s_snap.animation_count; ++i) {
    const ui_animation_t *a = &s_snap.animations[i];
    anim_hash = (anim_hash ^ (uint32_t)a->size_bytes) * 16777619u;
    anim_hash = (anim_hash ^ a->duration_ms) * 16777619u;
    anim_hash = (anim_hash ^ (a->valid ? 1u : 0u)) * 16777619u;
    for (const char *p = a->name; *p != '\0'; ++p) {
      anim_hash = (anim_hash ^ (uint8_t)*p) * 16777619u;
    }
  }
  anim_hash = (anim_hash ^ (uint32_t)s_snap.selected_animation) * 16777619u;
  if (anim_hash != s_anim_shown_hash) {
    s_anim_shown_hash = anim_hash;
    lv_obj_clean(s_anim_list);
    if (s_snap.animation_count == 0) {
      lv_obj_t *l = make_label(s_anim_list,
                               "No animations found. Insert an SD card with "
                               ".hbp files.",
                               &lv_font_montserrat_16, C_MUTED);
      lv_obj_set_style_pad_all(l, 12, 0);
    }
    for (int i = 0; i < s_snap.animation_count; ++i) {
      const ui_animation_t *a = &s_snap.animations[i];
      lv_obj_t *row = lv_button_create(s_anim_list);
      lv_obj_set_size(row, 664, 48);
      lv_obj_set_style_radius(row, 6, 0);
      lv_obj_set_style_bg_color(
          row, (i == s_snap.selected_animation) ? C_PANEL2 : C_PANEL, 0);
      lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
      lv_obj_set_style_border_width(row, 1, 0);
      lv_obj_set_style_border_color(
          row, (i == s_snap.selected_animation) ? C_ACCENT : C_LINE, 0);
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
                        s_snap.animation_count, s_snap.animation_source);
  lv_label_set_text(s_anim_status,
                    s_snap.animation_status[0] ? s_snap.animation_status
                                               : "idle");
  lv_obj_set_style_text_color(s_anim_status,
                              s_snap.animation_loading ? C_WARN : C_MUTED, 0);

  /* Grid */
  rebuild_grid(s_snap.grid_cols, s_snap.grid_rows);
  lv_label_set_text_fmt(s_grid_summary, "%d x %d, %lu assigned, %lu free",
                        s_snap.grid_cols, s_snap.grid_rows,
                        (unsigned long)s_snap.assigned_positions,
                        (unsigned long)s_snap.unassigned_positions);
  const int grid_cap = s_snap.grid_cols * s_snap.grid_rows;
  for (int i = 0; i < grid_cap && i < UI_MAX_POSITIONS; ++i) {
    lv_obj_t *cell = s_grid_cells[i];
    if (cell == nullptr) continue;
    const ui_cell_t *c = &s_snap.cells[i];
    lv_color_t bg = c->assigned ? C_OK : C_OFF;
    const ui_node_t *n = nullptr;
    if (c->node_index >= 0 && c->node_index < s_snap.node_count) {
      n = &s_snap.nodes[c->node_index];
    }
    if (n != nullptr && n->status_seen && n->fault_flags != 0) bg = C_ERR;
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
  for (int i = 0; i < s_snap.node_count; ++i) {
    const ui_node_t *n = &s_snap.nodes[i];
    hash = (hash ^ n->board_id) * 16777619u;
    hash = (hash ^ n->state) * 16777619u;
    hash = (hash ^ n->fault_flags) * 16777619u;
    hash = (hash ^ (n->online ? 1u : 0u)) * 16777619u;
    hash = (hash ^ (uint32_t)n->assigned_positions) * 16777619u;
  }
  hash = (hash ^ (uint32_t)s_snap.selected_node) * 16777619u;
  if (hash != s_nodes_shown_hash) {
    s_nodes_shown_hash = hash;
    lv_obj_clean(s_nodes_list);
    if (s_snap.node_count == 0) {
      lv_obj_t *l = make_label(s_nodes_list, "No boards discovered yet.",
                               &lv_font_montserrat_16, C_MUTED);
      lv_obj_set_style_pad_all(l, 12, 0);
    }
    for (int i = 0; i < s_snap.node_count; ++i) {
      const ui_node_t *n = &s_snap.nodes[i];
      lv_obj_t *row = lv_button_create(s_nodes_list);
      lv_obj_set_size(row, 664, 48);
      lv_obj_set_style_radius(row, 6, 0);
      lv_obj_set_style_bg_color(
          row, (i == s_snap.selected_node) ? C_PANEL2 : C_PANEL, 0);
      lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
      lv_obj_set_style_border_width(row, 1, 0);
      lv_obj_set_style_border_color(
          row, (i == s_snap.selected_node) ? C_ACCENT : C_LINE, 0);
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
      std::snprintf(flash, sizeof(flash), "%02x%02x%02x%02x%02x%02x%02x%02x",
                    n->flash_id[0], n->flash_id[1], n->flash_id[2],
                    n->flash_id[3], n->flash_id[4], n->flash_id[5],
                    n->flash_id[6], n->flash_id[7]);
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
      make_label(row, flash, &lv_font_montserrat_12, C_TEXT);
      make_label(row, board, &lv_font_montserrat_14,
                 n->board_id >= 0 ? C_OK : C_WARN);
      make_label(row, state, &lv_font_montserrat_12, C_MUTED);
      make_label(row, pos, &lv_font_montserrat_12, C_MUTED);
    }
  }
  lv_label_set_text_fmt(s_nodes_summary, "%lu discovered, %lu online, %lu assigned",
                        (unsigned long)s_snap.discovered_boards,
                        (unsigned long)s_snap.online_boards,
                        (unsigned long)s_snap.assigned_boards);

  /* Motor */
  const ui_node_t *sel = nullptr;
  if (s_snap.selected_node >= 0 && s_snap.selected_node < s_snap.node_count) {
    sel = &s_snap.nodes[s_snap.selected_node];
  }
  if (sel != nullptr) {
    char flash[24];
    std::snprintf(flash, sizeof(flash), "%02x%02x%02x%02x%02x%02x%02x%02x",
                  sel->flash_id[0], sel->flash_id[1], sel->flash_id[2],
                  sel->flash_id[3], sel->flash_id[4], sel->flash_id[5],
                  sel->flash_id[6], sel->flash_id[7]);
    lv_label_set_text_fmt(s_motor_node, "#%d  %s", s_snap.selected_node,
                          sel->online ? "online" : "offline");
    lv_label_set_text(s_motor_flash, flash);
    if (sel->board_id >= 0) {
      lv_label_set_text_fmt(s_motor_board, "board %d", sel->board_id);
    } else {
      lv_label_set_text(s_motor_board, "board unassigned");
    }
    lv_label_set_text_fmt(s_motor_result,
                          "%d positions assigned to this board, buffer %u, faults 0x%02x",
                          sel->assigned_positions, (unsigned)sel->buffer_depth,
                          (unsigned)sel->fault_flags);
  } else {
    lv_label_set_text(s_motor_node, "no board selected");
    lv_label_set_text(s_motor_flash, "");
    lv_label_set_text(s_motor_board, "");
    lv_label_set_text(s_motor_result, "Discover boards on the Nodes screen first.");
  }
  rebuild_motor_grid(s_snap.grid_cols, s_snap.grid_rows);
  bool axis_assigned[4] = {false, false, false, false};
  const int motor_cap = s_snap.grid_cols * s_snap.grid_rows;
  for (int i = 0; i < motor_cap && i < UI_MAX_POSITIONS; ++i) {
    const ui_cell_t *c = &s_snap.cells[i];
    if (c->assigned && c->node_index == s_snap.selected_node && c->axis < 4) {
      axis_assigned[c->axis] = true;
    }
  }
  for (int i = 0; i < 4; ++i) {
    lv_obj_t *btn = s_motor_axis_btn[i];
    lv_obj_set_style_bg_color(
        btn, axis_assigned[i] ? C_OK
                              : ((i == s_selected_axis) ? C_ACCENT : C_PANEL2),
        0);
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
    const ui_cell_t *c = &s_snap.cells[i];
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
  lv_label_set_text_fmt(s_cfg_grid, "%d x %d", s_snap.grid_cols, s_snap.grid_rows);
  lv_label_set_text_fmt(s_cfg_drop, "%u", (unsigned)s_snap.test_drop_mm);
  lv_label_set_text_fmt(s_cfg_speed, "%u", (unsigned)s_snap.test_speed_mm_s);
  lv_label_set_text_fmt(s_cfg_delay, "%u", (unsigned)s_snap.test_step_delay_ms);
  lv_obj_t *serp_lbl = lv_obj_get_child(s_cfg_serp, 0);
  lv_label_set_text(serp_lbl, s_snap.test_serpentine ? "SERP ON" : "SERP OFF");
  lv_obj_set_style_bg_color(s_cfg_serp,
                            s_snap.test_serpentine ? C_OK : C_PANEL2, 0);
  lv_label_set_text(s_cfg_status,
                    s_snap.config_dirty ? "UNSAVED CHANGES" : "saved");
  lv_obj_set_style_text_color(s_cfg_status,
                              s_snap.config_dirty ? C_WARN : C_OK, 0);

  /* CAN scope */
  if (!s_snap.can_running) {
    lv_label_set_text(s_can_bus, "STOPPED");
    lv_obj_set_style_text_color(s_can_bus, C_MUTED, 0);
  } else if (s_snap.can_bus_off) {
    lv_label_set_text(s_can_bus, "BUS OFF");
    lv_obj_set_style_text_color(s_can_bus, C_ERR, 0);
  } else {
    lv_label_set_text(s_can_bus, "BUS OK");
    lv_obj_set_style_text_color(s_can_bus, C_OK, 0);
  }
  lv_label_set_text_fmt(s_can_summary,
                        "rx %lu/s  %lu ids  tx %lu sent  %lu rej  %lu drop",
                        (unsigned long)s_snap.can_rate_1s,
                        (unsigned long)s_snap.can_unique_ids,
                        (unsigned long)s_snap.can_tx_frames,
                        (unsigned long)s_snap.can_tx_failed,
                        (unsigned long)s_snap.can_rx_dropped);
  uint32_t can_hash = 2166136261u;
  for (int i = 0; i < s_snap.can_entry_count; ++i) {
    const ui_can_entry_t *e = &s_snap.can_entries[i];
    can_hash = (can_hash ^ e->identifier) * 16777619u;
    can_hash = (can_hash ^ e->frame_count) * 16777619u;
    can_hash = (can_hash ^ e->interval_ms) * 16777619u;
    for (int b = 0; b < e->data_len; ++b) {
      can_hash = (can_hash ^ e->data[b]) * 16777619u;
    }
  }
  can_hash = (can_hash ^ s_snap.can_rate_1s) * 16777619u;
  can_hash = (can_hash ^ s_snap.can_dropped) * 16777619u;
  if (s_screen == UI_SCREEN_CAN && can_hash != s_can_shown_hash) {
    const uint32_t can_now = lv_tick_get();
    if (s_can_last_ms == 0 || (uint32_t)(can_now - s_can_last_ms) >= 1000u) {
      s_can_shown_hash = can_hash;
      s_can_last_ms = can_now;
      size_t off = 0;
      for (int i = 0; i < s_snap.can_entry_count; ++i) {
        const ui_can_entry_t *e = &s_snap.can_entries[i];
        char data[24];
        size_t dp = 0;
        for (int b = 0; b < e->data_len && b < 8; ++b) {
          dp += (size_t)std::snprintf(data + dp, sizeof(data) - dp, "%02x",
                                      e->data[b]);
        }
        data[dp] = '\0';
        off += (size_t)std::snprintf(
            s_can_buf + off, sizeof(s_can_buf) - off,
            "%03lx %-4u %5lu %s\n",
            (unsigned long)e->identifier, (unsigned)e->type,
            (unsigned long)e->frame_count, data);
        if (off >= sizeof(s_can_buf) - 1) break;
      }
      if (s_snap.can_entry_count == 0) {
        std::snprintf(s_can_buf, sizeof(s_can_buf), "no CAN frames yet");
      }
      lv_textarea_set_text(s_can_text, s_can_buf);
    }
  }

  /* Upgrade */
  int versioned = 0;
  uint32_t up_hash = 2166136261u;
  for (int i = 0; i < s_snap.node_count; ++i) {
    const ui_node_t *n = &s_snap.nodes[i];
    if (n->version_seen) versioned++;
    up_hash = (up_hash ^ n->board_id) * 16777619u;
    up_hash = (up_hash ^ (n->online ? 1u : 0u)) * 16777619u;
    up_hash = (up_hash ^ (n->version_seen ? 1u : 0u)) * 16777619u;
    for (const char *p = n->sw_version; *p != '\0'; ++p) {
      up_hash = (up_hash ^ (uint8_t)*p) * 16777619u;
    }
  }
  up_hash = (up_hash ^ (uint32_t)s_snap.selected_node) * 16777619u;
  up_hash = (up_hash ^ s_snap.upgrade_progress) * 16777619u;
  if (up_hash != s_upgrade_shown_hash) {
    s_upgrade_shown_hash = up_hash;
    lv_obj_clean(s_upgrade_list);
    if (s_snap.node_count == 0) {
      lv_obj_t *l = make_label(s_upgrade_list, "No boards discovered yet.",
                               &lv_font_montserrat_16, C_MUTED);
      lv_obj_set_style_pad_all(l, 12, 0);
    }
    for (int i = 0; i < s_snap.node_count; ++i) {
      const ui_node_t *n = &s_snap.nodes[i];
      lv_obj_t *row = lv_button_create(s_upgrade_list);
      lv_obj_set_size(row, 664, 48);
      lv_obj_set_style_radius(row, 6, 0);
      lv_obj_set_style_bg_color(
          row, (i == s_snap.selected_node) ? C_PANEL2 : C_PANEL, 0);
      lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
      lv_obj_set_style_border_width(row, 1, 0);
      lv_obj_set_style_border_color(
          row, (i == s_snap.selected_node) ? C_ACCENT : C_LINE, 0);
      lv_obj_set_style_shadow_width(row, 0, 0);
      lv_obj_set_style_pad_left(row, 10, 0);
      lv_obj_set_style_pad_right(row, 10, 0);
      lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
      lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                            LV_FLEX_ALIGN_CENTER);
      lv_obj_set_style_pad_column(row, 14, 0);
      lv_obj_add_event_cb(row, on_upgrade_row, LV_EVENT_CLICKED,
                          (void *)(intptr_t)i);
      char flash[24];
      std::snprintf(flash, sizeof(flash), "%02x%02x%02x%02x%02x%02x%02x%02x",
                    n->flash_id[0], n->flash_id[1], n->flash_id[2],
                    n->flash_id[3], n->flash_id[4], n->flash_id[5],
                    n->flash_id[6], n->flash_id[7]);
      char board[24];
      if (n->board_id >= 0) {
        std::snprintf(board, sizeof(board), "board %d", n->board_id);
      } else {
        std::snprintf(board, sizeof(board), "unassigned");
      }
      char version[32];
      std::snprintf(version, sizeof(version), "%s",
                    n->version_seen && n->sw_version[0] ? n->sw_version : "--");
      char state[40];
      std::snprintf(state, sizeof(state), "%s%s",
                    n->online ? "online" : "offline",
                    n->version_seen ? "" : " (no version)");
      make_label(row, flash, &lv_font_montserrat_12, C_TEXT);
      make_label(row, board, &lv_font_montserrat_14,
                 n->board_id >= 0 ? C_OK : C_WARN);
      make_label(row, version, &lv_font_montserrat_14,
                 n->version_seen ? C_TEXT : C_MUTED);
      make_label(row, state, &lv_font_montserrat_12, C_MUTED);
    }
  }
  lv_label_set_text_fmt(s_upgrade_summary, "%d boards, %d report version",
                        s_snap.node_count, versioned);
  lv_label_set_text(s_upgrade_status,
                    s_snap.upgrade_status[0] ? s_snap.upgrade_status : "idle");
  lv_obj_set_style_text_color(s_upgrade_status,
                              s_snap.upgrade_active ? C_WARN : C_MUTED, 0);
  lv_bar_set_value(s_upgrade_progress, s_snap.upgrade_progress, LV_ANIM_OFF);

  /* Settings */
  if (s_snap.wifi_present) {
    lv_label_set_text(s_set_wifi,
                      s_snap.wifi_connected ? "connected" : "offline");
    lv_obj_set_style_text_color(s_set_wifi,
                                s_snap.wifi_connected ? C_OK : C_WARN, 0);
    lv_label_set_text(s_set_ssid,
                      s_snap.wifi_ssid[0] ? s_snap.wifi_ssid : "--");
    lv_label_set_text(s_set_ip, s_snap.wifi_ip[0] ? s_snap.wifi_ip : "--");
    lv_label_set_text_fmt(s_set_rssi, "%d dBm", (int)s_snap.wifi_rssi);
  } else {
    lv_label_set_text(s_set_wifi, "not compiled");
    lv_obj_set_style_text_color(s_set_wifi, C_MUTED, 0);
    lv_label_set_text(s_set_ssid, "--");
    lv_label_set_text(s_set_ip, "--");
    lv_label_set_text(s_set_rssi, "--");
  }
  lv_label_set_text_fmt(s_set_can, "%s %s",
                        s_snap.can_running ? "running" : "stopped",
                        s_snap.can_mock ? "MOCK" : "HW");
  lv_label_set_text(s_set_firmware, s_snap.firmware_version);
  lv_label_set_text_fmt(s_set_boot, "%lu", (unsigned long)s_snap.boot_count);
  lv_label_set_text_fmt(s_set_uptime, "%lu s", (unsigned long)s_snap.uptime_s);
  lv_label_set_text_fmt(s_set_capacity, "%lu positions",
                        (unsigned long)s_snap.capacity);
  lv_label_set_text_fmt(s_set_watch, "%u", (unsigned)s_snap.last_watchpoint);

  /* Log */
  uint32_t log_hash = 2166136261u;
  for (const char *p = s_snap.log_text; *p != '\0'; ++p) {
    log_hash = (log_hash ^ (uint8_t)*p) * 16777619u;
  }
  const uint32_t now = lv_tick_get();
  if (log_hash != s_log_hash &&
      (s_log_last_ms == 0 || (uint32_t)(now - s_log_last_ms) >= 500u)) {
    s_log_hash = log_hash;
    s_log_last_ms = now;
    lv_textarea_set_text(s_log_text, s_snap.log_text);
  }
  lv_label_set_text_fmt(s_log_status, "boot #%lu  last watchpoint %u  %s",
                        (unsigned long)s_snap.boot_count,
                        (unsigned)s_snap.last_watchpoint,
                        s_snap.firmware_version);
}
