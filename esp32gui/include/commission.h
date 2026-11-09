/*
 * commission — discovery, board-ID assignment, grid mapping and the
 * commissioning test run.
 *
 * Identity rules are in doc/Fly_Micro4_addressing.md. The master sends ASK
 * (type 11) every 5 s; unidentified boards answer DISC (type 8) with their
 * 64-bit QSPI flash ID; the master answers ASSIGN (type 10) with the board ID
 * in the 7-bit field and the flash ID in the payload. The board never learns
 * a grid position.
 *
 * The home/up/down controls use type 5 (HOME) and type 4 (COMMAND). The
 * payload layout is the commissioning extension documented in
 * doc/esp32gui_whitepaper.md §9 and must stay in sync with the Fly-Micro4
 * firmware once that firmware implements it.
 *
 * Version query/response uses types 12/13 (motion_core.h). The master sends
 * VERSION_REQ to field 127; each board answers VERSION_RSP with its semantic
 * version and build id.
 */

#ifndef COMMISSION_H
#define COMMISSION_H

#include <stdbool.h>
#include <stdint.h>

#include "can_transport.h"
#include "config_store.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  SS_MOTOR_HOME = 0,
  SS_MOTOR_UP,
  SS_MOTOR_DOWN,
  SS_MOTOR_STOP
} ss_motor_action_t;

typedef struct {
  bool used;
  uint8_t flash_id[SS_FLASH_ID_LEN];
  int board_id;  /* -1 until ASSIGN */
  bool advertised;
  bool status_seen;
  uint8_t state;
  uint8_t fault_flags;
  uint8_t following_error[4];
  uint8_t buffer_depth;
  uint8_t last_seq;
  uint32_t last_seen_ms;
  uint32_t last_status_ms;
  bool health_seen;
  uint32_t last_health_ms;
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
  uint32_t last_version_ms;
  int assigned_positions;  /* count of grid positions for this board */
} ss_node_t;

typedef struct {
  uint32_t discovered_boards;
  uint32_t assigned_boards;
  uint32_t discovered_steppers;
  uint32_t assigned_positions;
  uint32_t unassigned_steppers;
  uint32_t unassigned_positions;
  uint32_t capacity;
  uint32_t online_boards;
  uint32_t ask_count;
  uint32_t disc_count;
  uint32_t assign_count;
  uint32_t tx_errors;
} ss_commission_stats_t;

/* Reset the registry and arm the first ASK. Does not start the transport. */
void commission_init(void);

/* CAN RX callback body: push to the monitor and update discovery/status. */
void commission_on_frame(const ss_can_frame_t *frame, void *ctx);

/* Main-loop service: send the 5 s ASK, persist newly assigned board IDs,
 * advance the test run. Call at >= 10 Hz. */
void commission_poll(void);

/* Re-copy the persisted (flash_id -> board_id) map after a config save/reset
 * or a discovered-board edit. Call from the main loop, not the CAN callback. */
void commission_reload_config(void);

int commission_node_count(void);
bool commission_get_node(int index, ss_node_t *out);
const ss_commission_stats_t *commission_stats(void);
int commission_find_node(const uint8_t flash_id[8]);

/* Selection used by the Grid/Nodes/Motor screens. */
int commission_selected_index(void);
void commission_select(int index);

/* Commission one motor. UP/DOWN jog `distance_mm`; HOME uses the HOME frame;
 * STOP cancels motion on that axis. Returns false when the board is unknown,
 * unassigned, or the transport rejected the frame. */
bool commission_test_motor(const uint8_t flash_id[8], uint8_t axis,
                           ss_motor_action_t action, uint16_t distance_mm,
                           uint16_t speed_mm_s);

/* Operational stop for every board (type 4, field 127). Not the hardwired
 * E-stop in FR-MOT-011. */
/* Ask every board for its software version (type 12, field 127). Results
 * arrive as type-13 responses and are kept in ss_node_t.sw_version. */
bool commission_query_versions(void);

bool commission_stop_all(void);

/* Commissioning cascade: requires every grid position to be assigned. Rows
 * run back to front; within a row the order is left->right, then right->left
 * on the next row. Each assigned sphere drops test_drop_mm slowly, then the
 * next one starts after test_step_delay_ms. This is not HBP2 playback. */
bool commission_test_run_start(void);
void commission_test_run_stop(void);
void commission_test_run_poll(void);
bool commission_test_run_active(void);
int commission_test_run_remaining(void);

#ifdef __cplusplus
}
#endif

#endif /* COMMISSION_H */
