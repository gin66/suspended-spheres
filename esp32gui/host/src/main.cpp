/*
 * Headless LVGL renderer for esp32gui.
 *
 * Renders the productive ui/ui.cpp — the same file the ESP32 firmware builds —
 * to output_<screen>.png at 800x480. There is no harness-local UI code.
 */

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <lvgl.h>
/* LVGL's bundled lodepng.h wraps its C++ API in extern "C". Use the C API. */
#define LODEPNG_NO_COMPILE_CPP
#include <lodepng.h>

#include "ui.h"

namespace {

constexpr int kWidth = 800;
constexpr int kHeight = 480;
constexpr int kBufSize = kWidth * kHeight * 4;
constexpr int kRailX = kWidth - 88;

uint8_t *s_raw;
lv_display_t *s_disp;

void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
  (void)area;
  (void)px_map;
  lv_display_flush_ready(disp);
}

uint32_t argb_to_rgba(uint32_t argb) {
  return ((argb & 0x000000FFu) << 16) | (argb & 0x0000FF00u) |
         ((argb & 0x00FF0000u) >> 16) | (argb & 0xFF000000u);
}

int render_to_png(const char *filename) {
  lv_draw_buf_t *buf = lv_display_get_buf_active(s_disp);
  if (buf == nullptr) {
    std::fprintf(stderr, "error: no active buffer\n");
    return 1;
  }
  const uint32_t *src = reinterpret_cast<const uint32_t *>(buf->data);
  uint8_t *rgba = static_cast<uint8_t *>(std::malloc(kBufSize));
  if (rgba == nullptr) return 1;
  for (int i = 0; i < kWidth * kHeight; ++i) {
    reinterpret_cast<uint32_t *>(rgba)[i] = argb_to_rgba(src[i]);
  }

  unsigned char *png = nullptr;
  size_t png_size = 0;
  const unsigned err =
      lodepng_encode32(&png, &png_size, rgba, kWidth, kHeight);
  std::free(rgba);
  if (err != 0) {
    std::fprintf(stderr, "lodepng error %u: %s\n", err,
                 lodepng_error_text(err));
    return 1;
  }

  FILE *fp = std::fopen(filename, "wb");
  if (fp == nullptr) {
    std::fprintf(stderr, "error: cannot open %s\n", filename);
    std::free(png);
    return 1;
  }
  std::fwrite(png, 1, png_size, fp);
  std::fclose(fp);
  std::free(png);
  std::printf("saved %s (%dx%d)\n", filename, kWidth, kHeight);
  return 0;
}

void clear_background() {
  for (int i = 0; i < kWidth * kHeight; ++i) {
    reinterpret_cast<uint32_t *>(s_raw)[i] = 0xFF0E1116u;
  }
}

void add_node(ui_snapshot_t *s, int index, uint8_t tag, int board, bool online,
              uint8_t state, uint8_t fault) {
  ui_node_t *n = &s->nodes[index];
  n->used = true;
  n->uuid[0] = tag;
  n->uuid[1] = 0x51;
  n->uuid[2] = 0x3A;
  n->uuid[3] = 0x00;
  n->uuid[4] = 0x11;
  n->uuid[5] = (uint8_t)(0x40 + board);
  n->board_id = board;
  n->online = online;
  n->status_seen = true;
  n->state = state;
  n->fault_flags = fault;
  n->buffer_depth = 3;
  n->health_seen = true;
  n->health_age_ms = 200u + (uint32_t)board * 100u;
  n->mcu_temp_c = (int8_t)(41 + board * 3);
  n->otpw_mask = (board == 1) ? 0x02 : 0;
  n->ot_mask = (board == 3) ? 0x01 : 0;
  n->diag_mask = (board == 1) ? 0x02 : ((board == 3) ? 0x01 : 0);
  n->ntc_t0_c = (board == 1) ? 28 : 0;
  n->ntc_bed_c = 0;
  n->health_valid = UI_HEALTH_VALID_MCU | UI_HEALTH_VALID_DRV_STATUS;
  if (board == 1) n->health_valid |= UI_HEALTH_VALID_NTC_T0;
  n->health_seq = (uint8_t)(10 + board);
  n->version_seen = true;
  std::snprintf(n->sw_version, sizeof(n->sw_version), "%s",
                (board == 3) ? "1.1.0-dirty" : "1.1.0");
  n->assigned_positions = 3;
  n->current_build = 1794839000u;
  n->version_flags = 0;
  n->version_age_ms = 300u;
  n->upgrade_color = UI_UPG_COLOR_DIM;
  n->upgrade_state = UI_UPG_SKIPPED;
}

