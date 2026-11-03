#ifndef UI_H
#define UI_H

#include "ui_model.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  UI_SCREEN_SHOW = 0,
  UI_SCREEN_GRID,
  UI_SCREEN_NODES,
  UI_SCREEN_MOTOR,
  UI_SCREEN_CONFIG,
  UI_SCREEN_LOG,
  UI_SCREEN_COUNT
} ui_screen_t;

void ui_init(const ui_actions_t *actions);
void ui_update(const ui_snapshot_t *snapshot);
void ui_show_screen(int index);
int ui_current_screen(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_H */
