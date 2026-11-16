/* test_release_pack.cpp — firmware_release_tdd.md phase R2 (P1–P5).
 * The small CLI test runs the built release_pack binary on a temporary
 * directory; the library assertions live in test_release_core.cpp. */

#include "assert.h"
#include "test_list.h"
#include "release_core.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

#ifndef RELEASE_PACK_BIN
#define RELEASE_PACK_BIN "./build/release_pack"
#endif

static void put_u32le(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static void write_bytes(const fs::path &p, const std::vector<uint8_t> &b) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!b.empty())
        f.write(reinterpret_cast<const char *>(b.data()),
                (std::streamsize)b.size());
}

static std::string read_text(const fs::path &p) {
    std::ifstream f(p, std::ios::binary);
    std::string s((std::istreambuf_iterator<char>(f)),
                  std::istreambuf_iterator<char>());
    return s;
}

static std::vector<uint8_t> read_bytes(const fs::path &p) {
    std::ifstream f(p, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),
                                std::istreambuf_iterator<char>());
}

static int run(const std::string &args) {
    std::string cmd = std::string("\"") + RELEASE_PACK_BIN + "\" " + args +
                      " >/dev/null 2>&1";
    return std::system(cmd.c_str());
}

static std::vector<uint8_t> fly_fixture(void) {
    std::vector<uint8_t> b(64, 0xAA);
    put_u32le(b.data(), 0x20001000u);        /* stack pointer in SRAM */
    put_u32le(b.data() + 4, 0x10004101u);    /* reset vector, Thumb bit */
    return b;
}

static fs::path make_tmp(const char *tag) {
    fs::path p = fs::temp_directory_path() / (std::string("ss_release_") + tag);
    std::error_code ec;
    fs::remove_all(p, ec);
    fs::create_directories(p, ec);
    return p;
}

/* P1 — clean fly invocation writes the bin, the manifest and release.json. */
static int test_p1_clean(void) {
    fs::path t = make_tmp("p1");
    fs::path in = t / "in.bin";
    fs::path out = t / "out";
    write_bytes(in, fly_fixture());
    char args[1024];
    snprintf(args, sizeof(args),
             "--out \"%s\" --version 1.4.2 --build 1794839525 --git 9f3c1ab7 --fly \"%s\"",
             out.c_str(), in.c_str());
    ASSERT(run(args) == 0);
    const char *name = "fly_micro4_20261116T143205Z_v1.4.2_9f3c1ab7_app.bin";
    ASSERT(fs::exists(out / name));
    ASSERT(fs::exists(out / "fly_micro4_20261116T143205Z_v1.4.2_9f3c1ab7_app.json"));
    ASSERT(fs::exists(out / "release.json"));
    std::string json = read_text(out /
        "fly_micro4_20261116T143205Z_v1.4.2_9f3c1ab7_app.json");
    std::vector<uint8_t> bytes = read_bytes(out / name);
    char sha[65];
    rc_sha256_hex(bytes.data(), bytes.size(), sha);
    ASSERT(json.find(sha) != std::string::npos);
    ASSERT(json.find("\"build\":1794839525") != std::string::npos);
    return 0;
}

/* P2 — dirty writes _dirty names, a bench directory and no release directory. */
static int test_p2_dirty(void) {
    fs::path t = make_tmp("p2");
    fs::path in = t / "in.bin";
    fs::path out = t / "bench";
    fs::path rel = t / "release";
    write_bytes(in, fly_fixture());
    char args[1200];
    snprintf(args, sizeof(args),
             "--out \"%s\" --release \"%s\" --dirty --version 1.4.2 "
             "--build 1794839525 --git 9f3c1ab7 --fly \"%s\"",
             out.c_str(), rel.c_str(), in.c_str());
    ASSERT(run(args) != 0);
    ASSERT(fs::exists(out));
    ASSERT(fs::exists(out /
        "fly_micro4_20261116T143205Z_v1.4.2_9f3c1ab7_app_dirty.bin"));
    ASSERT(!fs::exists(rel));
    return 0;
}

/* P3 — a previous build equal to the new one writes nothing. */
static int test_p3_previous_build(void) {
    fs::path t = make_tmp("p3");
    fs::path in = t / "in.bin";
    fs::path out = t / "out";
    write_bytes(in, fly_fixture());
    char args[1200];
    snprintf(args, sizeof(args),
             "--out \"%s\" --version 1.4.2 --build 1794839525 "
             "--previous-build 1794839525 --git 9f3c1ab7 --fly \"%s\"",
             out.c_str(), in.c_str());
    ASSERT(run(args) != 0);
    ASSERT(!fs::exists(out));
    return 0;
}

/* P4 — a fly image with a bad first word is rejected. */
static int test_p4_bad_fly(void) {
    fs::path t = make_tmp("p4");
    fs::path in = t / "in.bin";
    fs::path out = t / "out";
    std::vector<uint8_t> b = fly_fixture();
    put_u32le(b.data(), 0x30001000u);        /* outside SRAM */
    write_bytes(in, b);
    char args[1024];
    snprintf(args, sizeof(args),
             "--out \"%s\" --version 1.4.2 --build 1794839525 --git 9f3c1ab7 --fly \"%s\"",
             out.c_str(), in.c_str());
    ASSERT(run(args) != 0);
    ASSERT(!fs::exists(out));

    b = fly_fixture();
    put_u32le(b.data() + 4, 0x00000101u);    /* outside the app window */
    write_bytes(in, b);
    ASSERT(run(args) != 0);
    return 0;
}

/* P5 — a factory image is accepted only as kind: factory. */
static int test_p5_factory(void) {
    fs::path t = make_tmp("p5");
    fs::path in = t / "panel_factory.bin";
    fs::path out = t / "out";
    write_bytes(in, {0xE9, 0x00, 0x00, 0x00});
    char args[1200];
    snprintf(args, sizeof(args),
             "--out \"%s\" --version 1.4.2 --build 1794839525 --git 9f3c1ab7 --ota \"%s\"",
             out.c_str(), in.c_str());
    ASSERT(run(args) != 0);
    ASSERT(!fs::exists(out));

    fs::path out2 = t / "out2";
    snprintf(args, sizeof(args),
             "--out \"%s\" --version 1.4.2 --build 1794839525 --git 9f3c1ab7 "
             "--panel-factory \"%s\"", out2.c_str(), in.c_str());
    ASSERT(run(args) == 0);
    ASSERT(fs::exists(out2));
    return 0;
}

static int reg_p1 = add_test("R2-P1", test_p1_clean);
static int reg_p2 = add_test("R2-P2", test_p2_dirty);
static int reg_p3 = add_test("R2-P3", test_p3_previous_build);
static int reg_p4 = add_test("R2-P4", test_p4_bad_fly);
static int reg_p5 = add_test("R2-P5", test_p5_factory);
