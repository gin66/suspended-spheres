/*
 * show_player — the panel's HBP2 playback task.
 *
 * Owns the SD file, the PSRAM body, the master timeline and the CAN burst.
 * It sends a type-2 sync at T - SHOW_SYNC_LEAD_US and then one type-0 frame
 * per grid position, addressed by mc_position_field(position).  The Fly maps
 * the position to one of its four motors; the panel never touches that map.
 *
 * The task is pinned to core 0 so the LVGL flush on core 1 cannot delay the
 * 121-frame window burst.  Pause is a controlled stop at the last sent
 * window; resume re-anchors the timeline at the next window.
 */

#ifndef SHOW_PLAYER_H
#define SHOW_PLAYER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  SS_SHOW_IDLE = 0,
  SS_SHOW_LOADING,
  SS_SHOW_PLAYING,
  SS_SHOW_PAUSED,
  SS_SHOW_FAULT
} ss_show_state_t;

typedef struct {
  ss_show_state_t state;
  char name[40];
  char error[64];
  uint32_t elapsed_ms;
  uint32_t duration_ms;
  int window;
  int window_count;
  bool cycle;
} ss_show_status_t;

/* Create the task.  Safe before the CAN transport is started. */
void show_player_init(void);

/* Load one file and start it.  The path is /sdcard/animations/<name>.
 * Returns false only when the request could not be queued. */
bool show_player_start(const char *path, const char *name, bool cycle);

void show_player_pause(void);
bool show_player_resume(void);
void show_player_stop(void);
void show_player_set_cycle(bool on);

bool show_player_running(void);
bool show_player_playing(void);
void show_player_snapshot(ss_show_status_t *out);

#ifdef __cplusplus
}
#endif

#endif /* SHOW_PLAYER_H */
