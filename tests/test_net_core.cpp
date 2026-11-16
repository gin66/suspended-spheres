/* test_net_core.cpp — firmware_release_tdd.md R9 (B1–B6) and R10 (H1–H5). */

#include "assert.h"
#include "net_core.h"
#include "release_core.h"
#include "test_list.h"

#include <string.h>
#include <string>

static int s_write_count;
static int s_sd_count;
static int s_mark_count;

static int ota_write(void *, const uint8_t *, size_t) {
    ++s_write_count;
    return 0;
}
static int sd_write(void *, const uint8_t *, size_t) {
    ++s_sd_count;
    return 0;
}
static int ota_mark(void *) {
    ++s_mark_count;
    return 0;
}

static void panel_manifest(rc_manifest_t *m, uint32_t build,
                           const uint8_t *data, size_t len) {
    const rc_version_t v = {1, 4, 2};
    char sha[65];
    rc_sha256_hex(data, len, sha);
    rc_manifest_init(m, RC_TARGET_PANEL, "app", &v, build, build, "9f3c1ab7",
                     0, "esp32gui_s3_x_v1.4.2_9f3c1ab7_app.bin",
                     (uint32_t)len, sha);
    strcpy(m->partition_table_sha256,
           "b31f000000000000000000000000000000000000000000000000000000000000");
}

static void fly_manifest(rc_manifest_t *m, uint32_t build,
                         const uint8_t *data, size_t len) {
    const rc_version_t v = {1, 4, 2};
    char sha[65];
    rc_sha256_hex(data, len, sha);
    rc_manifest_init(m, RC_TARGET_FLY, "app", &v, build, build, "9f3c1ab7",
                     0, "fly_micro4_x_v1.4.2_9f3c1ab7_app.bin",
                     (uint32_t)len, sha);
}

/* B1 — link states and FORGET. */
static int test_b1_link(void) {
    net_link_t l;
    net_link_init(&l);
    ASSERT(l.state == NET_OFF);
    net_link_enable_setup(&l, 1000);
    ASSERT(l.state == NET_AP_SETUP);
    net_link_submit(&l, "ssid", "wrong", 1000);
    ASSERT(l.state == NET_CONNECTING);
    net_link_result(&l, false, "wrong password");
    ASSERT(l.state == NET_FAILED);
    ASSERT(l.have_credentials == false);
    net_link_enable_setup(&l, 2000);
    ASSERT(l.state == NET_AP_SETUP);
    net_link_submit(&l, "ssid", "right", 2000);
    net_link_result(&l, true, 0);
    ASSERT(l.state == NET_CONNECTED);
    ASSERT(l.have_credentials == true);
    net_link_forget(&l, 3000);
    ASSERT(l.state == NET_AP_SETUP);
    ASSERT(l.have_credentials == false);
    return 0;
}

/* B2 — the setup window supplied as now_ms expires at 10 minutes. */
static int test_b2_setup_window(void) {
    net_link_t l;
    net_link_init(&l);
    net_link_enable_setup(&l, 0);
    ASSERT(l.state == NET_AP_SETUP);
    net_link_poll(&l, NET_SETUP_WINDOW_MS - 1);
    ASSERT(l.state == NET_AP_SETUP);
    net_link_poll(&l, NET_SETUP_WINDOW_MS);
    ASSERT(l.state == NET_OFF);
    return 0;
}

/* B3 — upload refusals leave the write callback untouched. */
static int test_b3_upload_refusals(void) {
    uint8_t data[16];
    for (int i = 0; i < 16; ++i) data[i] = (uint8_t)i;
    rc_manifest_t m;
    panel_manifest(&m, 200, data, sizeof(data));
    net_upload_req_t req;
    memset(&req, 0, sizeof(req));
    req.server_on = true;
    req.password_ok = true;
    req.max_size = 100;
    pc_ota_callbacks_t cb = {ota_write, ota_mark, nullptr};

    s_write_count = 0;
    req.server_on = false;
    ASSERT(net_upload_run(&req, &m, data, sizeof(data), true, &cb, nullptr,
                          nullptr) == NET_UP_SERVER_OFF);
    req.server_on = true;
    req.password_ok = false;
    ASSERT(net_upload_run(&req, &m, data, sizeof(data), true, &cb, nullptr,
                          nullptr) == NET_UP_UNAUTHORIZED);
    req.password_ok = true;
    req.show_running = true;
    ASSERT(net_upload_run(&req, &m, data, sizeof(data), true, &cb, nullptr,
                          nullptr) == NET_UP_SHOW_ACTIVE);
    req.show_running = false;
    req.max_size = 4;
    ASSERT(net_upload_run(&req, &m, data, sizeof(data), true, &cb, nullptr,
                          nullptr) == NET_UP_TOO_LARGE);
    req.max_size = 100;
    rc_manifest_t bad = m;
    bad.kind[0] = 'f';                    /* factory is not an OTA image */
    ASSERT(net_upload_run(&req, &bad, data, sizeof(data), true, &cb, nullptr,
                          nullptr) == NET_UP_BAD_MANIFEST);
    ASSERT(s_write_count == 0);
    return 0;
}

