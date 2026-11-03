/*
 * ui_model — the only data that crosses between firmware logic and LVGL.
 *
 * ui/ must compile on the Mac host harness without Arduino or ESP-IDF, so it
 * includes only this header and lvgl.h. Firmware fills a snapshot and wires
 * the action callbacks; the host harness fills a mock snapshot and runs the
 * same screen code.
 */

#ifndef UI_MODEL_H
#define UI_MODEL_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UI_MAX_NODES      30
#define UI_MAX_POSITIONS  120
#define UI_GRID_MAX_COLS  12
#define UI_GRID_MAX_ROWS  12
#define UI_LOG_BYTES      4096
#define UI_MAX_CAN_IDS    256
#define UI_MAX_ANIMATIONS 32
#define UI_ANIM_NAME_LEN  40

typedef struct {
  bool used;
  uint8_t flash_id[8];
  int board_id;       /* -1 unassigned */
  bool online;
  bool status_seen;
  uint8_t state;      /* mc_status state */
  uint8_t fault_flags;
  uint8_t buffer_depth;
  bool version_seen;
  char sw_version[24];
  int assigned_positions;
} ui_node_t;

typedef struct {
  bool assigned;
  int node_index;     /* -1 when unassigned */
  uint8_t axis;
} ui_cell_t;

typedef struct {
  char name[UI_ANIM_NAME_LEN];
  uint32_t size_bytes;
  uint32_t duration_ms;
  bool valid;
} ui_animation_t;

typedef struct {
  uint32_t identifier;
  uint8_t type;
  uint8_t data_len;
  uint8_t data[8];
  uint32_t frame_count;
  uint32_t changed_count;
  uint32_t interval_ms;
  uint32_t interval_min_ms;
  uint32_t interval_max_ms;
} ui_can_entry_t;

typedef struct {
  /* Link and show state */
  bool can_running;
  bool can_mock;
  bool bus_ok;
  bool safe_hold;
  char hold_reason[64];
  char run_state[24];
  char pattern_name[40];
  uint32_t pattern_ms;
  uint32_t elapsed_ms;
  uint32_t uptime_s;

  /* Commissioning summary */
  uint32_t discovered_boards;
  uint32_t assigned_boards;
  uint32_t online_boards;
  uint32_t discovered_steppers;
  uint32_t assigned_positions;
  uint32_t unassigned_positions;
  uint32_t unassigned_steppers;
  uint32_t capacity;
  uint32_t can_rx_frames;
  uint32_t can_tx_frames;
  uint32_t can_errors;
  uint32_t can_tx_failed;
  uint32_t can_rx_dropped;
  bool can_bus_off;

  /* CAN scope: per-identifier rows and counters */
  int can_entry_count;
  ui_can_entry_t can_entries[UI_MAX_CAN_IDS];
  uint32_t can_rate_1s;
  uint32_t can_dropped;
  uint32_t can_unique_ids;

  /* Nodes */
  int node_count;
  ui_node_t nodes[UI_MAX_NODES];
  int selected_node;

  /* Grid */
  int grid_cols;
  int grid_rows;
  ui_cell_t cells[UI_MAX_POSITIONS];

  /* Configuration */
  uint16_t test_drop_mm;
  uint16_t test_speed_mm_s;
  uint16_t test_step_delay_ms;
  bool test_serpentine;
  bool test_run_active;
  int test_run_remaining;
  bool config_dirty;

  /* Animation library and selection */
  int animation_count;
  ui_animation_t animations[UI_MAX_ANIMATIONS];
  int selected_animation;
  bool animation_loading;
  char animation_source[24];
  char animation_status[64];

  /* Node software upgrade */
  bool upgrade_active;
  uint8_t upgrade_progress;  /* 0…100 */
  char upgrade_status[64];

  /* Network and service information (read-only on the panel) */
  bool wifi_present;
  bool wifi_connected;
  char wifi_ssid[33];
  char wifi_ip[16];
  int16_t wifi_rssi;

  /* Diagnostics */
  uint32_t boot_count;
  uint16_t last_watchpoint;
  char firmware_version[24];
  char log_text[UI_LOG_BYTES];
} ui_snapshot_t;

typedef struct {
  void (*node_select)(int index);
  void (*motor_action)(int action, int axis);  /* 0 home, 1 up, 2 down, 3 stop */
  void (*assign_position)(int position, int axis);
  void (*unassign_position)(int position);
  void (*grid_select)(int position);
  void (*config_grid)(int cols, int rows);
  void (*config_test)(int drop_mm, int speed_mm_s, int delay_ms, int serpentine);
  void (*config_save)(void);
  void (*config_reset)(void);
  void (*config_clear_map)(void);
  void (*test_run_start)(void);
  void (*test_run_stop)(void);
  void (*log_clear)(void);
  void (*can_start)(void);
  void (*can_stop)(void);
  void (*can_clear)(void);
  void (*animation_refresh)(void);
  void (*animation_select)(int index);
  void (*animation_load)(void);
  void (*version_query_all)(void);
  void (*upgrade_selected)(void);
  void (*mock_inject)(void);  /* host harness only; may be NULL */
} ui_actions_t;

#ifdef __cplusplus
}
#endif

#endif /* UI_MODEL_H */
