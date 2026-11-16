#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Write the SD panel image to the inactive OTA slot, set it as the boot
 * partition and restart.  Refuses while the Fly queue runs. */
bool panel_ota_start(char *status, unsigned status_len);

/* Called at the end of bring-up.  If this image is PENDING_VERIFY, mark it
 * valid when the panel and CAN are up, otherwise roll back. */
void panel_ota_boot_check(bool panel_ok);

#ifdef __cplusplus
}
#endif
