/*
 * config_store — persistent commissioning configuration on SPIFFS.
 *
 * The panel owns the host map from (64-bit flash ID, axis 0..3) to a grid
 * position 0..N-1. The Fly-Micro4 never learns a grid position
 * (Fly_Micro4_addressing.md §2). This store is the only copy that survives a
 * power cycle; the HBP2 playback file remains on SD and is read-only.
 */

#ifndef CONFIG_STORE_H
#define CONFIG_STORE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SS_MAX_BOARDS       30
#define SS_AXES_PER_BOARD   4
#define SS_FLASH_ID_LEN     8
#define SS_MAX_POSITIONS    120
#define SS_GRID_MAX_COLS    12
#define SS_GRID_MAX_ROWS    12

#define SS_CONFIG_FILE      "/config.bin"
#define SS_CONFIG_TMP_FILE  "/config.tmp"
#define SS_CONFIG_MAGIC     0x53534346u  /* "SSCF" */
#define SS_CONFIG_VERSION   1u

/* Row 0 is the back row; column 0 is the left column. */
#pragma pack(push, 1)
typedef struct {
  bool assigned;
  uint8_t flash_id[SS_FLASH_ID_LEN];
  uint8_t axis;  /* 0..3, local Drive0..Drive3 */
} ss_position_map_t;

/* The master-assigned board ID is RAM on the Fly-Micro4, so the panel keeps
 * the (flash_id -> board_id) map to give a returning board the same ID. */
typedef struct {
  uint8_t flash_id[SS_FLASH_ID_LEN];
  int8_t board_id;  /* -1 when unused */
} ss_board_map_t;

typedef struct {
  uint32_t magic;
  uint16_t version;
  uint8_t grid_cols;
  uint8_t grid_rows;
  uint16_t test_drop_mm;
  uint16_t test_speed_mm_s;
  uint16_t test_step_delay_ms;
  uint8_t test_serpentine;  /* 1 = left->right then right->left per row */
  uint8_t brightness;       /* 10..100 % */
  ss_position_map_t positions[SS_MAX_POSITIONS];
  ss_board_map_t boards[SS_MAX_BOARDS];
  uint32_t crc;
} ss_config_t;
#pragma pack(pop)

/* Mount SPIFFS, load /config.bin, or write defaults when absent/invalid. */
bool config_store_init(void);

const ss_config_t *config_store_get(void);

bool config_store_set_grid(uint8_t cols, uint8_t rows);
bool config_store_set_test_params(uint16_t drop_mm, uint16_t speed_mm_s,
                                  uint16_t step_delay_ms, bool serpentine);
bool config_store_set_brightness(uint8_t percent);

/* Assign one position, removing the same (flash_id, axis) and the position's
 * previous owner so the map stays one-to-one. */
bool config_store_assign_position(uint8_t position, const uint8_t flash_id[8],
                                  uint8_t axis);
bool config_store_unassign_position(uint8_t position);
void config_store_clear_positions(void);

int config_store_find_position(const uint8_t flash_id[8], uint8_t axis);
int config_store_assigned_count(void);
int config_store_capacity(void);

/* (flash_id -> board_id) persistence. get returns -1 when unknown. */
int config_store_get_board_id(const uint8_t flash_id[8]);
bool config_store_set_board_id(const uint8_t flash_id[8], int board_id);
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
