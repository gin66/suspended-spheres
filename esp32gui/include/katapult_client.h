#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "can_transport.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Feed every received CAN frame here before commission_on_frame().  Only
 * 0x1E9 bootloader responses are stored. */
void katapult_on_frame(const ss_can_frame_t *frame);

/* Phase text and per-board progress.  Called from the flashing task. */
typedef void (*katapult_progress_fn)(int board_id, const char *phase,
                                     uint8_t progress, void *ctx);

/* Flash one Fly-Micro4 over CAN with the Katapult byte stream.  The image is
 * the raw application at 0x10004000.  Returns true after COMPLETE and the
 * read-back comparison.  Synchronous; call from the main loop or a worker,
 * never from the CAN RX task or the LVGL draw path. */
bool katapult_flash(int board_id, const uint8_t uuid[6],
                    const uint8_t *image, uint32_t size,
                    char *status, unsigned status_len);

/* Same writer with a progress callback; the manager uses it to move the
 * yellow tile and the header bar. */
bool katapult_flash_ex(int board_id, const uint8_t uuid[6],
                       const uint8_t *image, uint32_t size,
                       char *status, unsigned status_len,
                       katapult_progress_fn progress, void *ctx);

#ifdef __cplusplus
}
#endif