/* B6 — the enable flag is false at boot, RAM only, cleared by a power cycle. */
static int test_b6_enable_flag(void) {
    net_service_t s;
    net_service_boot(&s);
    ASSERT(s.enabled == false);
    pc_config_v2_t cfg;
    pc_config_v2_defaults(&cfg);
    uint8_t a[sizeof(pc_config_v2_t)], b[sizeof(pc_config_v2_t)];
    ASSERT(pc_config_v2_pack(&cfg, a, sizeof(a)) == 0);
    net_service_set(&s, true);
    ASSERT(s.enabled == true);
    ASSERT(net_service_absent_from_config(&s, &cfg));
    ASSERT(pc_config_v2_pack(&cfg, b, sizeof(b)) == 0);
    ASSERT(memcmp(a, b, sizeof(a)) == 0);
    net_service_power_cycle(&s);
    ASSERT(s.enabled == false);
    return 0;
}

/* B4 — a panel bundle streams through OTA; a Fly bundle goes to SD. */
static int test_b4_stores(void) {
    uint8_t data[32];
    for (int i = 0; i < 32; ++i) data[i] = (uint8_t)(i * 3);
    net_upload_req_t req;
    memset(&req, 0, sizeof(req));
    req.server_on = true;
    req.password_ok = true;
    req.max_size = 1000;
    pc_ota_callbacks_t cb = {ota_write, ota_mark, nullptr};
    rc_manifest_t p;
    panel_manifest(&p, 200, data, sizeof(data));
    s_write_count = 0;
    s_sd_count = 0;
    bool stored = false;
    ASSERT(net_upload_run(&req, &p, data, sizeof(data), true, &cb, nullptr,
                          &stored) == NET_UP_OK);
    ASSERT(s_write_count == 1);
    ASSERT(stored == false);

    rc_manifest_t f;
    fly_manifest(&f, 200, data, sizeof(data));
    net_store_t store;
    memset(&store, 0, sizeof(store));
    store.sd_ready = true;
    store.sd_write = sd_write;
    s_write_count = 0;
    ASSERT(net_upload_run(&req, &f, data, sizeof(data), true, &cb, &store,
                          &stored) == NET_UP_OK);
    ASSERT(s_write_count == 0);
    ASSERT(s_sd_count == 1);
    ASSERT(stored == true);

    store.sd_ready = false;
    ASSERT(net_upload_run(&req, &f, data, sizeof(data), true, &cb, &store,
                          &stored) == NET_UP_NO_SD);
    return 0;
}

/* B5 — the status JSON never carries a credential. */
static int test_b5_status_json(void) {
    net_link_t l;
    net_link_init(&l);
    net_link_enable_setup(&l, 0);
    net_link_submit(&l, "ssid", "fixture-password", 0);
    net_link_result(&l, true, 0);
    char json[256];
    ASSERT(net_status_json(json, sizeof(json), &l, true, "10.0.0.5") == 0);
    ASSERT(strstr(json, "fixture-password") == 0);
    ASSERT(strstr(json, "password") == 0);
    ASSERT(strstr(json, "CONNECTED") != 0);
    return 0;
}

static std::string make_release_json(uint32_t build) {
    rc_release_t r;
    const rc_version_t v = {1, 4, 2};
    rc_release_init(&r, "v1.4.2", build);
    r.fly.version = v;
    r.fly.build = build;
    r.panel.version = v;
    r.panel.build = build;
    rc_release_add_asset(&r, RC_TARGET_FLY,
        "fly_micro4_x_v1.4.2_9f3c1ab7_app.bin");
    rc_release_add_asset(&r, RC_TARGET_PANEL,
        "esp32gui_s3_x_v1.4.2_9f3c1ab7_app.bin");
    char text[2048];
    rc_release_emit(&r, text, sizeof(text));
    return std::string(text);
}

/* H1 — only a higher build is offered. */
static int test_h1_update(void) {
    std::string json = make_release_json(200);
    net_github_result_t r;
    ASSERT(net_github_check(json.c_str(), 100, &r) == 0);
    ASSERT(r.update_available == true);
    ASSERT(r.build == 200);
    ASSERT(net_github_check(json.c_str(), 200, &r) == 0);
    ASSERT(r.update_available == false);
    ASSERT(net_github_check(json.c_str(), 300, &r) == 0);
    ASSERT(r.update_available == false);
    return 0;
}

