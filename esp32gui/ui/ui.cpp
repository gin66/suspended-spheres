#include "ui.h"

#include <cstdio>
#include <cstring>

#include <lvgl.h>

namespace {

constexpr int kWidth = 800;
constexpr int kTopBar = 56;
constexpr int kContentY = 60;
constexpr int kContentH = 416;

const lv_color_t C_BG = lv_color_hex(0x0E1116);
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
lv_obj_t *s_tabs[UI_SCREEN_COUNT];
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

/* ── helpers ───────────────────────────────────────────────────────────── */

lv_obj_t *make_label(lv_obj_t *parent, const char *text, const lv_font_t *font,
                     lv_color_t color) {
  lv_obj_t *l = lv_label_create(parent);
  lv_label_set_text(l, text != nullptr ? text : "");
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, color, 0);
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

void set_hidden(lv_obj_t *obj, bool hidden) {
  if (hidden) {
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
  }
}

void on_tab(lv_event_t *e) {
  const int index = (int)(intptr_t)lv_event_get_user_data(e);
  ui_show_screen(index);
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

void on_assign_minus(lv_event_t *e) {
  (void)e;
  if (s_assign_position > 0) s_assign_position--;
  ui_update(&s_snap);
}

void on_assign_plus(lv_event_t *e) {
  (void)e;
  const int cap = (s_snap.capacity > 0) ? (int)s_snap.capacity : UI_MAX_POSITIONS;
  if (s_assign_position + 1 < cap) s_assign_position++;
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

/* ── screen construction ───────────────────────────────────────────────── */

lv_obj_t *make_screen(void) {
  lv_obj_t *s = lv_obj_create(lv_screen_active());
  lv_obj_remove_style_all(s);
  lv_obj_set_pos(s, 0, kContentY);
  lv_obj_set_size(s, kWidth, kContentH);
  lv_obj_set_style_bg_opa(s, LV_OPA_TRANSP, 0);
  lv_obj_clear_flag(s, LV_OBJ_FLAG_SCROLLABLE);
  return s;
}

void build_topbar(void) {
  lv_obj_t *bar = lv_obj_create(lv_screen_active());
  lv_obj_remove_style_all(bar);
  lv_obj_set_pos(bar, 0, 0);
  lv_obj_set_size(bar, kWidth, kTopBar);
  lv_obj_set_style_bg_color(bar, C_PANEL, 0);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(bar, 1, 0);
  lv_obj_set_style_border_color(bar, C_LINE, 0);
  lv_obj_set_style_pad_left(bar, 12, 0);
  lv_obj_set_style_pad_right(bar, 12, 0);
  lv_obj_set_style_pad_top(bar, 8, 0);
  lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(bar, 6, 0);

  static const char *names[UI_SCREEN_COUNT] = {"SHOW", "GRID", "NODES",
                                               "MOTOR", "CONFIG", "LOG"};
  for (int i = 0; i < UI_SCREEN_COUNT; ++i) {
    s_tabs[i] = make_button(bar, names[i], 116, 38, on_tab,
                            (void *)(intptr_t)i);
  }

  s_link_label = make_label(bar, "", &lv_font_montserrat_12, C_MUTED);
  lv_obj_set_flex_grow(s_link_label, 1);
  lv_obj_set_style_text_align(s_link_label, LV_TEXT_ALIGN_RIGHT, 0);
}

void build_show(void) {
  lv_obj_t *s = s_screens[UI_SCREEN_SHOW];

  lv_obj_t *title = make_panel(s, 16, 8, 768, 92);
  s_show_pattern = make_label(title, "NO PATTERN", &lv_font_montserrat_28,
                              C_TEXT);
  lv_obj_set_pos(s_show_pattern, 12, 8);
  s_show_state = make_label(title, "idle", &lv_font_montserrat_16, C_MUTED);
  lv_obj_set_pos(s_show_state, 12, 52);
  s_show_elapsed = make_label(title, "0:00", &lv_font_montserrat_20, C_MUTED);
  lv_obj_align(s_show_elapsed, LV_ALIGN_RIGHT_MID, -12, 0);

  lv_obj_t *bus = make_panel(s, 16, 108, 372, 126);
  make_label(bus, "CAN BUS", &lv_font_montserrat_14, C_MUTED);
  s_show_bus = make_label(bus, "--", &lv_font_montserrat_20, C_TEXT);
  lv_obj_set_pos(s_show_bus, 12, 28);
  s_show_hold = make_label(bus, "", &lv_font_montserrat_14, C_ERR);
  lv_obj_set_pos(s_show_hold, 12, 68);
  lv_label_set_long_mode(s_show_hold, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(s_show_hold, 340);

  lv_obj_t *comm = make_panel(s, 404, 108, 380, 126);
  make_label(comm, "COMMISSIONING", &lv_font_montserrat_14, C_MUTED);
  s_show_discovered = make_label(comm, "boards --", &lv_font_montserrat_16, C_TEXT);
  lv_obj_set_pos(s_show_discovered, 12, 26);
  s_show_assigned_boards = make_label(comm, "assigned --", &lv_font_montserrat_16, C_TEXT);
  lv_obj_set_pos(s_show_assigned_boards, 200, 26);
  s_show_online = make_label(comm, "online --", &lv_font_montserrat_16, C_TEXT);
  lv_obj_set_pos(s_show_online, 12, 52);
  s_show_positions = make_label(comm, "positions --", &lv_font_montserrat_16,
                                C_TEXT);
  lv_obj_set_pos(s_show_positions, 200, 52);

  lv_obj_t *stat = make_panel(s, 16, 244, 768, 76);
  s_show_unassigned = make_label(stat, "", &lv_font_montserrat_16, C_WARN);
  lv_obj_set_pos(s_show_unassigned, 12, 10);
  s_show_testrun = make_label(stat, "", &lv_font_montserrat_14, C_MUTED);
  lv_obj_set_pos(s_show_testrun, 12, 38);

  lv_obj_t *actions = make_panel(s, 16, 330, 768, 74);
  s_show_run_btn = make_button(actions, "START TEST RUN", 220, 46, on_test_start,
                               nullptr);
  lv_obj_set_pos(s_show_run_btn, 8, 8);
  lv_obj_t *stop = make_button(actions, "STOP", 120, 46, on_test_stop, nullptr);
  lv_obj_set_pos(stop, 240, 8);
  lv_obj_t *can = make_button(actions, "CAN START/STOP", 180, 46,
                              on_can_toggle, nullptr);
  lv_obj_set_pos(can, 372, 8);
  if (s_actions.mock_inject != nullptr) {
    lv_obj_t *mock = make_button(actions, "MOCK NODE", 150, 46,
                                 on_mock_inject, nullptr);
    lv_obj_set_pos(mock, 564, 8);
  }
  (void)can;
}

void build_grid(void) {
  lv_obj_t *s = s_screens[UI_SCREEN_GRID];
  lv_obj_t *head = make_panel(s, 16, 8, 768, 66);
  make_label(head, "GRID", &lv_font_montserrat_20, C_TEXT);
  s_grid_summary = make_label(head, "", &lv_font_montserrat_14, C_MUTED);
  lv_obj_set_pos(s_grid_summary, 12, 30);
  lv_obj_t *hint = make_label(head, "row 0 = back, column 0 = left",
                              &lv_font_montserrat_12, C_MUTED);
  lv_obj_align(hint, LV_ALIGN_RIGHT_MID, -12, 0);

  lv_obj_t *body = lv_obj_create(s);
  lv_obj_remove_style_all(body);
  lv_obj_set_pos(body, 16, 82);
  lv_obj_set_size(body, 768, 326);
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
  const int cw = (740 - (cols - 1) * gap) / cols;
  const int ch = (300 - (rows - 1) * gap) / rows;
  const int gw = cols * cw + (cols - 1) * gap;
  const int gh = rows * ch + (rows - 1) * gap;
  const int x0 = (768 - gw) / 2;
  const int y0 = (326 - gh) / 2;

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
  lv_obj_t *head = make_panel(s, 16, 8, 768, 66);
  make_label(head, "DISCOVERED BOARDS", &lv_font_montserrat_20, C_TEXT);
  s_nodes_summary = make_label(head, "", &lv_font_montserrat_14, C_MUTED);
  lv_obj_set_pos(s_nodes_summary, 300, 30);

  lv_obj_t *list = lv_obj_create(s);
  lv_obj_remove_style_all(list);
  lv_obj_set_pos(list, 16, 82);
  lv_obj_set_size(list, 768, 326);
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
  lv_obj_t *head = make_panel(s, 16, 8, 768, 76);
  make_label(head, "MOTOR TEST", &lv_font_montserrat_20, C_TEXT);
  s_motor_node = make_label(head, "no board", &lv_font_montserrat_14, C_TEXT);
  lv_obj_set_pos(s_motor_node, 12, 36);
  s_motor_flash = make_label(head, "", &lv_font_montserrat_12, C_MUTED);
  lv_obj_set_pos(s_motor_flash, 240, 36);
  s_motor_board = make_label(head, "", &lv_font_montserrat_12, C_MUTED);
  lv_obj_set_pos(s_motor_board, 560, 36);
  lv_obj_t *prev = make_button(head, "<", 36, 36, on_node_prev, nullptr);
  lv_obj_set_pos(prev, 660, 8);
  lv_obj_t *next = make_button(head, ">", 36, 36, on_node_next, nullptr);
  lv_obj_set_pos(next, 702, 8);

  lv_obj_t *axis = make_panel(s, 16, 94, 372, 120);
  make_label(axis, "AXIS (Drive 0-3)", &lv_font_montserrat_14, C_MUTED);
  for (int i = 0; i < 4; ++i) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "AX %d", i);
    s_motor_axis_btn[i] = make_button(axis, buf, 78, 44, on_axis,
                                      (void *)(intptr_t)i);
    lv_obj_set_pos(s_motor_axis_btn[i], 10 + i * 89, 46);
  }

  lv_obj_t *jog = make_panel(s, 404, 94, 380, 120);
  make_label(jog, "JOG / HOME", &lv_font_montserrat_14, C_MUTED);
  lv_obj_t *home = make_button(jog, "HOME", 80, 46, on_motor, (void *)0);
  lv_obj_set_pos(home, 10, 46);
  lv_obj_t *up = make_button(jog, "UP", 80, 46, on_motor, (void *)1);
  lv_obj_set_pos(up, 98, 46);
  lv_obj_t *down = make_button(jog, "DOWN", 80, 46, on_motor, (void *)2);
  lv_obj_set_pos(down, 186, 46);
  lv_obj_t *stop = make_button(jog, "STOP", 80, 46, on_motor, (void *)3);
  lv_obj_set_pos(stop, 274, 46);

  lv_obj_t *assign = make_panel(s, 16, 226, 768, 128);
  make_label(assign, "ASSIGN TO GRID POSITION", &lv_font_montserrat_14, C_MUTED);
  lv_obj_t *minus = make_button(assign, "-", 54, 44, on_assign_minus, nullptr);
  lv_obj_set_pos(minus, 12, 40);
  s_motor_assign_pos = make_label(assign, "0", &lv_font_montserrat_28, C_TEXT);
  lv_obj_set_pos(s_motor_assign_pos, 82, 44);
  lv_obj_t *plus = make_button(assign, "+", 54, 44, on_assign_plus, nullptr);
  lv_obj_set_pos(plus, 160, 40);
  lv_obj_t *set = make_button(assign, "ASSIGN", 130, 44, on_assign, nullptr);
  lv_obj_set_pos(set, 240, 40);
  lv_obj_t *unset = make_button(assign, "UNASSIGN", 140, 44, on_unassign, nullptr);
  lv_obj_set_pos(unset, 382, 40);
  s_motor_result = make_label(assign, "", &lv_font_montserrat_12, C_MUTED);
  lv_obj_set_pos(s_motor_result, 12, 92);
}

void build_config(void) {
  lv_obj_t *s = s_screens[UI_SCREEN_CONFIG];
  lv_obj_t *head = make_panel(s, 16, 8, 768, 54);
  make_label(head, "CONFIGURATION", &lv_font_montserrat_20, C_TEXT);
  lv_obj_t *hint = make_label(head, "saved to SPIFFS", &lv_font_montserrat_12,
                              C_MUTED);
  lv_obj_align(hint, LV_ALIGN_RIGHT_MID, -12, 0);

  lv_obj_t *grid = make_panel(s, 16, 72, 372, 176);
  make_label(grid, "GRID SIZE", &lv_font_montserrat_14, C_MUTED);
  lv_obj_t *cols_lbl = make_label(grid, "columns", &lv_font_montserrat_12,
                                  C_MUTED);
  lv_obj_set_pos(cols_lbl, 12, 38);
  lv_obj_t *cm = make_button(grid, "-", 42, 38, on_grid_minus, nullptr);
  lv_obj_set_pos(cm, 90, 30);
  s_cfg_grid = make_label(grid, "10 x 12", &lv_font_montserrat_20, C_TEXT);
  lv_obj_set_pos(s_cfg_grid, 144, 34);
  lv_obj_t *cp = make_button(grid, "+", 42, 38, on_grid_plus, nullptr);
  lv_obj_set_pos(cp, 290, 30);
  lv_obj_t *rows_lbl = make_label(grid, "rows", &lv_font_montserrat_12, C_MUTED);
  lv_obj_set_pos(rows_lbl, 12, 82);
  lv_obj_t *rm = make_button(grid, "-", 42, 38, on_rows_minus, nullptr);
  lv_obj_set_pos(rm, 90, 74);
  lv_obj_t *rp = make_button(grid, "+", 42, 38, on_rows_plus, nullptr);
  lv_obj_set_pos(rp, 290, 74);
  lv_obj_t *p10 = make_button(grid, "10 x 10", 100, 30, on_preset_10x10,
                              nullptr);
  lv_obj_set_pos(p10, 12, 118);
  lv_obj_t *p12 = make_button(grid, "10 x 12", 100, 30, on_preset_10x12,
                              nullptr);
  lv_obj_set_pos(p12, 122, 118);

  lv_obj_t *test = make_panel(s, 404, 72, 380, 176);
  make_label(test, "COMMISSIONING TEST RUN", &lv_font_montserrat_14, C_MUTED);
  lv_obj_t *drop_lbl = make_label(test, "drop mm", &lv_font_montserrat_12,
                                  C_MUTED);
  lv_obj_set_pos(drop_lbl, 12, 36);
  lv_obj_t *dm = make_button(test, "-", 40, 32, on_drop_minus, nullptr);
  lv_obj_set_pos(dm, 100, 30);
  s_cfg_drop = make_label(test, "100", &lv_font_montserrat_16, C_TEXT);
  lv_obj_set_pos(s_cfg_drop, 150, 34);
  lv_obj_t *dp = make_button(test, "+", 40, 32, on_drop_plus, nullptr);
  lv_obj_set_pos(dp, 196, 30);
  lv_obj_t *speed_lbl = make_label(test, "speed mm/s", &lv_font_montserrat_12,
                                   C_MUTED);
  lv_obj_set_pos(speed_lbl, 12, 76);
  lv_obj_t *sm = make_button(test, "-", 40, 32, on_speed_minus, nullptr);
  lv_obj_set_pos(sm, 100, 70);
  s_cfg_speed = make_label(test, "20", &lv_font_montserrat_16, C_TEXT);
  lv_obj_set_pos(s_cfg_speed, 150, 74);
  lv_obj_t *sp = make_button(test, "+", 40, 32, on_speed_plus, nullptr);
  lv_obj_set_pos(sp, 196, 70);
  lv_obj_t *gap_lbl = make_label(test, "gap ms", &lv_font_montserrat_12, C_MUTED);
  lv_obj_set_pos(gap_lbl, 12, 116);
  lv_obj_t *gm = make_button(test, "-", 40, 32, on_delay_minus, nullptr);
  lv_obj_set_pos(gm, 100, 110);
  s_cfg_delay = make_label(test, "300", &lv_font_montserrat_16, C_TEXT);
  lv_obj_set_pos(s_cfg_delay, 150, 114);
  lv_obj_t *gp = make_button(test, "+", 40, 32, on_delay_plus, nullptr);
  lv_obj_set_pos(gp, 196, 110);
  s_cfg_serp = make_button(test, "SERP ON", 104, 72, on_serp, nullptr);
  lv_obj_set_pos(s_cfg_serp, 246, 30);

  lv_obj_t *actions = make_panel(s, 16, 258, 768, 82);
  lv_obj_t *save = make_button(actions, "SAVE TO SPIFFS", 180, 46, on_save,
                               nullptr);
  lv_obj_set_pos(save, 8, 8);
  lv_obj_t *reset = make_button(actions, "RESET DEFAULTS", 170, 46, on_reset,
                                nullptr);
  lv_obj_set_pos(reset, 198, 8);
  lv_obj_t *clear = make_button(actions, "CLEAR MAPPING", 160, 46, on_clear_map,
                                nullptr);
  lv_obj_set_pos(clear, 378, 8);
  s_cfg_status = make_label(actions, "", &lv_font_montserrat_12, C_MUTED);
  lv_obj_set_pos(s_cfg_status, 560, 22);
}

void build_log(void) {
  lv_obj_t *s = s_screens[UI_SCREEN_LOG];
  lv_obj_t *head = make_panel(s, 16, 8, 768, 54);
  make_label(head, "LOG", &lv_font_montserrat_20, C_TEXT);
  lv_obj_t *clear = make_button(head, "CLEAR", 100, 36, on_log_clear, nullptr);
  lv_obj_align(clear, LV_ALIGN_RIGHT_MID, -12, 0);
  s_log_status = make_label(head, "", &lv_font_montserrat_12, C_MUTED);
  lv_obj_align(s_log_status, LV_ALIGN_LEFT_MID, 120, 0);

  s_log_text = lv_textarea_create(s);
  lv_obj_set_pos(s_log_text, 16, 72);
  lv_obj_set_size(s_log_text, 768, 336);
  lv_textarea_set_text(s_log_text, "");
  lv_obj_set_style_bg_color(s_log_text, C_PANEL, 0);
  lv_obj_set_style_bg_opa(s_log_text, LV_OPA_COVER, 0);
  lv_obj_set_style_text_color(s_log_text, C_TEXT, 0);
  lv_obj_set_style_text_font(s_log_text, &lv_font_montserrat_12, 0);
  lv_obj_set_style_border_color(s_log_text, C_LINE, 0);
  lv_obj_set_style_border_width(s_log_text, 1, 0);
  lv_obj_clear_flag(s_log_text, LV_OBJ_FLAG_CLICKABLE);
}

}  // namespace

void ui_init(const ui_actions_t *actions) {
  if (actions != nullptr) s_actions = *actions;
  std::memset(&s_snap, 0, sizeof(s_snap));

  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, C_BG, 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  build_topbar();
  for (int i = 0; i < UI_SCREEN_COUNT; ++i) s_screens[i] = make_screen();
  build_show();
  build_grid();
  build_nodes();
  build_motor();
  build_config();
  build_log();

  s_screen = UI_SCREEN_SHOW;
  ui_show_screen(UI_SCREEN_SHOW);
}

void ui_show_screen(int index) {
  if (index < 0) index = 0;
  if (index >= UI_SCREEN_COUNT) index = UI_SCREEN_COUNT - 1;
  s_screen = index;
  if (index == UI_SCREEN_LOG) s_log_last_ms = 0;
  for (int i = 0; i < UI_SCREEN_COUNT; ++i) {
    set_hidden(s_screens[i], i != index);
    lv_obj_set_style_bg_color(
        s_tabs[i], (i == index) ? C_ACCENT : C_PANEL2, 0);
  }
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

  /* Top bar link state. */
  lv_label_set_text_fmt(s_link_label, "%s  %s  boot %lu",
                        s_snap.can_running ? "CAN" : "CAN OFF",
                        s_snap.can_mock ? "MOCK" : "HW",
                        (unsigned long)s_snap.boot_count);

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
      lv_obj_set_size(row, 744, 48);
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
  for (int i = 0; i < 4; ++i) {
    lv_obj_set_style_bg_color(s_motor_axis_btn[i],
                              (i == s_selected_axis) ? C_ACCENT : C_PANEL2, 0);
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
