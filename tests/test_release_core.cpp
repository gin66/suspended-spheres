/* test_release_core.cpp — firmware_release_tdd.md phase R1 (A1–A9). */

#include "assert.h"
#include "release_core.h"
#include "test_list.h"

#include <string.h>

static int str_contains(const char *hay, const char *needle) {
    return strstr(hay, needle) != 0;
}

static void remove_key(char *json, const char *key) {
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\":", key);
    char *p = strstr(json, pat);
    if (!p)
        return;
    char *end = p + strlen(pat);
    if (*end == '"') {
        ++end;
        while (*end && *end != '"')
            ++end;
        if (*end == '"')
            ++end;
    } else if (strncmp(end, "true", 4) == 0) {
        end += 4;
    } else if (strncmp(end, "false", 5) == 0) {
        end += 5;
    } else {
        while (*end && *end != ',' && *end != '}')
            ++end;
    }
    memmove(p, end, strlen(end) + 1);
}

static void replace_value(char *json, const char *key, const char *value) {
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\":", key);
    char *p = strstr(json, pat);
    if (!p)
        return;
    char *start = p + strlen(pat);
    char *end = start;
    if (*end == '"') {
        ++end;
        while (*end && *end != '"')
            ++end;
        if (*end == '"')
            ++end;
    } else {
        while (*end && *end != ',' && *end != '}')
            ++end;
    }
    size_t rest = strlen(end) + 1;
    memmove(start + strlen(value), end, rest);
    memcpy(start, value, strlen(value));
}

static void fill_panel_manifest(rc_manifest_t *m) {
    const rc_version_t v = {1, 4, 2};
    rc_manifest_init(m, RC_TARGET_PANEL, "app", &v, 1794839525u, 1794839525u,
                     "9f3c1ab7", 0,
                     "esp32gui_s3_20261116T143205Z_v1.4.2_9f3c1ab7_app.bin",
                     742400,
                     "4a7d000000000000000000000000000000000000000000000000000000000000");
    strcpy(m->partition_table_sha256,
           "b31f000000000000000000000000000000000000000000000000000000000000");
}

static void fill_fly_manifest(rc_manifest_t *m) {
    const rc_version_t v = {1, 4, 2};
    rc_manifest_init(m, RC_TARGET_FLY, "app", &v, 1794839525u, 1794839525u,
                     "9f3c1ab7", 0,
                     "fly_micro4_20261116T143205Z_v1.4.2_9f3c1ab7_app.bin",
                     187392,
                     "9c1e000000000000000000000000000000000000000000000000000000000000");
}

