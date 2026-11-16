/*
 * release_pack — write one release directory from already-built images.
 *
 * It has no network code and no --publish flag.  The workflow is the only
 * publisher (firmware_release_tdd.md R2, firmware_release_whitepaper.md §1.6).
 */

#include "release_core.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

struct Options {
    std::string out;
    std::string release;
    std::string version;
    std::string git;
    std::string fly;
    std::string panel_app;
    std::string panel_factory;
    std::string ota;
    std::string partition_sha;
    uint32_t build = 0;
    uint32_t previous_build = 0;
    bool dirty = false;
};

static int usage(void) {
    std::fprintf(stderr,
        "usage: release_pack --out DIR --version X.Y.Z --build N --git HEX8\n"
        "       [--dirty] [--previous-build N] [--release DIR]\n"
        "       [--fly FILE] [--panel-app FILE] [--panel-factory FILE]\n"
        "       [--ota FILE] [--partition-sha HEX64]\n");
    return 2;
}

static bool read_file(const std::string &path, std::vector<uint8_t> &out) {
    std::ifstream f(path, std::ios::binary);
    if (!f)
        return false;
    f.seekg(0, std::ios::end);
    std::streamoff n = f.tellg();
    if (n < 0)
        return false;
    f.seekg(0, std::ios::beg);
    out.resize((size_t)n);
    if (n > 0)
        f.read(reinterpret_cast<char *>(out.data()), n);
    return f.good() || f.eof();
}

static uint32_t rd_u32le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* A Katapult payload's first word is an SRAM stack pointer and its second
 * word is a reset handler inside the application window.  An 8-byte fixture
 * is enough to test this. */
static bool fly_image_ok(const std::vector<uint8_t> &img, std::string &why) {
    if (img.size() < 8) {
        why = "fly image is shorter than 8 bytes";
        return false;
    }
    uint32_t sp = rd_u32le(img.data());
    uint32_t reset = rd_u32le(img.data() + 4);
    if (sp < 0x20000000u || sp > 0x20042000u) {
        why = "fly image stack pointer is outside SRAM";
        return false;
    }
    const uint32_t app_start = RC_LOAD_ADDRESS_FLY;
    const uint32_t app_end = 0x10200000u;   /* 2 MiB RP2040 flash window */
    if (reset < app_start || reset >= app_end) {
        why = "fly image reset vector is outside the Katapult window";
        return false;
    }
    return true;
}

static bool name_looks_factory(const std::string &path) {
    return path.find("factory") != std::string::npos;
}

static int write_text(const fs::path &path, const std::string &text) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f)
        return -1;
    f.write(text.data(), (std::streamsize)text.size());
    return f.good() ? 0 : -1;
}

static int write_copy(const fs::path &path, const std::vector<uint8_t> &img) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f)
        return -1;
    if (!img.empty())
        f.write(reinterpret_cast<const char *>(img.data()),
                (std::streamsize)img.size());
    return f.good() ? 0 : -1;
}

struct Artifact {
    std::string target;
    std::string kind;
    std::vector<uint8_t> bytes;
    std::string sha;
    std::string filename;
    std::string manifest_text;
    std::string basename;   /* file without .bin */
};

static int make_artifact(Artifact &a, const Options &o, const rc_version_t &ver,
                         const std::string &target, const std::string &kind,
                         const std::vector<uint8_t> &bytes) {
    char name[128];
    if (rc_make_filename(target.c_str(), kind.c_str(), o.build, &ver,
                         o.git.c_str(), o.dirty ? 1 : 0,
                         name, sizeof(name)) < 0)
        return -1;
    a.target = target;
    a.kind = kind;
    a.bytes = bytes;
    a.filename = name;
    char sha[65];
    rc_sha256_hex(bytes.data(), bytes.size(), sha);
    a.sha = sha;

    rc_manifest_t m;
    rc_manifest_init(&m, target.c_str(), kind.c_str(), &ver, o.build, o.build,
                     o.git.c_str(), o.dirty ? 1 : 0, name,
                     (uint32_t)bytes.size(), sha);
    if (target == RC_TARGET_PANEL) {
        std::string psha = o.partition_sha;
        if (psha.empty())
            psha.assign(64, '0');
        std::strncpy(m.partition_table_sha256, psha.c_str(),
                     sizeof(m.partition_table_sha256) - 1);
        m.flash_size = RC_FLASH_SIZE_PANEL;
    }
    char text[2048];
    if (rc_manifest_emit(&m, text, sizeof(text)) < 0)
        return -1;
    a.manifest_text = text;
    a.basename = a.filename.substr(0, a.filename.size() - 4); /* drop .bin */
    return 0;
}

static int write_dir(const fs::path &dir, const std::vector<Artifact> &arts,
                     const rc_release_t &release, bool with_release_json) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec)
        return -1;
    for (const auto &a : arts) {
        if (write_copy(dir / a.filename, a.bytes) < 0)
            return -1;
        if (write_text(dir / (a.basename + ".json"), a.manifest_text) < 0)
            return -1;
    }
    if (with_release_json) {
        char text[4096];
        if (rc_release_emit(&release, text, sizeof(text)) < 0)
            return -1;
        if (write_text(dir / "release.json", text) < 0)
            return -1;
    }
    return 0;
}

