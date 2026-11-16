#ifndef FLEET_CORE_H
#define FLEET_CORE_H

/*
 * fleet_core — the outdated set, the sequential Katapult queue and the tile
 * colour.  No sockets, no wall clock, no LVGL and no TWAI
 * (firmware_release_whitepaper.md §4.5–§4.8, firmware_release_tdd.md R4).
 */

#include <stdbool.h>
#include <stdint.h>

#include "release_core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FL_MAX_BOARDS    30
#define FL_MAX_LOG       256
#define FL_UUID_LEN      6
#define FL_ONLINE_MS     3000u
#define FL_APP_START     0x10004000u
#define FL_BLOCK_SIZE    64u

typedef enum {
    FL_IDLE = 0,
    FL_QUEUED,
    FL_ENTERING,
    FL_CONNECTING,
    FL_WRITING,
    FL_VERIFYING,
    FL_REBOOTING,
    FL_DONE,
    FL_FAILED,
    FL_SKIPPED
} fl_state_t;

typedef enum {
    FL_COLOR_RED = 0,
    FL_COLOR_YELLOW,
    FL_COLOR_GREEN,
    FL_COLOR_DIM,
    FL_COLOR_FAILED
} fl_color_t;

typedef enum {
    FL_REASON_NONE = 0,
    FL_REASON_SELECTED,
    FL_REASON_BUILD_ZERO,
    FL_REASON_VERSION_UNKNOWN,
    FL_REASON_OFFLINE,
    FL_REASON_UNASSIGNED,
    FL_REASON_NOT_IN_CONFIG,
    FL_REASON_CURRENT,
    FL_REASON_NEWER,
    FL_REASON_NOT_SELECTED,
    FL_REASON_TABLE_FULL,
    FL_REASON_FAILED
} fl_reason_t;

typedef struct {
    uint8_t  uuid[FL_UUID_LEN];
    int      board_id;        /* -1 unassigned */
    bool     online;
    bool     version_valid;
    uint8_t  version_major;
    uint8_t  version_minor;
    uint8_t  version_patch;
    uint8_t  version_flags;   /* bit 0 dirty */
    uint32_t current_build;   /* 0 unknown */
    bool     in_config;       /* UUID is in the persisted map */
    uint32_t last_seen_ms;
} fl_node_t;

typedef struct {
    uint32_t build;
    uint8_t  version_major;
    uint8_t  version_minor;
    uint8_t  version_patch;
    char     target[16];
    char     kind[8];
    bool     dirty;
    char     sha256[65];
    const uint8_t *data;
    uint32_t size;
    const uint8_t *previous;
    uint32_t previous_size;
} fl_image_t;

typedef enum {
    FL_CMD_ENTER_BOOTLOADER = 0,
    FL_CMD_SET_NODEID,
    FL_CMD_CONNECT,
    FL_CMD_GET_CANBUS_ID,
    FL_CMD_SEND_BLOCK,
    FL_CMD_EOF,
    FL_CMD_REQUEST_BLOCK,
    FL_CMD_COMPLETE,
    FL_CMD_VERSION_REQ,
    FL_CMD_ASSIGN,
    FL_CMD_QUERY_ALL,
    FL_CMD_WRITE_PREVIOUS
} fl_cmd_kind_t;

typedef struct {
    fl_cmd_kind_t kind;
    int           board_id;
    uint32_t      address;
    int           image;      /* 0 new, 1 previous */
} fl_cmd_t;

typedef struct {
    int         node_count;
    int         tile_count;
    bool        table_full;
    fl_node_t   nodes[FL_MAX_BOARDS];
    fl_state_t  states[FL_MAX_BOARDS];
    fl_color_t  colors[FL_MAX_BOARDS];
    fl_reason_t reasons[FL_MAX_BOARDS];
    int         selected[FL_MAX_BOARDS];
    int         selected_count;
    uint32_t    image_build;
} fl_set_t;

/* Classify every supplied node.  Offline is decided from last_seen_ms and
 * now_ms.  At most 30 nodes are stored; a 31st sets table_full. */
void fl_build_set(fl_set_t *set, const fl_node_t *nodes, int count,
                  const fl_image_t *image, uint32_t now_ms);

const char *fl_reason_text(fl_reason_t reason);
const char *fl_state_text(fl_state_t state);
const char *fl_color_text(fl_color_t color);

/* Tile colour from the queue state after rebuilding the set. */
fl_color_t fl_state_color(fl_state_t state, fl_reason_t reason);

/* ------------------------------------------------------------------ */
/*  Queue                                                              */
/* ------------------------------------------------------------------ */

typedef enum {
    FL_QUEUE_IDLE = 0,
    FL_QUEUE_RUNNING,
    FL_QUEUE_STOPPED,
    FL_QUEUE_FAILED,
    FL_QUEUE_DONE
} fl_queue_status_t;

typedef struct {
    fl_queue_status_t status;
    fl_set_t   set;
    fl_image_t image;
    int        write_previous;
    int        current;          /* index into set */
    int        next_order;       /* position in selected[] */
    uint32_t   block_address;
    uint32_t   blocks_total;
    uint32_t   blocks_done;
    int        log_count;
    fl_cmd_t   log[FL_MAX_LOG];
    int        stop_requested;
    int        show_running;
} fl_queue_t;

/* Validate the image, its SHA-256, its target/kind, the manifest conflict
 * and the show flag, then build the outdated set and emit the first
 * ENTER_BOOTLOADER.  images may hold more than one candidate; two manifests
 * with the same highest build and different SHA-256 refuse the queue.
 * Returns 1 when a board was queued, 0 when no board is outdated, -1 when
 * the queue refuses before any command. */
int fl_queue_start(fl_queue_t *q, const fl_node_t *nodes, int count,
                   const fl_image_t *const *images, int image_count,
                   int show_running, uint32_t now_ms);

/* Rebuild the set without touching the stored queue (after a panel reset). */
void fl_queue_rebuild(fl_queue_t *q, const fl_node_t *nodes, int count,
                      uint32_t now_ms);

int  fl_queue_enter_result(fl_queue_t *q, int ok);
int  fl_queue_connect_result(fl_queue_t *q, int ok, uint32_t start_address,
                             uint32_t block_size, int mcu_ok);
int  fl_queue_canbus_id(fl_queue_t *q, const uint8_t uuid[FL_UUID_LEN]);
int  fl_queue_block_ack(fl_queue_t *q, uint32_t address, int ok);
int  fl_queue_eof_result(fl_queue_t *q, int ok);
int  fl_queue_readback_result(fl_queue_t *q, int match);
int  fl_queue_complete_result(fl_queue_t *q, int ok);
int  fl_queue_version_result(fl_queue_t *q, uint32_t build, int answered);
int  fl_queue_stop(fl_queue_t *q);

int  fl_queue_active(const fl_queue_t *q);
int  fl_queue_yellow_count(const fl_queue_t *q);
int  fl_queue_count_cmd(const fl_queue_t *q, fl_cmd_kind_t kind);
int  fl_queue_count_cmd_for(const fl_queue_t *q, fl_cmd_kind_t kind,
                            int board_id);
uint8_t fl_queue_progress(const fl_queue_t *q);

#ifdef __cplusplus
}
#endif

#endif /* FLEET_CORE_H */
