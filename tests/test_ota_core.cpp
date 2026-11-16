/* test_ota_core.cpp — firmware_release_tdd.md R7 (S1–S5). */

#include "assert.h"
#include "panel_core.h"
#include "release_core.h"
#include "test_list.h"

#include <cstring>
#include <fstream>
#include <string>

static void panel_manifest(rc_manifest_t *m, uint32_t build, char sha_fill,
                           const char *kind, const char *file) {
    const rc_version_t v = {1, 4, 2};
    char sha[65];
    for (int i = 0; i < 64; ++i) sha[i] = sha_fill;
    sha[64] = '\0';
    rc_manifest_init(m, RC_TARGET_PANEL, kind, &v, build, build, "9f3c1ab7",
                     0, file, 1024, sha);
    strcpy(m->partition_table_sha256,
           "b31f000000000000000000000000000000000000000000000000000000000000");
}

static std::string read_all(const char *path) {
    std::ifstream f(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)),
                       std::istreambuf_iterator<char>());
}

/* S1 — the highest valid build wins; a broken SHA is ignored; a factory
 * manifest is listed for the service screen and not selectable for OTA. */
static int test_s1_listing(void) {
    pc_image_list_t list;
    memset(&list, 0, sizeof(list));
    rc_manifest_t a, b, broken, factory;
    panel_manifest(&a, 100, 'a', "app",
                   "esp32gui_s3_x_v1.4.2_9f3c1ab7_app.bin");
    panel_manifest(&b, 200, 'b', "app",
                   "esp32gui_s3_x_v1.4.2_9f3c1ab7_app.bin");
    panel_manifest(&broken, 300, 'c', "app",
                   "esp32gui_s3_x_v1.4.2_9f3c1ab7_app.bin");
    panel_manifest(&factory, 200, 'd', "factory",
                   "esp32gui_s3_x_v1.4.2_9f3c1ab7_factory.bin");
    char sha_a[65], sha_b[65], sha_broken[65], sha_f[65];
    strncpy(sha_a, a.sha256, sizeof(sha_a));
    strncpy(sha_b, b.sha256, sizeof(sha_b));
    strncpy(sha_broken, broken.sha256, sizeof(sha_broken));
    sha_broken[0] = (sha_broken[0] == '0') ? '1' : '0';
    strncpy(sha_f, factory.sha256, sizeof(sha_f));

    ASSERT(pc_image_list_add(&list, &a, sha_a) == 0);
    ASSERT(pc_image_list_add(&list, &b, sha_b) == 0);
    ASSERT(pc_image_list_add(&list, &broken, sha_broken) < 0);  /* ignored */
    ASSERT(pc_image_list_add(&list, &factory, sha_f) == 0);     /* service */
    ASSERT(list.count == 3);
    const rc_manifest_t *sel = pc_image_select_panel(&list);
    ASSERT(sel != 0);
    ASSERT(sel->build == 200);
    ASSERT(rc_manifest_ota_ok(sel) == 1);
    const rc_manifest_t *fac = pc_image_find_factory(&list);
    ASSERT(fac != 0);
    ASSERT(strcmp(fac->kind, "factory") == 0);
    ASSERT(rc_manifest_ota_ok(fac) == 0);
    return 0;
}

/* S2/S3 — show/queue refuse, and a partition-table mismatch refuses before
 * any write callback. */
static int s_write_count;
static int s_mark_count;
static int ota_write(void *, const uint8_t *, size_t) {
    ++s_write_count;
    return 0;
}
static int ota_mark(void *) {
    ++s_mark_count;
    return 0;
}

static int test_s2_s3_busy_and_partition(void) {
    rc_manifest_t m;
    panel_manifest(&m, 200, 'a', "app",
                   "esp32gui_s3_x_v1.4.2_9f3c1ab7_app.bin");
    uint8_t data[16] = {0};
    char sha[65];
    rc_sha256_hex(data, sizeof(data), sha);
    strncpy(m.sha256, sha, sizeof(m.sha256));

    pc_ota_request_t req;
    memset(&req, 0, sizeof(req));
    req.max_size = 1000;
    ASSERT(pc_ota_check(&req, &m, 16) == PC_OTA_OK);
    req.show_running = true;
    ASSERT(pc_ota_check(&req, &m, 16) == PC_OTA_BUSY);
    req.show_running = false;
    req.queue_active = true;
    ASSERT(pc_ota_check(&req, &m, 16) == PC_OTA_BUSY);
    req.queue_active = false;

    s_write_count = 0;
    s_mark_count = 0;
    pc_ota_callbacks_t cb = {ota_write, ota_mark, nullptr};
    req.running_partition_sha256 =
        "0000000000000000000000000000000000000000000000000000000000000000";
    pc_ota_result_t r = pc_ota_run(&req, &m, data, sizeof(data), true, &cb);
    ASSERT(r == PC_OTA_BAD_PARTITION);
    ASSERT(s_write_count == 0);
    ASSERT(s_mark_count == 0);

    req.running_partition_sha256 = 0;
    ASSERT(pc_ota_run(&req, &m, data, sizeof(data), true, &cb) == PC_OTA_OK);
    ASSERT(s_write_count == 1);
    ASSERT(s_mark_count == 1);
    return 0;
}

/* S4 — a failed self-test leaves the slot unmarked. */
static int test_s4_self_test(void) {
    rc_manifest_t m;
    panel_manifest(&m, 200, 'a', "app",
                   "esp32gui_s3_x_v1.4.2_9f3c1ab7_app.bin");
    uint8_t data[32];
    for (int i = 0; i < 32; ++i) data[i] = (uint8_t)i;
    char sha[65];
    rc_sha256_hex(data, sizeof(data), sha);
    strncpy(m.sha256, sha, sizeof(m.sha256));
    pc_ota_request_t req;
    memset(&req, 0, sizeof(req));
    s_write_count = 0;
    s_mark_count = 0;
    pc_ota_callbacks_t cb = {ota_write, ota_mark, nullptr};
    ASSERT(pc_ota_run(&req, &m, data, sizeof(data), false, &cb) == PC_OTA_OK);
    ASSERT(s_write_count == 1);
    ASSERT(s_mark_count == 0);          /* mark valid skipped */
    return 0;
}

/* S5 — the release environment defines SS_RELEASE without SS_CAN_MOCK, and
 * the common screen/source gate exists. */
static int test_s5_release_gate(void) {
    std::string ini = read_all(ESP32GUI_PLATFORMIO_INI);
    ASSERT(!ini.empty());
    size_t env = ini.find("[env:esp32can]");
    ASSERT(env != std::string::npos);
    std::string can = ini.substr(env);
    ASSERT(can.find("SS_RELEASE") != std::string::npos);
    ASSERT(can.find("SS_CAN_MOCK") == std::string::npos);
    std::string gate = read_all(ESP32GUI_RELEASE_GATE_H);
    ASSERT(gate.find("SS_CAN_MOCK") != std::string::npos);
    ASSERT(gate.find("SS_RELEASE") != std::string::npos);
    ASSERT(gate.find("#error") != std::string::npos);
    return 0;
}

static int reg_s1 = add_test("R7-S1", test_s1_listing);
static int reg_s2 = add_test("R7-S2S3", test_s2_s3_busy_and_partition);
static int reg_s4 = add_test("R7-S4", test_s4_self_test);
static int reg_s5 = add_test("R7-S5", test_s5_release_gate);
