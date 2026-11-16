#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "can_transport.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Feed every received CAN frame here before commission_on_frame().  Only
 * 0x3E9 bootloader responses are stored. */
void katapult_on_frame(const ss_can_frame_t *frame);

/* Flash one Fly-Micro4 over CAN with the Katapult byte stream.  The image is
 * the raw application at 0x10004000.  Returns true after COMPLETE and the
 * read-back comparison.  Synchronous; call from the main loop or a worker,
 * never from the CAN RX task or the LVGL draw path. */
bool katapult_flash(int board_id, const uint8_t uuid[6],
                    const uint8_t *image, uint32_t size,
                    char *status, unsigned status_len);

#ifdef __cplusplus
}
#endif