ui_snapshot_t make_upgrade_snapshot(const ui_snapshot_t &base) {
  ui_snapshot_t s = base;
  std::memset(s.nodes, 0, sizeof(s.nodes));
  s.node_count = 30;
  for (int i = 0; i < 30; ++i) {
    add_node(&s, i, (uint8_t)(0x10 + i), i, true, 0, 0);
    ui_node_t *n = &s.nodes[i];
    n->version_seen = (i != 25);
    n->current_build = (i == 25) ? 0u : 1794839000u;
    std::snprintf(n->sw_version, sizeof(n->sw_version), "%s",
                  (i == 25) ? "--" : "1.4.0");
    if (i < 2) {
      n->upgrade_color = UI_UPG_COLOR_GREEN;
      n->upgrade_state = UI_UPG_DONE;
      std::snprintf(n->upgrade_result, sizeof(n->upgrade_result), "build 1794839525");
    } else if (i == 2) {
      n->upgrade_color = UI_UPG_COLOR_YELLOW;
      n->upgrade_state = UI_UPG_WRITING;
      n->upgrade_progress = 46;
    } else if (i == 25) {
      n->upgrade_color = UI_UPG_COLOR_DIM;
      n->upgrade_state = UI_UPG_SKIPPED;
      std::snprintf(n->upgrade_result, sizeof(n->upgrade_result), "version unknown");
    } else {
      n->upgrade_color = UI_UPG_COLOR_RED;
      n->upgrade_state = UI_UPG_QUEUED;
    }
  }
  std::snprintf(s.upgrade_image_version, sizeof(s.upgrade_image_version), "%s",
                "1.4.2");
  s.upgrade_image_build = 1794839525u;
  std::snprintf(s.upgrade_source, sizeof(s.upgrade_source), "%s", "SD");
  s.upgrade_done = 2;
  s.upgrade_total = 30;
  s.upgrade_yellow_board = 2;
  std::snprintf(s.upgrade_status, sizeof(s.upgrade_status), "%s", "write 46%");
  s.upgrade_active = true;
  s.upgrade_progress = 46;
  return s;
}

