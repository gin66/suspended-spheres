#ifndef PANEL_CORE_H
#define PANEL_CORE_H

/*
 * panel_core — host-tested panel policy that has no LVGL, no TWAI, no
 * SPIFFS and no ESP-IDF.  The firmware builds its config store, version
 * query and upgrade selection on these functions.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "release_core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PC_UUID_LEN       6
#define PC_MAX_BOARDS     30
#define PC_MAX_POSITIONS  120
#define PC_GRID_MAX_COLS  12
#define PC_GRID_MAX_ROWS  12

#define PC_CONFIG_MAGIC    0x53534346u  /* "SSCF" */
#define PC_CONFIG_VERSION  2u

#pragma pack(push, 1)
typedef struct {
    bool     assigned;
    uint8_t  uuid[PC_UUID_LEN];
    uint8_t  axis;              /* 0..3 */
} pc_position_t;

/* Persistent per-UUID record.  board_id keeps the motor fields put; the
 * build/flags/result are the last confirmed upgrade answer. */
typedef struct {
    uint8_t  uuid[PC_UUID_LEN];
    int8_t   board_id;          /* -1 when unused */
    uint32_t last_build;        /* 0 = version unknown */
    uint8_t  version_flags;     /* bit 0 dirty */
    char     last_result[24];
} pc_board_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint8_t  grid_cols;
    uint8_t  grid_rows;
    uint16_t test_drop_mm;
    uint16_t test_speed_mm_s;
    uint16_t test_step_delay_ms;
    uint8_t  test_serpentine;
    uint8_t  brightness;
    pc_position_t positions[PC_MAX_POSITIONS];
    pc_board_t boards[PC_MAX_BOARDS];
    uint32_t crc;
} pc_config_v2_t;
#pragma pack(pop)

uint32_t pc_crc32(const void *data, size_t len);

/* Serialise the version-2 record with its CRC.  Unpack rejects a version-1
 * blob (the panel loads that as "version unknown" and does not invent UUIDs
 * for old rows; the operator re-assigns). */
int pc_config_v2_pack(const pc_config_v2_t *cfg, uint8_t *out, size_t cap);
int pc_config_v2_unpack(const uint8_t *blob, size_t len, pc_config_v2_t *out);

void pc_config_v2_defaults(pc_config_v2_t *cfg);
int  pc_config_v2_find_position(const pc_config_v2_t *cfg,
                                const uint8_t uuid[PC_UUID_LEN], uint8_t axis);
int  pc_config_v2_get_board_id(const pc_config_v2_t *cfg,
                               const uint8_t uuid[PC_UUID_LEN]);
bool pc_config_v2_set_board(pc_config_v2_t *cfg,
                            const uint8_t uuid[PC_UUID_LEN], int board_id,
                            uint32_t build, uint8_t flags,
                            const char *result);

/* ------------------------------------------------------------------ */
/*  QUERY ALL retry                                                    */
/* ------------------------------------------------------------------ */

#define PC_QUERY_ATTEMPTS 3
#define PC_QUERY_WAIT_MS  2000u

typedef struct {
    int      missing[PC_MAX_BOARDS];
    int      missing_count;
    int      attempt;          /* 1..PC_QUERY_ATTEMPTS */
    uint32_t deadline_ms;
    bool     done;
} pc_vquery_t;

/* Start after the caller cleared version_valid and sent the first query.
 * board_ids are the rows that have a board ID. */
void pc_vquery_start(pc_vquery_t *q, const int *board_ids, int count,
                     uint32_t now_ms);
/* Record an answer; that board is no longer missing. */
void pc_vquery_response(pc_vquery_t *q, int board_id);
/* Advance time.  Returns 1 when the caller must resend VERSION_REQ to the
 * remaining board_ids, 0 while waiting, -1 when the query is finished. */
int  pc_vquery_poll(pc_vquery_t *q, uint32_t now_ms);
int  pc_vquery_missing_count(const pc_vquery_t *q);
bool pc_vquery_is_missing(const pc_vquery_t *q, int board_id);

/* ------------------------------------------------------------------ */
/*  SD listing and panel OTA policy                                    */
/* ------------------------------------------------------------------ */

#define PC_MAX_IMAGES 16

typedef struct {
    rc_manifest_t entries[PC_MAX_IMAGES];
    int count;
} pc_image_list_t;

/* Add a manifest only when the SHA-256 of the file bytes matches.  A broken
 * hash is ignored (not stored); a factory manifest is kept for the service
 * screen and rejected by the OTA selector. */
int  pc_image_list_add(pc_image_list_t *list, const rc_manifest_t *m,
                       const char *file_sha256);
const rc_manifest_t *pc_image_select_panel(const pc_image_list_t *list);
const rc_manifest_t *pc_image_select_fly(const pc_image_list_t *list);
const rc_manifest_t *pc_image_find_factory(const pc_image_list_t *list);

typedef enum {
    PC_OTA_OK = 0,
    PC_OTA_BUSY,
    PC_OTA_BAD_MANIFEST,
    PC_OTA_BAD_PARTITION,
    PC_OTA_TOO_LARGE,
    PC_OTA_HASH_MISMATCH,
    PC_OTA_WRITE_FAILED
} pc_ota_result_t;

typedef struct {
    bool show_running;
    bool queue_active;
    bool server_on;           /* browser upload (phase R9) */
    uint32_t max_size;
    const char *running_partition_sha256;  /* may be NULL in a host test */
} pc_ota_request_t;

/* The write callback returns 0 on success.  mark_valid is called only when
 * the write, hash and self-test all pass. */
typedef int (*pc_ota_write_fn)(void *ctx, const uint8_t *data, size_t len);
typedef int (*pc_ota_mark_valid_fn)(void *ctx);

typedef struct {
    pc_ota_write_fn write;
    pc_ota_mark_valid_fn mark_valid;
    void *ctx;
} pc_ota_callbacks_t;

pc_ota_result_t pc_ota_check(const pc_ota_request_t *req,
                             const rc_manifest_t *m, uint32_t body_size);
pc_ota_result_t pc_ota_run(const pc_ota_request_t *req,
                           const rc_manifest_t *m,
                           const uint8_t *data, size_t len, bool self_test_ok,
                           const pc_ota_callbacks_t *cb);

#ifdef __cplusplus
}
#endif

#endif /* PANEL_CORE_H */
