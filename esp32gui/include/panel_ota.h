#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "release_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Write the SD panel image to the inactive OTA slot, set it as the boot
 * partition and restart.  Refuses while the Fly queue runs. */
bool panel_ota_start(char *status, unsigned status_len);

/* Apply an in-memory panel bundle (web upload).  Does not restart. */
bool panel_ota_apply(const rc_manifest_t *m, const uint8_t *data, size_t len,
                     char *status, unsigned status_len);

/* Called at the end of bring-up.  If this image is PENDING_VERIFY, mark it
 * valid when the panel and CAN are up, otherwise roll back. */
void panel_ota_boot_check(bool panel_ok);

#ifdef __cplusplus
}
#endif
