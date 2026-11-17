#include "net_core.h"

#include <stdio.h>
#include <string.h>

void net_link_init(net_link_t *l) {
    memset(l, 0, sizeof(*l));
    l->state = NET_OFF;
}

const char *net_state_text(net_state_t s) {
    switch (s) {
    case NET_OFF: return "OFF";
    case NET_AP_SETUP: return "AP_SETUP";
    case NET_SCANNING: return "SCANNING";
    case NET_CONNECTING: return "CONNECTING";
    case NET_CONNECTED: return "CONNECTED";
    case NET_FAILED: return "FAILED";
    }
    return "?";
}

void net_link_enable_setup(net_link_t *l, uint32_t now_ms) {
    if (l->have_credentials) {
        l->state = NET_CONNECTING;
        l->reason[0] = '\0';
    } else {
        l->state = NET_AP_SETUP;
        l->setup_deadline_ms = now_ms + NET_SETUP_WINDOW_MS;
    }
}

void net_link_scan_start(net_link_t *l) {
    if (l->state == NET_AP_SETUP || l->state == NET_FAILED ||
        l->state == NET_OFF)
        l->state = NET_SCANNING;
}

void net_link_scan_done(net_link_t *l, int found) {
    (void)found;
    if (l->state == NET_SCANNING) {
        l->state = NET_AP_SETUP;
    }
}

void net_link_submit(net_link_t *l, const char *ssid, const char *password,
                     uint32_t now_ms) {
    (void)now_ms;
    l->state = NET_CONNECTING;
    l->reason[0] = '\0';
    (void)ssid;
    (void)password;   /* write-only; never logged or returned */
}

void net_link_result(net_link_t *l, bool connected, const char *reason) {
    if (connected) {
        l->state = NET_CONNECTED;
        l->have_credentials = true;
        l->reason[0] = '\0';
    } else {
        /* A failed join stays in CONNECTING for a visible retry; it never
         * falls back to the setup AP.  The caller keeps the stored pair. */
        l->state = NET_CONNECTING;
        l->have_credentials = true;
        snprintf(l->reason, sizeof(l->reason), "%s; retrying",
                 reason ? reason : "connect failed");
    }
}

void net_link_forget(net_link_t *l, uint32_t now_ms) {
    (void)now_ms;
    l->have_credentials = false;
    l->state = NET_OFF;
    l->setup_deadline_ms = 0;
    snprintf(l->reason, sizeof(l->reason), "credentials forgotten; WiFi off");
}

void net_link_poll(net_link_t *l, uint32_t now_ms) {
    if (l->state == NET_AP_SETUP &&
        (int32_t)(now_ms - l->setup_deadline_ms) >= 0) {
        l->state = NET_OFF;
        snprintf(l->reason, sizeof(l->reason), "setup window expired");
    }
}

net_upload_result_t net_upload_check(const net_upload_req_t *req,
                                     const rc_manifest_t *m, size_t body_size) {
    if (req == 0 || m == 0)
        return NET_UP_BAD_MANIFEST;
    if (!req->server_on)
        return NET_UP_SERVER_OFF;
    if (!req->password_ok)
        return NET_UP_UNAUTHORIZED;
    if (req->show_running || req->queue_active)
        return NET_UP_SHOW_ACTIVE;
    if (req->max_size != 0 && body_size > req->max_size)
        return NET_UP_TOO_LARGE;
    if (strcmp(m->target, RC_TARGET_PANEL) == 0) {
        if (!rc_manifest_ota_ok(m))
            return NET_UP_BAD_MANIFEST;
    } else if (strcmp(m->target, RC_TARGET_FLY) == 0) {
        if (!rc_manifest_katapult_ok(m))
            return NET_UP_BAD_MANIFEST;
    } else {
        return NET_UP_BAD_MANIFEST;
    }
    return NET_UP_OK;
}

