/* test_workflow.cpp — firmware_release_tdd.md R8 (W1–W5, W7 contract).
 * Reads the workflow as text; it does not run Actions. */

#include "assert.h"
#include "test_list.h"

#include <fstream>
#include <string>

#ifndef RELEASE_WORKFLOW_YML
#define RELEASE_WORKFLOW_YML ".github/workflows/release.yml"
#endif

static std::string read_workflow(void) {
    std::ifstream f(RELEASE_WORKFLOW_YML, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)),
                       std::istreambuf_iterator<char>());
}

static bool has(const std::string &s, const char *needle) {
    return s.find(needle) != std::string::npos;
}

/* W1 — tag trigger only, no workflow_dispatch. */
static int test_w1_trigger(void) {
    std::string w = read_workflow();
    ASSERT(!w.empty());
    ASSERT(has(w, "push:"));
    ASSERT(has(w, "tags:"));
    ASSERT(has(w, "v[0-9]+.[0-9]+.[0-9]+"));
    ASSERT(!has(w, "workflow_dispatch"));
    return 0;
}

/* W2 — contents: write and no personal access token or other secret. */
static int test_w2_permissions(void) {
    std::string w = read_workflow();
    ASSERT(has(w, "contents: write"));
    size_t pos = 0;
    while ((pos = w.find("secrets.", pos)) != std::string::npos) {
        const size_t n = std::string("secrets.GITHUB_TOKEN").size();
        ASSERT(w.compare(pos, n, "secrets.GITHUB_TOKEN") == 0);
        pos += n;
    }
    ASSERT(!has(w, "ghp_"));
    ASSERT(!has(w, "personal_access_token"));
    return 0;
}

/* W3 — tests before builds, builds before publish. */
static int test_w3_order(void) {
    std::string w = read_workflow();
    size_t tests = w.find("motion_core_tests");
    size_t build = w.find("pio run");
    size_t publish = w.find("gh release create");
    ASSERT(tests != std::string::npos);
    ASSERT(build != std::string::npos);
    ASSERT(publish != std::string::npos);
    ASSERT(tests < build);
    ASSERT(build < publish);
    return 0;
}

/* W4 — release environments and flags. */
static int test_w4_environments(void) {
    std::string w = read_workflow();
    ASSERT(has(w, "pio run -e fly_micro4_katapult"));
    ASSERT(has(w, "pio run -e esp32can"));
    ASSERT(has(w, "SS_RELEASE"));
    ASSERT(!has(w, "SS_CAN_MOCK"));
    ASSERT(!has(w, "esp32usb"));
    return 0;
}

/* W5 — gh release create is the only publish step. */
static int test_w5_publish(void) {
    std::string w = read_workflow();
    ASSERT(has(w, "gh release create"));
    ASSERT(!has(w, "gh release delete"));
    ASSERT(!has(w, "gh release edit"));
    return 0;
}

/* W7 — version tag, generated notes, existing release and monotonic build
 * checks are in the file.  Annotated tags are not required. */
static int test_w7_guards(void) {
    std::string w = read_workflow();
    ASSERT(!has(w, "git cat-file -t"));
    ASSERT(!has(w, "annotated"));
    ASSERT(has(w, "--generate-notes"));
    ASSERT(has(w, "gh release view"));
    ASSERT(has(w, "previous-release.json"));
    ASSERT(has(w, "not greater"));
    return 0;
}

static int reg_w1 = add_test("R8-W1", test_w1_trigger);
static int reg_w2 = add_test("R8-W2", test_w2_permissions);
static int reg_w3 = add_test("R8-W3", test_w3_order);
static int reg_w4 = add_test("R8-W4", test_w4_environments);
static int reg_w5 = add_test("R8-W5", test_w5_publish);
static int reg_w7 = add_test("R8-W7", test_w7_guards);
