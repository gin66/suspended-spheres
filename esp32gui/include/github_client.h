#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Pinned public repository from SS_GITHUB_OWNER / SS_GITHUB_REPO.  The check
 * reads .../releases/latest/download/release.json; install downloads the
 * panel app and Fly app through the same OTA/SD stores as SD and web. */
void github_client_check(void);
void github_client_install(void);
void github_client_status(char *out, unsigned len);
bool github_client_update_available(void);

#ifdef __cplusplus
}
#endif