/* H2 — bad hash, wrong target and factory abort before the write callback. */
static int test_h2_bad_asset(void) {
    uint8_t data[16];
    for (int i = 0; i < 16; ++i) data[i] = (uint8_t)i;
    rc_manifest_t m;
    panel_manifest(&m, 200, data, sizeof(data));
    net_upload_req_t req;
    memset(&req, 0, sizeof(req));
    req.server_on = true;
    req.password_ok = true;
    req.max_size = 100;
    pc_ota_callbacks_t cb = {ota_write, ota_mark, nullptr};
    s_write_count = 0;
    m.sha256[0] = (m.sha256[0] == '0') ? '1' : '0';
    ASSERT(net_upload_run(&req, &m, data, sizeof(data), true, &cb, nullptr,
                          nullptr) == NET_UP_HASH_MISMATCH);
    ASSERT(s_write_count == 0);
    panel_manifest(&m, 200, data, sizeof(data));
    strcpy(m.target, RC_TARGET_FLY);
    ASSERT(net_upload_run(&req, &m, data, sizeof(data), true, &cb, nullptr,
                          nullptr) == NET_UP_BAD_MANIFEST);
    panel_manifest(&m, 200, data, sizeof(data));
    strcpy(m.kind, "factory");
    ASSERT(net_upload_run(&req, &m, data, sizeof(data), true, &cb, nullptr,
                          nullptr) == NET_UP_BAD_MANIFEST);
    ASSERT(s_write_count == 0);
    return 0;
}

/* H3 — SNTP-invalid disables only the GitHub pull. */
static int test_h3_sntp(void) {
    char reason[80];
    ASSERT(net_github_enabled(false, reason, sizeof(reason)) == false);
    ASSERT(reason[0] != '\0');
    ASSERT(net_github_enabled(true, reason, sizeof(reason)) == true);
    return 0;
}

/* H4 — owner and repository come from build flags; foreign hosts fail. */
static int test_h4_url(void) {
    ASSERT(net_github_url_ok(
        "https://github.com/acme/suspended/releases/download/v1.4.2/a.bin",
        "acme", "suspended") == true);
    ASSERT(net_github_url_ok(
        "https://github.com/evil/suspended/releases/download/v1/a.bin",
        "acme", "suspended") == false);
    ASSERT(net_github_url_ok("https://evil.example/a.bin", "acme",
                             "suspended") == false);
    ASSERT(net_github_url_ok(
        "https://objects.githubusercontent.com/a.bin", "acme",
        "suspended") == true);
    return 0;
}

/* H5 — cancel leaves both stores untouched; confirm installs. */
static int test_h5_confirm(void) {
    uint8_t data[16];
    for (int i = 0; i < 16; ++i) data[i] = (uint8_t)(i + 1);
    rc_manifest_t m;
    panel_manifest(&m, 200, data, sizeof(data));
    net_upload_req_t req;
    memset(&req, 0, sizeof(req));
    req.server_on = true;
    req.password_ok = true;
    req.max_size = 100;
    pc_ota_callbacks_t cb = {ota_write, ota_mark, nullptr};
    s_write_count = 0;
    s_mark_count = 0;
    bool stored = true;
    ASSERT(net_github_install(false, &req, &m, data, sizeof(data), true, &cb,
                              nullptr, &stored) == NET_UP_OK);
    ASSERT(s_write_count == 0);
    ASSERT(s_mark_count == 0);
    ASSERT(stored == false);
    ASSERT(net_github_install(true, &req, &m, data, sizeof(data), true, &cb,
                              nullptr, &stored) == NET_UP_OK);
    ASSERT(s_write_count == 1);
    ASSERT(s_mark_count == 1);
    return 0;
}

static int reg_b1 = add_test("R9-B1", test_b1_link);
static int reg_b2 = add_test("R9-B2", test_b2_setup_window);
static int reg_b3 = add_test("R9-B3", test_b3_upload_refusals);
static int reg_b6 = add_test("R9-B6", test_b6_enable_flag);
static int reg_b4 = add_test("R9-B4", test_b4_stores);
static int reg_b5 = add_test("R9-B5", test_b5_status_json);
static int reg_h1 = add_test("R10-H1", test_h1_update);
static int reg_h2 = add_test("R10-H2", test_h2_bad_asset);
static int reg_h3 = add_test("R10-H3", test_h3_sntp);
static int reg_h4 = add_test("R10-H4", test_h4_url);
static int reg_h5 = add_test("R10-H5", test_h5_confirm);
