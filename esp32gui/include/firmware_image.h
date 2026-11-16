#pragma once

/*
 * firmware_image — the image source for the upgrade screen.
 *
 * The field source is the microSD card (/firmware/fly and /firmware/panel).
 * A bench build with SS_FLY_IMAGE keeps the embedded Fly app as the fallback
 * when the card is absent.  Manifests and SHA-256 are checked here; the
 * selection and OTA policy stay in panel_core.
 */

#include <stdbool.h>
#include <stdint.h>

#include "release_core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  bool present;
  const uint8_t *data;
  uint32_t size;
  uint32_t build;
  char version[16];
  char sha256[65];
  char source[8];
  char path[96];
} ss_fw_image_t;

void firmware_image_init(void);
bool firmware_image_sd_present(void);
const ss_fw_image_t *firmware_image_fly(void);
const rc_manifest_t *firmware_image_panel(void);
const char *firmware_image_panel_path(void);

#ifdef __cplusplus
}
#endif