ui_snapshot_t make_snapshot() {
  ui_snapshot_t s;
  std::memset(&s, 0, sizeof(s));

  s.can_running = true;
  s.can_mock = true;
  s.bus_ok = true;
  s.run_state[0] = '\0';
  std::snprintf(s.run_state, sizeof(s.run_state), "IDLE");
  std::snprintf(s.pattern_name, sizeof(s.pattern_name), "sine_120.hbp");
  s.uptime_s = 123;
  s.elapsed_ms = 0;

  s.discovered_boards = 4;
  s.assigned_boards = 4;
  s.online_boards = 4;
  s.discovered_steppers = 16;
  s.assigned_positions = 12;
  s.unassigned_positions = 108;
  s.unassigned_steppers = 4;
  s.capacity = 120;
  s.can_rx_frames = 1842;
  s.can_tx_frames = 37;
  s.can_errors = 0;

  s.node_count = 4;
  add_node(&s, 0, 0xA1, 0, true, 3, 0);
  add_node(&s, 1, 0xB2, 1, true, 3, 0);
  add_node(&s, 2, 0xC3, 2, true, 2, 0);
  add_node(&s, 3, 0xD4, 3, true, 5, 0x20);
  s.selected_node = 1;

  s.grid_cols = 10;
  s.grid_rows = 12;
  for (int pos = 0; pos < UI_MAX_POSITIONS; ++pos) {
    s.cells[pos].assigned = false;
    s.cells[pos].node_index = -1;
    s.cells[pos].axis = 0;
  }
  for (int i = 0; i < 12; ++i) {
    s.cells[i].assigned = true;
    s.cells[i].node_index = i % 4;
    s.cells[i].axis = (uint8_t)(i % 4);
  }

  s.test_drop_mm = 100;
  s.test_speed_mm_s = 20;
  s.test_step_delay_ms = 300;
  s.test_serpentine = true;
  s.test_run_active = false;
  s.test_run_remaining = 0;
  s.config_dirty = true;

  s.wifi_present = true;
  s.wifi_connected = false;
  std::snprintf(s.wifi_state, sizeof(s.wifi_state), "%s", "AP_SETUP");
  std::snprintf(s.wifi_ssid, sizeof(s.wifi_ssid), "%s",
                "SuspendedSpheres-A1B2");
  std::snprintf(s.wifi_password, sizeof(s.wifi_password), "%s",
                "fixture-password");
  std::snprintf(s.wifi_ip, sizeof(s.wifi_ip), "%s", "192.168.4.1");
  std::snprintf(s.wifi_ap_ssid, sizeof(s.wifi_ap_ssid), "%s",
                "SuspendedSpheres-A1B2");
  std::snprintf(s.wifi_ap_password, sizeof(s.wifi_ap_password), "%s",
                "K7mQ4pR9X2");
  std::snprintf(s.wifi_reason, sizeof(s.wifi_reason), "%s",
                "scan: 6 networks");
  s.wifi_has_credentials = true;
  s.wifi_rssi = 0;
  s.wifi_scan_count = 6;
  const char *scan[] = {"Home-2.4G", "Workshop", "Guest",
                        "Lab-AP", "Neighbour", "Cafe"};
  const int8_t scan_rssi[] = {-42, -55, -61, -68, -74, -81};
  const char *scan_auth[] = {"WPA2", "WPA2", "W2/3", "WPA2", "WPA3", "OPEN"};
  for (int i = 0; i < 6; ++i) {
    std::snprintf(s.wifi_scan_ssid[i], sizeof(s.wifi_scan_ssid[i]), "%s",
                  scan[i]);
    s.wifi_scan_rssi[i] = scan_rssi[i];
    std::snprintf(s.wifi_scan_auth[i], sizeof(s.wifi_scan_auth[i]), "%s",
                  scan_auth[i]);
  }
  s.service_on = true;
  std::snprintf(s.github_status, sizeof(s.github_status), "%s",
                "same 0.1.0");

  std::snprintf(s.upgrade_image_version, sizeof(s.upgrade_image_version),
                "%s", "1.4.2");
  s.upgrade_image_build = 1794839525u;
  std::snprintf(s.upgrade_source, sizeof(s.upgrade_source), "%s", "SD");
  s.upgrade_active = false;
  s.upgrade_progress = 0;
  std::snprintf(s.upgrade_status, sizeof(s.upgrade_status), "%s",
                "idle; QUERY ALL asks every board for its version");

  s.animation_count = 4;
  const struct {
    const char *name;
    uint32_t size;
    uint32_t duration_ms;
    bool valid;
  } anim_rows[4] = {
      {"sine_120.hbp", 1u << 20, 120000, true},
      {"rain_2d.hbp", 3u << 20, 240000, true},
      {"spiral.hbp", 2u << 20, 180000, true},
      {"wave_test.hbp", 64u << 10, 30000, false},
  };
  for (int i = 0; i < s.animation_count; ++i) {
    ui_animation_t *a = &s.animations[i];
    std::snprintf(a->name, sizeof(a->name), "%s", anim_rows[i].name);
    a->size_bytes = anim_rows[i].size;
    a->duration_ms = anim_rows[i].duration_ms;
    a->valid = anim_rows[i].valid;
    a->in_playlist = (i < 2);
  }
  s.selected_animation = 0;
  std::snprintf(s.animation_source, sizeof(s.animation_source), "%s", "SD");
  std::snprintf(s.animation_status, sizeof(s.animation_status), "%s",
                "SD library ready");

  s.can_tx_failed = 2;
  s.can_rx_dropped = 0;
  s.can_bus_off = false;
  s.can_rate_1s = 350;
  s.can_dropped = 0;
  s.can_unique_ids = 7;
  s.can_entry_count = 7;
  const struct {
    uint32_t id;
    uint8_t type;
    uint32_t count;
    uint32_t interval;
    uint8_t len;
    uint8_t data[8];
  } can_rows[7] = {
      {0x000, 0, 1200, 10, 8, {0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
      {0x010, 0, 1200, 10, 8, {0x00, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
      {0x002, 2, 50, 20, 8, {0x24, 0x1A, 0x10, 0x27, 0x10, 0x27, 0x04, 0x5A}},
      {0x033, 3, 30, 100, 8, {0x03, 0x00, 0x00, 0x00, 0x00, 0x03, 0x05, 0x5A}},
      {0x01E, 14, 1, 1000, 8, {0x2A, 0x00, 0x00, 0x00, 0x00, 0x09, 0x01, 0x00}},
      {0x00B, 11, 12, 5000, 8, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
      {0x7FC, 12, 12, 5000, 8, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
  };
  for (int i = 0; i < s.can_entry_count; ++i) {
    ui_can_entry_t *e = &s.can_entries[i];
    e->identifier = can_rows[i].id;
    e->type = can_rows[i].type;
    e->frame_count = can_rows[i].count;
    e->changed_count = can_rows[i].count;
    e->interval_ms = can_rows[i].interval;
    e->interval_min_ms = can_rows[i].interval;
    e->interval_max_ms = can_rows[i].interval;
    e->data_len = can_rows[i].len;
    std::memcpy(e->data, can_rows[i].data, can_rows[i].len);
  }

  s.sd_present = true;
  s.sd_fly_images = 1;
  s.sd_panel_images = 1;
  s.sd_free_mb = 14800;
  s.display_timeout_s = 60;
  s.boot_count = 7;
  s.last_watchpoint = 3;
  std::snprintf(s.firmware_version, sizeof(s.firmware_version), "%s",
                "0.1.0-stage1");

  std::snprintf(s.log_text, sizeof(s.log_text),
                "[12.004][I][boot] esp32gui 0.1.0-stage1 starting\n"
                "[12.006][I][boot] boot #7 reset=1 last_watchpoint=3\n"
                "[12.180][I][cfg] loaded 10x12, 12 positions assigned\n"
                "[12.201][I][can] TWAI mock started\n"
                "[12.240][I][commission] ready; ASK every 5000 ms\n"
                "[12.241][I][commission] ASSIGN board 0 flash a1513a0011223340\n"
                "[12.243][I][commission] ASSIGN board 1 flash b2513a0011223341\n"
                "[12.245][I][commission] ASSIGN board 2 flash c3513a0011223342\n"
                "[12.247][I][commission] ASSIGN board 3 flash d4513a0011223343\n"
                "[15.002][W][commission] fault frame id=0x0337 board=51\n"
                "[18.441][I][motor] position 3 <- board 0 axis 3\n"
                "[22.100][I][cfg] saved: 10x12, 12 positions, test 100mm @ "
                "20mm/s, 300ms\n"
                "[24.500][W][health] board 1 OTPW mask 0x00 -> 0x02\n");
  return s;
}


/* ── layout audit ────────────────────────────────────────────────────────
 * The panel has no CSS overflow pass: a control that is 4 px too far right
 * simply draws over its neighbour or the rail.  After every render, walk the
 * live LVGL tree and fail on any two unrelated visible widgets whose boxes
 * intersect, and on any content that crosses the rail or the panel edge. */
struct AuditItem {
  lv_obj_t *obj;
  lv_area_t area;
  bool overlay;
};

bool hidden_in_chain(lv_obj_t *obj) {
  for (lv_obj_t *p = obj; p != nullptr; p = lv_obj_get_parent(p)) {
    if (lv_obj_has_flag(p, LV_OBJ_FLAG_HIDDEN)) return true;
  }
  return false;
}

/* Rows below a scroll viewport are still children with coordinates outside
 * the parent.  LVGL clips them, so they are not layout violations. */
bool clipped_by_scroller(lv_obj_t *obj) {
  lv_area_t oa;
  lv_obj_get_coords(obj, &oa);
  for (lv_obj_t *p = lv_obj_get_parent(obj); p != nullptr;
       p = lv_obj_get_parent(p)) {
    if (lv_obj_get_scroll_dir(p) == LV_DIR_NONE) continue;
    lv_area_t pa;
    lv_obj_get_coords(p, &pa);
    if (oa.x1 < pa.x1 || oa.y1 < pa.y1 || oa.x2 > pa.x2 || oa.y2 > pa.y2)
      return true;
  }
  return false;
}

bool is_ancestor(lv_obj_t *maybe_parent, lv_obj_t *child) {
  for (lv_obj_t *p = lv_obj_get_parent(child); p != nullptr;
       p = lv_obj_get_parent(p)) {
    if (p == maybe_parent) return true;
  }
  return false;
}

void collect_visible(lv_obj_t *obj, bool overlay, std::vector<AuditItem> &out) {
  if (obj == nullptr || hidden_in_chain(obj) || clipped_by_scroller(obj)) return;
  lv_area_t area;
  lv_obj_get_coords(obj, &area);
  const int w = lv_area_get_width(&area);
  const int h = lv_area_get_height(&area);
  const bool is_root = (obj == lv_screen_active());
  const bool full_screen =
      lv_obj_get_parent(obj) == lv_screen_active() && area.x1 == 0 &&
      area.y1 == 0 && w == kWidth && h == kHeight;
  const bool in_overlay = overlay || full_screen;
  if (!is_root && w > 0 && h > 0)
    out.push_back({obj, area, in_overlay});
  const uint32_t n = lv_obj_get_child_count(obj);
  for (uint32_t i = 0; i < n; ++i)
    collect_visible(lv_obj_get_child(obj, i), in_overlay, out);
}

const char *obj_kind(lv_obj_t *obj) {
  if (lv_obj_check_type(obj, &lv_label_class)) return "label";
  if (lv_obj_check_type(obj, &lv_button_class)) return "button";
  if (lv_obj_check_type(obj, &lv_textarea_class)) return "textarea";
  if (lv_obj_check_type(obj, &lv_bar_class)) return "bar";
  if (lv_obj_check_type(obj, &lv_obj_class)) return "object";
  return "container";
}

int audit_layout(const char *screen) {
  std::vector<AuditItem> items;
  collect_visible(lv_screen_active(), false, items);
  int violations = 0;
  for (size_t i = 0; i < items.size(); ++i) {
    const AuditItem &a = items[i];
    const int aw = lv_area_get_width(&a.area);
    const int ah = lv_area_get_height(&a.area);
    if (a.area.x1 < 0 || a.area.y1 < 0 || a.area.x2 >= kWidth ||
        a.area.y2 >= kHeight) {
      std::printf("LAYOUT %s: %s outside display (%d,%d %dx%d)\n", screen,
                  obj_kind(a.obj), (int)a.area.x1, (int)a.area.y1, aw, ah);
      ++violations;
    }
    if (!a.overlay && a.area.x1 < kRailX && a.area.x2 >= kRailX) {
      std::printf("LAYOUT %s: %s crosses into rail (%d..%d)\n", screen,
                  obj_kind(a.obj), (int)a.area.x1, (int)a.area.x2);
      ++violations;
    }
    for (size_t j = i + 1; j < items.size(); ++j) {
      const AuditItem &b = items[j];
      if (a.overlay != b.overlay) continue;
      if (is_ancestor(a.obj, b.obj) || is_ancestor(b.obj, a.obj)) continue;
      const int ox = std::min(a.area.x2, b.area.x2) - std::max(a.area.x1, b.area.x1);
      const int oy = std::min(a.area.y2, b.area.y2) - std::max(a.area.y1, b.area.y1);
      if (ox > 1 && oy > 1) {
        std::printf(
            "LAYOUT %s: %s (%d,%d %dx%d) overlaps %s (%d,%d %dx%d)\n", screen,
            obj_kind(a.obj), (int)a.area.x1, (int)a.area.y1,
            lv_area_get_width(&a.area), lv_area_get_height(&a.area),
            obj_kind(b.obj), (int)b.area.x1, (int)b.area.y1,
            lv_area_get_width(&b.area), lv_area_get_height(&b.area));
        ++violations;
      }
    }
  }
  if (violations == 0) std::printf("layout %s: ok (%zu widgets)\n", screen,
                                   items.size());
  return violations;
}

}  // namespace

int main(int argc, char **argv) {
  const char *output_dir = (argc > 1) ? argv[1] : ".";

  lv_init();
  s_raw = static_cast<uint8_t *>(std::malloc(kBufSize));
  if (s_raw == nullptr) {
    std::fprintf(stderr, "malloc failed\n");
    return 1;
  }

  lv_draw_buf_t draw_buf;
  if (lv_draw_buf_init(&draw_buf, kWidth, kHeight, LV_COLOR_FORMAT_ARGB8888,
                       LV_STRIDE_AUTO, s_raw, kBufSize) != LV_RESULT_OK) {
    std::fprintf(stderr, "lv_draw_buf_init failed\n");
    return 1;
  }
  draw_buf.unaligned_data = s_raw;

  s_disp = lv_display_create(kWidth, kHeight);
  lv_display_set_flush_cb(s_disp, flush_cb);
  lv_display_set_draw_buffers(s_disp, &draw_buf, nullptr);
  lv_display_set_render_mode(s_disp, LV_DISPLAY_RENDER_MODE_FULL);

  ui_actions_t actions;
  std::memset(&actions, 0, sizeof(actions));
  ui_init(&actions);

  ui_snapshot_t snap = make_snapshot();
  int audit_failures = 0;

  static const char *names[UI_SCREEN_COUNT] = {
      "show", "animations", "grid", "nodes", "motor", "config",
      "log", "can", "upgrade", "settings", "wifi", "setup", "service"};
  ui_snapshot_t upgrade_snap = make_upgrade_snapshot(snap);
  for (int i = 0; i < UI_SCREEN_COUNT; ++i) {
    if (i == UI_SCREEN_ANIMATIONS) continue;  /* folded into SHOW */
    ui_show_screen(i);
    ui_update(i == UI_SCREEN_UPGRADE ? &upgrade_snap : &snap);
    clear_background();
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(s_disp);

    char filename[512];
    std::snprintf(filename, sizeof(filename), "%s/output_%s.png", output_dir,
                  names[i]);
    render_to_png(filename);
    if (audit_layout(names[i]) != 0) audit_failures++;
  }

  ui_snapshot_t wifi_snap = snap;
  std::snprintf(wifi_snap.wifi_state, sizeof(wifi_snap.wifi_state), "%s",
                "CONNECTED");
  std::snprintf(wifi_snap.wifi_ssid, sizeof(wifi_snap.wifi_ssid), "%s",
                "KiNet");
  std::snprintf(wifi_snap.wifi_ip, sizeof(wifi_snap.wifi_ip), "%s",
                "192.168.1.160");
  wifi_snap.wifi_connected = true;
  wifi_snap.wifi_rssi = -52;
  wifi_snap.service_on = false;
  wifi_snap.wifi_reason[0] = '\0';
  std::snprintf(wifi_snap.github_status, sizeof(wifi_snap.github_status), "%s",
                "same 0.1.0");
  ui_show_screen(UI_SCREEN_WIFI);
  ui_update(&wifi_snap);
  clear_background();
  lv_obj_invalidate(lv_screen_active());
  lv_refr_now(s_disp);
  char wifi_connected_png[512];
  std::snprintf(wifi_connected_png, sizeof(wifi_connected_png),
                "%s/output_wifi_connected.png", output_dir);
  render_to_png(wifi_connected_png);
  if (audit_layout("wifi_connected") != 0) audit_failures++;

  ui_snapshot_t wifi_off = snap;
  std::snprintf(wifi_off.wifi_state, sizeof(wifi_off.wifi_state), "%s", "OFF");
  wifi_off.wifi_connected = false;
  wifi_off.wifi_ip[0] = '\0';
  wifi_off.service_on = false;
  wifi_off.wifi_scan_count = 6;
  std::snprintf(wifi_off.wifi_reason, sizeof(wifi_off.wifi_reason), "%s",
                "WiFi is off; nothing starts until you choose a mode.");
  ui_show_screen(UI_SCREEN_WIFI);
  ui_update(&wifi_off);
  clear_background();
  lv_obj_invalidate(lv_screen_active());
  lv_refr_now(s_disp);
  char wifi_off_png[512];
  std::snprintf(wifi_off_png, sizeof(wifi_off_png),
                "%s/output_wifi_off.png", output_dir);
  render_to_png(wifi_off_png);
  if (audit_layout("wifi_off") != 0) audit_failures++;

  ui_show_screen(UI_SCREEN_WIFI);
  ui_update(&wifi_snap);
  ui_debug_open_wifi_modal(0);
  clear_background();
  lv_obj_invalidate(lv_screen_active());
  lv_refr_now(s_disp);
  char keyboard_png[512];
  std::snprintf(keyboard_png, sizeof(keyboard_png),
                "%s/output_wifi_keyboard.png", output_dir);
  render_to_png(keyboard_png);
  if (audit_layout("wifi_keyboard") != 0) audit_failures++;

  ui_show_screen(UI_SCREEN_SHOW);
  ui_update(&snap);
  snap.show_playing = true;
  snap.show_cycle = true;
  ui_update(&snap);
  clear_background();
  lv_obj_invalidate(lv_screen_active());
  lv_refr_now(s_disp);
  char playing_png[512];
  std::snprintf(playing_png, sizeof(playing_png),
                "%s/output_show_playing.png", output_dir);
  render_to_png(playing_png);
  if (audit_layout("show_playing") != 0) audit_failures++;

  ui_snapshot_t updating = snap;
  updating.panel_update_active = true;
  updating.panel_update_progress = 42;
  std::snprintf(updating.panel_update_stage,
                sizeof(updating.panel_update_stage), "%s", "panel image");
  ui_show_screen(UI_SCREEN_UPGRADE);
  ui_update(&updating);
  clear_background();
  lv_obj_invalidate(lv_screen_active());
  lv_refr_now(s_disp);
  char updating_png[512];
  std::snprintf(updating_png, sizeof(updating_png),
                "%s/output_updating.png", output_dir);
  render_to_png(updating_png);
  if (audit_layout("updating") != 0) audit_failures++;

  ui_show_screen(UI_SCREEN_WIFI);
  ui_update(&snap);
  ui_debug_open_wifi_modal(2);
  clear_background();
  lv_obj_invalidate(lv_screen_active());
  lv_refr_now(s_disp);
  char ap_key_png[512];
  std::snprintf(ap_key_png, sizeof(ap_key_png),
                "%s/output_wifi_ap_key.png", output_dir);
  render_to_png(ap_key_png);
  if (audit_layout("wifi_ap_key") != 0) audit_failures++;

  if (audit_failures != 0) {
    std::printf("layout audit: %d violation(s)\n", audit_failures);
    return 1;
  }
  std::printf("layout audit: clean\n");
  return 0;
}
