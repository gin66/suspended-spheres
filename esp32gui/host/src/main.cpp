/*
 * Headless LVGL renderer for esp32gui.
 *
 * Renders the productive ui/ui.cpp — the same file the ESP32 firmware builds —
 * to output_<screen>.png at 800x480. There is no harness-local UI code.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <lvgl.h>
/* LVGL's bundled lodepng.h wraps its C++ API in extern "C". Use the C API. */
#define LODEPNG_NO_COMPILE_CPP
#include <lodepng.h>

#include "ui.h"

namespace {

constexpr int kWidth = 800;
constexpr int kHeight = 480;
constexpr int kBufSize = kWidth * kHeight * 4;

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
  n->flash_id[0] = tag;
  n->flash_id[1] = 0x51;
  n->flash_id[2] = 0x3A;
  n->flash_id[3] = 0x00;
  n->flash_id[4] = 0x11;
  n->flash_id[5] = 0x22;
  n->flash_id[6] = 0x33;
  n->flash_id[7] = (uint8_t)(0x40 + board);
  n->board_id = board;
  n->online = online;
  n->status_seen = true;
  n->state = state;
  n->fault_flags = fault;
  n->buffer_depth = 3;
  n->version_seen = true;
  std::snprintf(n->sw_version, sizeof(n->sw_version), "%s",
                (board == 3) ? "1.1.0-dirty" : "1.1.0");
  n->assigned_positions = 3;
}

ui_snapshot_t make_snapshot() {
  ui_snapshot_t s;
  std::memset(&s, 0, sizeof(s));

  s.can_running = true;
  s.can_mock = true;
  s.bus_ok = true;
  s.run_state[0] = '\0';
  std::snprintf(s.run_state, sizeof(s.run_state), "IDLE");
  std::snprintf(s.pattern_name, sizeof(s.pattern_name), "COMMISSIONING");
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

  s.upgrade_active = false;
  s.upgrade_progress = 0;
  std::snprintf(s.upgrade_status, sizeof(s.upgrade_status), "%s",
                "idle; QUERY ALL asks every board for its version");

  s.can_rate_1s = 350;
  s.can_dropped = 0;
  s.can_unique_ids = 6;
  s.can_entry_count = 6;
  const struct {
    uint32_t id;
    uint8_t type;
    uint8_t field;
    uint32_t count;
    uint32_t interval;
    uint8_t len;
    uint8_t data[8];
  } can_rows[6] = {
      {0x000, 0, 0, 1200, 10, 8, {0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
      {0x010, 0, 1, 1200, 10, 8, {0x00, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
      {0x002, 2, 0, 50, 20, 8, {0x24, 0x1A, 0x10, 0x27, 0x10, 0x27, 0x04, 0x5A}},
      {0x033, 3, 3, 30, 100, 8, {0x03, 0x00, 0x00, 0x00, 0x00, 0x03, 0x05, 0x5A}},
      {0x00B, 11, 0, 12, 5000, 8, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
      {0x7FC, 12, 127, 12, 5000, 8, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
  };
  for (int i = 0; i < s.can_entry_count; ++i) {
    ui_can_entry_t *e = &s.can_entries[i];
    e->identifier = can_rows[i].id;
    e->type = can_rows[i].type;
    e->field = can_rows[i].field;
    e->frame_count = can_rows[i].count;
    e->changed_count = can_rows[i].count;
    e->interval_ms = can_rows[i].interval;
    e->interval_min_ms = can_rows[i].interval;
    e->interval_max_ms = can_rows[i].interval;
    e->data_len = can_rows[i].len;
    std::memcpy(e->data, can_rows[i].data, can_rows[i].len);
  }

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
                "20mm/s, 300ms\n");
  return s;
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

  static const char *names[UI_SCREEN_COUNT] = {
      "show", "grid", "nodes", "motor", "config",
      "log",  "can",  "upgrade", "settings"};
  for (int i = 0; i < UI_SCREEN_COUNT; ++i) {
    ui_show_screen(i);
    ui_update(&snap);
    clear_background();
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(s_disp);

    char filename[512];
    std::snprintf(filename, sizeof(filename), "%s/output_%s.png", output_dir,
                  names[i]);
    render_to_png(filename);
  }

  return 0;
}
