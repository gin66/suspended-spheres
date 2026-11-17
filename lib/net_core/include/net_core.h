#ifndef NET_CORE_H
#define NET_CORE_H

/*
 * net_core — host-tested WiFi, browser-upload and GitHub-pull policy.
 * No sockets, no TLS, no NVS and no wall clock
 * (firmware_release_whitepaper.md §3.7–§3.10, firmware_release_tdd.md R9/R10).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "panel_core.h"
#include "release_core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NET_OFF = 0,
    NET_AP_SETUP,
    NET_SCANNING,
    NET_CONNECTING,
    NET_CONNECTED,
    NET_FAILED
} net_state_t;

#define NET_SETUP_WINDOW_MS (10u * 60u * 1000u)

typedef struct {
    net_state_t state;
    bool have_credentials;
    uint32_t setup_deadline_ms;
    char reason[64];
} net_link_t;

void net_link_init(net_link_t *l);
/* OFF -> AP_SETUP when no credentials; a stored credential goes to
 * CONNECTING.  After an optional scan the caller returns to AP_SETUP. */
void net_link_enable_setup(net_link_t *l, uint32_t now_ms);
void net_link_scan_start(net_link_t *l);
void net_link_scan_done(net_link_t *l, int found);
void net_link_submit(net_link_t *l, const char *ssid, const char *password,
                     uint32_t now_ms);
/* Failure keeps CONNECTING and the credential flag for a visible retry; it
 * never falls back to AP_SETUP. */
void net_link_result(net_link_t *l, bool connected, const char *reason);
void net_link_forget(net_link_t *l, uint32_t now_ms);
void net_link_poll(net_link_t *l, uint32_t now_ms);
const char *net_state_text(net_state_t s);

/* ------------------------------------------------------------------ */
/*  Browser upload                                                     */
/* ------------------------------------------------------------------ */

typedef enum {
    NET_UP_OK = 0,
    NET_UP_SERVER_OFF,
    NET_UP_UNAUTHORIZED,
    NET_UP_SHOW_ACTIVE,
    NET_UP_TOO_LARGE,
    NET_UP_BAD_MANIFEST,
    NET_UP_NO_SD,
    NET_UP_WRITE_FAILED,
    NET_UP_HASH_MISMATCH
} net_upload_result_t;

typedef struct {
    bool server_on;           /* Settings switch for this power cycle */
    bool password_ok;
    bool show_running;
    bool queue_active;
    uint32_t max_size;
} net_upload_req_t;

typedef struct {
    bool sd_ready;
    pc_ota_write_fn sd_write;
    void *ctx;
} net_store_t;

net_upload_result_t net_upload_check(const net_upload_req_t *req,
                                     const rc_manifest_t *m, size_t body_size);

/* A panel bundle goes through the OTA writer.  A Fly bundle is stored on SD
 * and never becomes an OTA write; no card refuses it. */
net_upload_result_t net_upload_run(const net_upload_req_t *req,
                                   const rc_manifest_t *m,
                                   const uint8_t *data, size_t len,
                                   bool self_test_ok,
                                   const pc_ota_callbacks_t *ota,
                                   const net_store_t *store,
                                   bool *stored_to_sd);

/* ------------------------------------------------------------------ */
/*  Session enable flag                                                */
/* ------------------------------------------------------------------ */

typedef struct {
    bool enabled;
} net_service_t;

void net_service_boot(net_service_t *s);       /* always off at boot */
void net_service_set(net_service_t *s, bool on);
void net_service_power_cycle(net_service_t *s);
/* The flag is not part of the version-2 record: packing the same record with
 * the service on or off gives identical bytes. */
bool net_service_absent_from_config(const net_service_t *s,
                                    const pc_config_v2_t *cfg);

/* Status JSON never carries a credential. */
int net_status_json(char *out, size_t cap, const net_link_t *l,
                    bool service_on, const char *ip);

/* ------------------------------------------------------------------ */
/*  GitHub release pull                                                */
/* ------------------------------------------------------------------ */

typedef struct {
    bool update_available;
    uint32_t build;
    char version[16];
    char reason[64];
} net_github_result_t;

/* Compare a published release.json with the running panel build.  An equal
 * or lower build is not offered. */
int net_github_check(const char *release_json, uint32_t running_build,
                     net_github_result_t *out);

/* SNTP-invalid disables the pull and sets a reason.  The SD path does not
 * call this. */
bool net_github_enabled(bool sntp_valid, char *reason, size_t cap);

/* Owner and repository come from the build flags. */
bool net_github_url_ok(const char *url, const char *owner,
                       const char *repository);

/* Confirm installs through the same OTA/SD stores as R7 and R9; cancel
 * leaves both untouched. */
net_upload_result_t net_github_install(bool confirmed,
                                       const net_upload_req_t *req,
                                       const rc_manifest_t *m,
                                       const uint8_t *data, size_t len,
                                       bool self_test_ok,
                                       const pc_ota_callbacks_t *ota,
                                       const net_store_t *store,
                                       bool *stored_to_sd);

#ifdef __cplusplus
}
#endif

#endif /* NET_CORE_H */
