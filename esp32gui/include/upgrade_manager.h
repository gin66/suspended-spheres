#pragma once

/*
 * upgrade_manager — the operator's one-board-at-a-time Katapult queue.
 * Selection and tile colour come from fleet_core; the byte stream comes from
 * katapult_client.  This file owns the task, the image and the UI state.
 */

#include <stdbool.h>
#include <stdint.h>

#include "ui_model.h"

#ifdef __cplusplus
extern "C" {
#endif

void upgrade_manager_init(void);

/* Build the outdated set, stop the test run and start one worker.  Returns
 * false (and leaves a reason in the upgrade status) when no image is loaded,
 * CAN is stopped, or there is nothing to do. */
bool upgrade_manager_start_all(void);
bool upgrade_manager_start_selected(void);

/* Finish the board that is already yellow, then stop before the next one. */
void upgrade_manager_stop(void);

/* Main-loop service: drain persisted results and rebuild the idle tiles. */
void upgrade_manager_poll(void);

/* Re-classify the tiles after QUERY ALL; leaves a finished pass visible until
 * the operator asks again. */
void upgrade_manager_refresh(void);

bool upgrade_manager_active(void);
void upgrade_manager_status(char *out, unsigned len);

/* Overlay the queue state on the snapshot's node array, which is in the same
 * order as commission_node_count(). */
void upgrade_manager_snapshot(ui_snapshot_t *snap);

#ifdef __cplusplus
}
#endif