int main(int argc, char **argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        std::string k = argv[i];
        auto need = [&](const char *name) -> const char * {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "release_pack: %s needs a value\n", name);
                std::exit(2);
            }
            return argv[++i];
        };
        if (k == "--out") o.out = need("--out");
        else if (k == "--release") o.release = need("--release");
        else if (k == "--version") o.version = need("--version");
        else if (k == "--git") o.git = need("--git");
        else if (k == "--fly") o.fly = need("--fly");
        else if (k == "--panel-app") o.panel_app = need("--panel-app");
        else if (k == "--panel-factory") o.panel_factory = need("--panel-factory");
        else if (k == "--ota") o.ota = need("--ota");
        else if (k == "--partition-sha") o.partition_sha = need("--partition-sha");
        else if (k == "--build") o.build = (uint32_t)std::strtoul(need("--build"), 0, 10);
        else if (k == "--previous-build") o.previous_build = (uint32_t)std::strtoul(need("--previous-build"), 0, 10);
        else if (k == "--dirty") o.dirty = true;
        else if (k == "--help" || k == "-h") return usage();
        else {
            std::fprintf(stderr, "release_pack: unknown flag %s\n", k.c_str());
            return usage();
        }
    }
    if (o.out.empty() || o.version.empty() || o.git.empty() || o.build == 0)
        return usage();

    rc_version_t ver;
    if (rc_semver_parse(o.version[0] == 'v' ? o.version.c_str()
                                            : ("v" + o.version).c_str(), &ver) < 0) {
        std::fprintf(stderr, "release_pack: bad version %s\n", o.version.c_str());
        return 1;
    }
    if (!rc_build_id_accept(o.build, o.previous_build)) {
        std::fprintf(stderr, "release_pack: build id %u is not greater than %u\n",
                     o.build, o.previous_build);
        return 1;
    }

    std::vector<Artifact> arts;
    rc_release_t release;
    rc_release_init(&release, ("v" + o.version).c_str(), o.build);
    release.fly.version = ver;
    release.panel.version = ver;
    release.fly.build = o.build;
    release.panel.build = o.build;

    if (!o.ota.empty()) {
        if (name_looks_factory(o.ota)) {
            std::fprintf(stderr,
                "release_pack: a factory image cannot be passed as --ota\n");
            return 1;
        }
        o.fly = o.ota;
    }

    if (!o.fly.empty()) {
        if (name_looks_factory(o.fly)) {
            std::fprintf(stderr, "release_pack: --fly cannot be a factory image\n");
            return 1;
        }
        Artifact a;
        std::string why;
        if (!read_file(o.fly, a.bytes)) {
            std::fprintf(stderr, "release_pack: cannot read %s\n", o.fly.c_str());
            return 1;
        }
        if (!fly_image_ok(a.bytes, why)) {
            std::fprintf(stderr, "release_pack: %s\n", why.c_str());
            return 1;
        }
        if (make_artifact(a, o, ver, RC_TARGET_FLY, "app", a.bytes) < 0)
            return 1;
        rc_release_add_asset(&release, RC_TARGET_FLY, a.filename.c_str());
        rc_release_add_asset(&release, RC_TARGET_FLY,
                             (a.basename + ".json").c_str());
        arts.push_back(a);
    }

    if (!o.panel_app.empty()) {
        Artifact a;
        if (!read_file(o.panel_app, a.bytes)) {
            std::fprintf(stderr, "release_pack: cannot read %s\n",
                         o.panel_app.c_str());
            return 1;
        }
        if (make_artifact(a, o, ver, RC_TARGET_PANEL, "app", a.bytes) < 0)
            return 1;
        rc_release_add_asset(&release, RC_TARGET_PANEL, a.filename.c_str());
        rc_release_add_asset(&release, RC_TARGET_PANEL,
                             (a.basename + ".json").c_str());
        arts.push_back(a);
    }

    if (!o.panel_factory.empty()) {
        Artifact a;
        if (!read_file(o.panel_factory, a.bytes)) {
            std::fprintf(stderr, "release_pack: cannot read %s\n",
                         o.panel_factory.c_str());
            return 1;
        }
        if (make_artifact(a, o, ver, RC_TARGET_PANEL, "factory", a.bytes) < 0)
            return 1;
        rc_release_add_asset(&release, RC_TARGET_PANEL, a.filename.c_str());
        rc_release_add_asset(&release, RC_TARGET_PANEL,
                             (a.basename + ".json").c_str());
        arts.push_back(a);
    }

    if (arts.empty()) {
        std::fprintf(stderr, "release_pack: no image given\n");
        return 1;
    }

    bool want_release = !o.release.empty();
    if (o.dirty && want_release) {
        /* A bench directory is still useful; the release directory is not
         * written and the caller sees a failure. */
        if (write_dir(o.out, arts, release, false) < 0)
            return 1;
        std::fprintf(stderr,
            "release_pack: dirty tree cannot be written to a release directory\n");
        return 1;
    }

    if (write_dir(o.out, arts, release, true) < 0) {
        std::fprintf(stderr, "release_pack: cannot write %s\n", o.out.c_str());
        return 1;
    }
    if (want_release && write_dir(o.release, arts, release, true) < 0) {
        std::fprintf(stderr, "release_pack: cannot write %s\n",
                     o.release.c_str());
        return 1;
    }
    return 0;
}
