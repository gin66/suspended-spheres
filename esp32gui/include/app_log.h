/*
 * app_log — local, bounded diagnostic log for the installation panel.
 *
 * Concept adapted from ESP32_projects/ESP32/prj_template/tpl_debug_log, but
 * this copy has no WebSocket, HTTP or WiFi sink. The ring lives in PSRAM when
 * available and falls back to internal RAM. New records overwrite the oldest
 * bytes, so the newest records are always readable.
 *
 * A logging call never blocks: the record is formatted on the caller's stack
 * and copied under a short critical section. If the ring is full the oldest
 * bytes are simply overwritten.
 */

#ifndef APP_LOG_H
#define APP_LOG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define APP_LOG_LEVEL_DEBUG 0
#define APP_LOG_LEVEL_INFO  2
#define APP_LOG_LEVEL_WARN  3
#define APP_LOG_LEVEL_ERROR 4

#ifndef APP_LOG_DEFAULT_BYTES
#define APP_LOG_DEFAULT_BYTES (256u * 1024u)
#endif

/* Allocate the ring. Safe to call once from setup(). Returns false only when
 * both the PSRAM and the internal-RAM fallback allocations fail; logging still
 * falls back to Serial in that case. */
bool app_log_init(size_t bytes);

/* Format and append one record. `tag` is copied into the record. */
void app_log_write(const char *tag, int level, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

#define SS_LOGD(tag, ...) app_log_write((tag), APP_LOG_LEVEL_DEBUG, __VA_ARGS__)
#define SS_LOGI(tag, ...) app_log_write((tag), APP_LOG_LEVEL_INFO, __VA_ARGS__)
#define SS_LOGW(tag, ...) app_log_write((tag), APP_LOG_LEVEL_WARN, __VA_ARGS__)
#define SS_LOGE(tag, ...) app_log_write((tag), APP_LOG_LEVEL_ERROR, __VA_ARGS__)

/* Copy up to `max` bytes of the newest records into `out`, NUL-terminated.
 * Returns the number of bytes written (excluding the terminator). */
size_t app_log_dump(char *out, size_t max);

void app_log_clear(void);
size_t app_log_length(void);

/* Serial mirror. Turn it off if UART0 must stay quiet. The ring keeps working.
 * Disabled automatically when the ring could not be allocated. */
void app_log_set_serial(bool enabled);
bool app_log_serial_enabled(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_LOG_H */