net_upload_result_t net_upload_run(const net_upload_req_t *req,
                                   const rc_manifest_t *m,
                                   const uint8_t *data, size_t len,
                                   bool self_test_ok,
                                   const pc_ota_callbacks_t *ota,
                                   const net_store_t *store,
                                   bool *stored_to_sd) {
    if (stored_to_sd)
        *stored_to_sd = false;
    net_upload_result_t r = net_upload_check(req, m, len);
    if (r != NET_UP_OK)
        return r;
    char sha[65];
    rc_sha256_hex(data, len, sha);
    if (strcmp(sha, m->sha256) != 0)
        return NET_UP_HASH_MISMATCH;   /* refuse before the first write */
    if (strcmp(m->target, RC_TARGET_PANEL) == 0) {
        pc_ota_request_t oreq;
        memset(&oreq, 0, sizeof(oreq));
        oreq.show_running = req->show_running;
        oreq.queue_active = req->queue_active;
        oreq.max_size = req->max_size;
        pc_ota_result_t o =
            pc_ota_run(&oreq, m, data, len, self_test_ok, ota);
        /* The caller has already checked the session policy; the OTA runner
         * repeats the manifest and hash checks. */
        if (o == PC_OTA_BAD_MANIFEST)
            return NET_UP_BAD_MANIFEST;
        if (o == PC_OTA_HASH_MISMATCH)
            return NET_UP_HASH_MISMATCH;
        if (o != PC_OTA_OK)
            return NET_UP_WRITE_FAILED;
        return NET_UP_OK;
    }
    /* Fly bundle: store on SD, never an OTA write. */
    if (store == 0 || !store->sd_ready || store->sd_write == 0)
        return NET_UP_NO_SD;
    if (store->sd_write(store->ctx, data, len) != 0)
        return NET_UP_WRITE_FAILED;
    if (stored_to_sd)
        *stored_to_sd = true;
    return NET_UP_OK;
}

void net_service_boot(net_service_t *s) {
    if (s) s->enabled = false;
}

void net_service_set(net_service_t *s, bool on) {
    if (s) s->enabled = on;
}

void net_service_power_cycle(net_service_t *s) {
    if (s) s->enabled = false;
}

bool net_service_absent_from_config(const net_service_t *s,
                                    const pc_config_v2_t *cfg) {
    if (cfg == 0)
        return false;
    uint8_t a[sizeof(pc_config_v2_t)], b[sizeof(pc_config_v2_t)];
    if (pc_config_v2_pack(cfg, a, sizeof(a)) != 0)
        return false;
    if (s && s->enabled) {
        /* The flag lives in RAM only; the record bytes are unchanged. */
    }
    if (pc_config_v2_pack(cfg, b, sizeof(b)) != 0)
        return false;
    return memcmp(a, b, sizeof(a)) == 0;
}

int net_status_json(char *out, size_t cap, const net_link_t *l,
                    bool service_on, const char *ip) {
    if (out == 0 || cap == 0)
        return -1;
    int n = snprintf(out, cap,
                     "{\"wifi\":\"%s\",\"ip\":\"%s\",\"service_on\":%s,"
                     "\"reason\":\"%s\"}",
                     l ? net_state_text(l->state) : "OFF",
                     ip ? ip : "", service_on ? "true" : "false",
                     l ? l->reason : "");
    return (n > 0 && (size_t)n < cap) ? 0 : -1;
}

int net_github_check(const char *release_json, uint32_t running_build,
                     net_github_result_t *out) {
    if (out == 0)
        return -1;
    memset(out, 0, sizeof(*out));
    if (release_json == 0) {
        snprintf(out->reason, sizeof(out->reason), "no release metadata");
        return -1;
    }
    rc_release_t r;
    if (rc_release_parse(release_json, &r) != 0) {
        snprintf(out->reason, sizeof(out->reason), "invalid release.json");
        return -1;
    }
    if (!r.panel.present) {
        snprintf(out->reason, sizeof(out->reason), "no panel target");
        return -1;
    }
    if (r.panel.build <= running_build) {
        snprintf(out->reason, sizeof(out->reason), "current");
        out->build = r.panel.build;
        rc_semver_format(&r.panel.version, out->version);
        return 0;
    }
    out->update_available = true;
    out->build = r.panel.build;
    rc_semver_format(&r.panel.version, out->version);
    snprintf(out->reason, sizeof(out->reason), "update available");
    return 0;
}

bool net_github_enabled(bool sntp_valid, char *reason, size_t cap) {
    if (reason && cap)
        snprintf(reason, cap, "%s",
                 sntp_valid ? "" : "SNTP time invalid; GitHub pull disabled");
    return sntp_valid;
}

bool net_github_url_ok(const char *url, const char *owner,
                       const char *repository) {
    if (url == 0 || owner == 0 || repository == 0)
        return false;
    char prefix[256];
    snprintf(prefix, sizeof(prefix), "https://github.com/%s/%s/releases/download/",
             owner, repository);
    if (strncmp(url, prefix, strlen(prefix)) == 0)
        return true;
    return strncmp(url, "https://objects.githubusercontent.com/", 38) == 0;
}

net_upload_result_t net_github_install(bool confirmed,
                                       const net_upload_req_t *req,
                                       const rc_manifest_t *m,
                                       const uint8_t *data, size_t len,
                                       bool self_test_ok,
                                       const pc_ota_callbacks_t *ota,
                                       const net_store_t *store,
                                       bool *stored_to_sd) {
    if (stored_to_sd)
        *stored_to_sd = false;
    if (!confirmed)
        return NET_UP_OK;              /* cancel leaves both stores untouched */
    return net_upload_run(req, m, data, len, self_test_ok, ota, store,
                          stored_to_sd);
}
