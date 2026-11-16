/*
 * config_store — persistent commissioning configuration on SPIFFS.
 *
 * Version 2 keys the map by the 6-byte Katapult/Klipper UUID, not the 8-byte
 * QSPI flash id (firmware_release_whitepaper.md §4.3).  A version-1 file is
 * loaded as "version unknown": the grid and test parameters survive, but no
 * UUID is invented for an old row.  The operator re-assigns those positions.
 */

#ifndef CONFIG_STORE_H
#define CONFIG_STORE_H

#include <stdbool.h>
#include <stdint.h>

#include "panel_core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SS_MAX_BOARDS       30
#define SS_AXES_PER_BOARD   4
#define SS_UUID_LEN         6
#define SS_MAX_POSITIONS    120
#define SS_GRID_MAX_COLS    12
#define SS_GRID_MAX_ROWS    12

#define SS_CONFIG_FILE      "/config.bin"
#define SS_CONFIG_TMP_FILE  "/config.tmp"
#define SS_CONFIG_MAGIC     PC_CONFIG_MAGIC
#define SS_CONFIG_VERSION   PC_CONFIG_VERSION

typedef pc_position_t ss_position_map_t;
typedef pc_board_t ss_board_map_t;
typedef pc_config_v2_t ss_config_t;

/* Mount SPIFFS, load /config.bin, or write defaults when absent/invalid. */
bool config_store_init(void);

const ss_config_t *config_store_get(void);

bool config_store_set_grid(uint8_t cols, uint8_t rows);
bool config_store_set_test_params(uint16_t drop_mm, uint16_t speed_mm_s,
                                  uint16_t step_delay_ms, bool serpentine);
bool config_store_set_brightness(uint8_t percent);

/* Assign one position, removing the same (uuid, axis) and the position's
 * previous owner so the map stays one-to-one. */
bool config_store_assign_position(uint8_t position, const uint8_t uuid[6],
                                  uint8_t axis);
bool config_store_unassign_position(uint8_t position);
void config_store_clear_positions(void);

int config_store_find_position(const uint8_t uuid[6], uint8_t axis);
int config_store_assigned_count(void);
int config_store_capacity(void);

/* (uuid -> board_id) persistence. get returns -1 when unknown. */
int config_store_get_board_id(const uint8_t uuid[6]);
bool config_store_set_board_id(const uint8_t uuid[6], int board_id);
/* The last confirmed version answer, keyed by UUID. */
bool config_store_set_board_version(const uint8_t uuid[6], int board_id,
                                    uint32_t build, uint8_t flags,
                                    const char *result);
const ss_board_map_t *config_store_get_board(const uint8_t uuid[6]);
int config_store_free_board_id(void);
void config_store_clear_boards(void);

bool config_store_dirty(void);
bool config_store_save(void);

/* Restore compiled defaults and save immediately. */
bool config_store_reset_defaults(void);

#ifdef __cplusplus
}
#endif

#endif /* CONFIG_STORE_H */
