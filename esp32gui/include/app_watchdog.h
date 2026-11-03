/*
 * app_watchdog — boot provenance, task liveness and safe-hold supervision.
 *
 * Concepts adapted from ESP32_projects/ESP32/prj_template (tpl_system WATCH(),
 * RTC boot count, tpl_net_watchdog), reduced to this panel:
 *   - no WiFi, so no network watchdog; the supervised link is CAN.
 *   - a stalled UI/CAN/show task does not reboot the installation. It raises a
 *     safe hold through the registered callback, logs, and leaves the master
 *     alive so the operator can read the reason on the panel.
 */

#ifndef APP_WATCHDOG_H
#define APP_WATCHDOG_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SS_WATCH_UI   0x01u
#define SS_WATCH_CAN  0x02u
#define SS_WATCH_SHOW 0x04u
#define SS_WATCH_ALL  0x07u

#define SS_WATCH_UI_TIMEOUT_MS   3000u
#define SS_WATCH_CAN_TIMEOUT_MS  3000u
#define SS_WATCH_SHOW_TIMEOUT_MS 3000u

typedef struct {
  uint32_t boot_count;
  uint32_t reset_reason;
  uint16_t watchpoint;       /* where the running image says it is */
  uint16_t last_watchpoint;  /* what the previous boot reached */
  bool ui_alive;
  bool can_alive;
  bool show_alive;
  bool can_link_ok;
  bool safe_hold;
  uint32_t last_can_rx_ms;
  char hold_reason[64];
  uint32_t safe_hold_count;
} app_watchdog_status_t;

/* Call once in setup(), before starting tasks. */
void app_watchdog_init(void);

/* Set a coarse checkpoint. This survives a reset and is logged at next boot. */
void app_watchdog_set_watchpoint(uint16_t point);
#define WATCH(i) app_watchdog_set_watchpoint((uint16_t)(i))

/* Task heartbeat. `mask` is one or more SS_WATCH_* bits. */
void app_watchdog_kick(int mask);

/* Note a received CAN frame. This is what keeps the CAN link "alive". */
void app_watchdog_note_can_rx(void);
void app_watchdog_set_can_expected(bool expected);

/* Register the safe-hold action. Called at most once per hold episode. It must
 * be quick and must not block on CAN: encode a stop/hold broadcast and return.
 * The watchdog clears the hold when every supervised task kicks again. */
typedef void (*app_watchdog_hold_fn)(const char *reason);
void app_watchdog_set_hold_callback(app_watchdog_hold_fn fn);

/* Call from the main loop at >= 10 Hz. */
void app_watchdog_poll(void);

const app_watchdog_status_t *app_watchdog_status(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_WATCHDOG_H */