/* A1 — known SHA-256 answers and a one-byte change. */
static int test_a1_sha256(void) {
    char hex[65];
    ASSERT(rc_sha256_hex("", 0, hex) == 0);
    ASSERT(strcmp(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") == 0);
    ASSERT(rc_sha256_hex("abc", 3, hex) == 0);
    ASSERT(strcmp(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0);
    char hex2[65];
    ASSERT(rc_sha256_hex("abd", 3, hex2) == 0);
    ASSERT(strcmp(hex, hex2) != 0);
    ASSERT(rc_hex_is_sha256(hex) == 1);
    ASSERT(rc_hex_is_sha256("abc") == 0);
    return 0;
}

/* A2 — tag parse and rejects. */
static int test_a2_semver(void) {
    rc_version_t v;
    ASSERT(rc_semver_parse("v1.4.2", &v) == 0);
    ASSERT(v.major == 1 && v.minor == 4 && v.patch == 2);
    ASSERT(rc_semver_parse("v0.0.0", &v) == 0);
    ASSERT(rc_semver_parse("v255.255.255", &v) == 0);
    ASSERT(rc_semver_parse("v01.4.2", &v) < 0);
    ASSERT(rc_semver_parse("v1.4", &v) < 0);
    ASSERT(rc_semver_parse("v1.4.2-rc.1", &v) < 0);
    ASSERT(rc_semver_parse("V1.4.2", &v) < 0);
    ASSERT(rc_semver_parse("v256.0.0", &v) < 0);
    ASSERT(rc_semver_parse("", &v) < 0);
    ASSERT(rc_semver_parse("v1.4.2 ", &v) < 0);
    char out[16];
    ASSERT(rc_semver_format(&v, out) == 0);
    return 0;
}

/* A3 — filename and UTC conversion. */
static int test_a3_filename(void) {
    const rc_version_t v = {1, 4, 2};
    char a[128], b[128], utc[17], iso[21];
    ASSERT(rc_make_filename(RC_TARGET_FLY, "app", 1794839525u, &v, "9f3c1ab7",
                            0, a, sizeof(a)) == 0);
    ASSERT(strcmp(a, "fly_micro4_20261116T143205Z_v1.4.2_9f3c1ab7_app.bin") == 0);
    ASSERT(rc_make_filename(RC_TARGET_FLY, "app", 1794839525u, &v, "9f3c1ab7",
                            0, b, sizeof(b)) == 0);
    ASSERT(strcmp(a, b) == 0);
    ASSERT(rc_utc_compact(1794839525u, utc) == 0);
    ASSERT(strcmp(utc, "20261116T143205Z") == 0);
    ASSERT(rc_utc_iso(1794839525u, iso) == 0);
    ASSERT(strcmp(iso, "2026-11-16T14:32:05Z") == 0);
    ASSERT(rc_make_filename(RC_TARGET_FLY, "app", 1794839525u, &v, "9f3c1ab7",
                            1, b, sizeof(b)) == 0);
    ASSERT(strstr(b, "_app_dirty.bin") != 0);
    return 0;
}

/* A4 — build id is 0 or strictly increasing. */
static int test_a4_build_id(void) {
    ASSERT(rc_build_id_accept(0, 0) == 0);
    ASSERT(rc_build_id_accept(0, 10) == 0);
    ASSERT(rc_build_id_accept(10, 0) == 1);
    ASSERT(rc_build_id_accept(10, 9) == 1);
    ASSERT(rc_build_id_accept(10, 10) == 0);
    ASSERT(rc_build_id_accept(9, 10) == 0);
    return 0;
}

/* A5 — canonical manifest emits and parses; broken identity fields fail. */
static int test_a5_manifest(void) {
    rc_manifest_t m, out;
    char text[1024], mutated[1024];

    fill_fly_manifest(&m);
    ASSERT(rc_manifest_emit(&m, text, sizeof(text)) == 0);
    ASSERT(text[0] == '{' && text[1] == '"');
    ASSERT(strstr(text, "\"schema\":1") != 0);
    ASSERT(strstr(text, "\"target\":\"fly_micro4\"") != 0);
    ASSERT(strstr(text, "\"load_address\":268451840") != 0);
    ASSERT(strstr(text, "\"katapult_min\":\"1.0.0\"") != 0);
    ASSERT(strstr(text, " ") == 0);   /* no insignificant whitespace */
    ASSERT(rc_manifest_parse(text, &out) == 0);
    ASSERT(strcmp(out.sha256, m.sha256) == 0);
    ASSERT(out.build == m.build);

    strcpy(mutated, text);
    remove_key(mutated, "sha256");
    ASSERT(rc_manifest_parse(mutated, &out) < 0);

    strcpy(mutated, text);
    replace_value(mutated, "target", "\"other\"");
    ASSERT(rc_manifest_parse(mutated, &out) < 0);

    strcpy(mutated, text);
    replace_value(mutated, "load_address", "0");
    ASSERT(rc_manifest_parse(mutated, &out) < 0);

    rc_manifest_t p;
    char ptext[1024];
    fill_panel_manifest(&p);
    ASSERT(rc_manifest_emit(&p, ptext, sizeof(ptext)) == 0);
    ASSERT(rc_manifest_parse(ptext, &out) == 0);
    strcpy(mutated, ptext);
    replace_value(mutated, "flash_size", "0");
    ASSERT(rc_manifest_parse(mutated, &out) < 0);
    return 0;
}

/* A6 — factory is panel-only and is rejected by both selectors. */
static int test_a6_factory_selector(void) {
    rc_manifest_t p, f, out;
    char text[1024];
    fill_panel_manifest(&p);
    strcpy(p.kind, "factory");
    p.build = 1794839525u;
    strcpy(p.file,
           "esp32gui_s3_20261116T143205Z_v1.4.2_9f3c1ab7_factory.bin");
    ASSERT(rc_manifest_emit(&p, text, sizeof(text)) == 0);
    ASSERT(rc_manifest_parse(text, &out) == 0);
    ASSERT(rc_manifest_ota_ok(&out) == 0);
    ASSERT(rc_manifest_katapult_ok(&out) == 0);

    fill_panel_manifest(&f);
    ASSERT(rc_manifest_ota_ok(&f) == 1);
    ASSERT(rc_manifest_katapult_ok(&f) == 0);

    fill_fly_manifest(&f);
    ASSERT(rc_manifest_katapult_ok(&f) == 1);
    ASSERT(rc_manifest_ota_ok(&f) == 0);
    return 0;
}

/* A7 — same build, different SHA-256 is a conflict. */
static int test_a7_conflict(void) {
    rc_manifest_t a, b;
    fill_fly_manifest(&a);
    fill_fly_manifest(&b);
    b.sha256[0] = (b.sha256[0] == '9') ? '8' : '9';
    const rc_manifest_t *list[2] = {&a, &b};
    const rc_manifest_t *pick = (const rc_manifest_t *)1;
    ASSERT(rc_select_highest(list, 2, 0, &pick) == -1);
    ASSERT(pick == 0);

    b = a;
    ASSERT(rc_select_highest(list, 2, 0, &pick) == 1);
    ASSERT(pick != 0);
    return 0;
}

/* A8 — a dirty stamp makes the _dirty filename and the fleet selector
 * rejects the manifest.  The release parser rejects dirty text. */
static int test_a8_dirty(void) {
    rc_manifest_t m;
    const rc_version_t v = {1, 4, 2};
    char name[128];
    fill_fly_manifest(&m);
    m.dirty = 1;
    ASSERT(rc_make_filename(RC_TARGET_FLY, "app", 1794839525u, &v, "9f3c1ab7",
                            1, name, sizeof(name)) == 0);
    ASSERT(strstr(name, "_dirty") != 0);
    ASSERT(rc_manifest_katapult_ok(&m) == 0);
    char text[1024];
    ASSERT(rc_manifest_emit(&m, text, sizeof(text)) == 0);
    rc_manifest_t out;
    ASSERT(rc_manifest_parse(text, &out) < 0);
    return 0;
}

/* A9 — release.json carries both targets with one version and one build;
 * factory files are panel assets and a Katapult UF2 is not an asset. */
static int test_a9_release_json(void) {
    rc_release_t r;
    const rc_version_t v = {1, 4, 2};
    rc_release_init(&r, "v1.4.2", 1794839700u);
    r.fly.version = v; r.fly.build = 1794839525u;
    r.panel.version = v; r.panel.build = 1794839525u;
    ASSERT(rc_release_add_asset(&r, RC_TARGET_FLY,
        "fly_micro4_20261116T143205Z_v1.4.2_9f3c1ab7_app.bin") == 0);
    ASSERT(rc_release_add_asset(&r, RC_TARGET_FLY,
        "fly_micro4_20261116T143205Z_v1.4.2_9f3c1ab7_app.json") == 0);
    ASSERT(rc_release_add_asset(&r, RC_TARGET_PANEL,
        "esp32gui_s3_20261116T143205Z_v1.4.2_9f3c1ab7_app.bin") == 0);
    ASSERT(rc_release_add_asset(&r, RC_TARGET_PANEL,
        "esp32gui_s3_20261116T143205Z_v1.4.2_9f3c1ab7_factory.bin") == 0);
    ASSERT(rc_release_validate(&r) == 1);
    char text[2048];
    ASSERT(rc_release_emit(&r, text, sizeof(text)) == 0);
    ASSERT(strstr(text, "\"fly_micro4\"") != 0);
    ASSERT(strstr(text, "\"esp32gui_s3\"") != 0);
    ASSERT(strstr(text, "\"build\":1794839525") != 0);
    rc_release_t out;
    ASSERT(rc_release_parse(text, &out) == 0);
    ASSERT(out.fly.asset_count == 2);
    ASSERT(out.panel.asset_count == 2);

    rc_release_t bad;
    rc_release_init(&bad, "v1.4.2", 1794839700u);
    bad.fly.version = v; bad.fly.build = 1;
    bad.panel.version = v; bad.panel.build = 1;
    ASSERT(rc_release_add_asset(&bad, RC_TARGET_FLY,
        "fly_micro4_20261116T143205Z_v1.4.2_9f3c1ab7_app.bin") == 0);
    ASSERT(rc_release_add_asset(&bad, RC_TARGET_PANEL,
        "esp32gui_s3_20261116T143205Z_v1.4.2_9f3c1ab7_app.bin") == 0);
    ASSERT(rc_release_add_asset(&bad, RC_TARGET_FLY,
        "katapult_rp2040_20261101T090000Z_0123abcd.uf2") == 0);
    ASSERT(rc_release_validate(&bad) == 0);
    return 0;
}

static int reg_a1 = add_test("R1-A1", test_a1_sha256);
static int reg_a2 = add_test("R1-A2", test_a2_semver);
static int reg_a3 = add_test("R1-A3", test_a3_filename);
static int reg_a4 = add_test("R1-A4", test_a4_build_id);
static int reg_a5 = add_test("R1-A5", test_a5_manifest);
static int reg_a6 = add_test("R1-A6", test_a6_factory_selector);
static int reg_a7 = add_test("R1-A7", test_a7_conflict);
static int reg_a8 = add_test("R1-A8", test_a8_dirty);
static int reg_a9 = add_test("R1-A9", test_a9_release_json);
