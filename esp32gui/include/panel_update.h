#ifndef PANEL_UPDATE_H
#define PANEL_UPDATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Shared state for the panel self-update screen.  The network task produces
 * it, the snapshot task copies it into ui_snapshot_t, and the main loop owns
 * the backlight: after the overlay has been on screen for a moment it blanks
 * the display, so the flash write cannot show a garbled frame.
 */

typedef enum {
  PANEL_UPDATE_IDLE = 0,
  PANEL_UPDATE_DOWNLOAD = 1,
  PANEL_UPDATE_WRITING = 2,
  PANEL_UPDATE_ERROR = 3,
  PANEL_UPDATE_DONE = 4
} panel_update_state_t;

void panel_update_clear(void);
void panel_update_begin(const char *stage);
void panel_update_stage(const char *stage);
void panel_update_progress(int percent);
void panel_update_begin_write(void);
void panel_update_error(const char *text);
void panel_update_done(const char *text);

bool panel_update_writing(void);
bool panel_update_blank_due(uint32_t now_ms, uint32_t delay_ms);
void panel_update_mark_blanked(bool blanked);
bool panel_update_blanked(void);

/* Copy the state for the UI.  progress is -1 when it is unknown. */
void panel_update_get(bool *active, bool *error, int *progress, char *stage,
                      size_t stage_cap, char *result, size_t result_cap);

#ifdef __cplusplus
}
#endif

#endif /* PANEL_UPDATE_H */
