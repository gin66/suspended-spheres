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
#define UI_MAX_WIFI_SCAN  12
#define UI_ANIM_NAME_LEN  40

/* Mirrors MC_HEALTH_VALID_* in motion_core.h. The UI model stays free of
 * the shared core header so the host harness compiles without it. */
#define UI_HEALTH_VALID_MCU        0x01u
#define UI_HEALTH_VALID_NTC_T0     0x02u
#define UI_HEALTH_VALID_NTC_BED    0x04u
#define UI_HEALTH_VALID_DRV_STATUS 0x08u

/* Mirrors fleet_core fl_color_t and fl_state_t without including the core. */
#define UI_UPG_COLOR_RED     0u
#define UI_UPG_COLOR_YELLOW  1u
#define UI_UPG_COLOR_GREEN   2u
#define UI_UPG_COLOR_DIM     3u
#define UI_UPG_COLOR_FAILED  4u

#define UI_UPG_IDLE       0u
#define UI_UPG_QUEUED     1u
#define UI_UPG_ENTERING   2u
#define UI_UPG_CONNECTING 3u
#define UI_UPG_WRITING    4u
#define UI_UPG_VERIFYING  5u
#define UI_UPG_REBOOTING  6u
#define UI_UPG_DONE       7u
#define UI_UPG_FAILED     8u
#define UI_UPG_SKIPPED    9u

typedef struct {
  bool used;
  uint8_t uuid[6];
  int board_id;       /* -1 unassigned */
  bool online;
  bool status_seen;
  uint8_t state;      /* mc_status state */
  uint8_t fault_flags;
  uint8_t buffer_depth;
  bool health_seen;
  uint32_t health_age_ms;
  int8_t mcu_temp_c;
  uint8_t otpw_mask;
  uint8_t ot_mask;
  uint8_t diag_mask;
  int8_t ntc_t0_c;
  int8_t ntc_bed_c;
  uint8_t health_valid;
  uint8_t health_seq;
  bool version_seen;
  char sw_version[24];
  uint8_t version_flags;   /* bit 0 dirty */
  uint32_t current_build;  /* 0 unknown */
  uint32_t version_age_ms;
  int assigned_positions;
  uint8_t upgrade_state;   /* fleet_core fl_state_t */
  uint8_t upgrade_color;   /* fleet_core fl_color_t */
  uint8_t upgrade_progress;
  char upgrade_result[28];
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
  bool in_playlist;
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
  char upgrade_image_version[24];
  uint32_t upgrade_image_build;
  char upgrade_source[8];
  int upgrade_done;
  int upgrade_total;
  int upgrade_yellow_board;   /* -1 when none */

  /* Network and service information (read-only on the panel) */
  bool wifi_present;
  bool wifi_connected;
  char wifi_ssid[33];
  char wifi_password[65];
  char wifi_ip[16];
  int16_t wifi_rssi;
  char wifi_state[16];
  char wifi_reason[64];
  char wifi_ap_ssid[32];
  char wifi_ap_password[16];
  bool wifi_has_credentials;
  bool wifi_scanning;
  int wifi_scan_count;
  char wifi_scan_ssid[UI_MAX_WIFI_SCAN][33];
  bool service_on;
  char github_status[64];
  bool github_update_available;

  /* Display */
  uint16_t display_timeout_s;

  /* Diagnostics */
  uint32_t boot_count;
  uint16_t last_watchpoint;
  char firmware_version[24];
  char panel_status[64];
  bool panel_image_ready;
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
  void (*animation_add)(int index);
  void (*animation_remove)(int index);
  void (*animation_load)(void);
  void (*animation_pause)(void);
  void (*animation_stop)(void);
  void (*animation_stop_immediate)(void);
  void (*version_query_all)(void);
  void (*upgrade_selected)(void);
  void (*upgrade_all_outdated)(void);
  void (*upgrade_stop)(void);
  void (*panel_update)(void);
  void (*wifi_setup)(void);
  void (*wifi_ap)(void);
  void (*wifi_off)(void);
  void (*wifi_retry)(void);
  void (*wifi_new_ap_key)(void);
  void (*wifi_set_ap_key)(const char *key);
  void (*wifi_forget)(void);
  void (*wifi_scan)(void);
  void (*service_toggle)(void);
  void (*github_check)(void);
  void (*github_install)(void);
  void (*wifi_connect)(const char *ssid, const char *password);
  void (*wifi_set_admin)(const char *password);
  void (*display_timeout_cycle)(void);
  void (*display_timeout_set)(uint16_t seconds);
  void (*mock_inject)(void);  /* host harness only; may be NULL */
} ui_actions_t;

#ifdef __cplusplus
}
#endif

#endif /* UI_MODEL_H */
