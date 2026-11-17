#ifndef ANIMATION_STORE_H
#define ANIMATION_STORE_H

#include <stddef.h>

#include "ui_model.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Scans /sdcard/animations for HBP2 files.  The list is cached; call
 * animation_store_refresh() after the card or its contents change. */
void animation_store_init(void);
void animation_store_refresh(void);
void animation_store_request_refresh(void);
void animation_store_poll(void);
int animation_store_count(void);
/* Copy the display name (the SD file name) of one cached row. */
bool animation_store_get_name(int index, char *out, size_t cap);void animation_store_snapshot(int *count, ui_animation_t *dst, int cap,
                              char *status, size_t status_cap);

#ifdef __cplusplus
}
#endif

#endif /* ANIMATION_STORE_H */
