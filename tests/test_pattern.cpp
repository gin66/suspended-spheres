/* test_pattern.cpp — §10 items A1–A10 of motion_streaming_tdd.md.
 *
 * Pattern parse, deterministic bake, limits, report and .hbp export.
 * Headless: no browser, no WebGL, no hardware.  The checks call the
 * shared pattern_core library; the test does not reimplement the bake.
 */

#include "assert.h"
#include "motion_core.h"
#include "pattern_core/pattern_core.h"
#include "test_list.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fstream>
#include <string>
#include <vector>

static int32_t abs_i32(int32_t v) { return v < 0 ? -v : v; }

static bool contains(const std::string &s, const char *needle) {
    return s.find(needle) != std::string::npos;
}

static std::string read_text(const std::string &path) {
    std::ifstream f(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)),
                       std::istreambuf_iterator<char>());
}

static std::vector<unsigned char> read_bytes(const std::string &path) {
    std::ifstream f(path, std::ios::binary);
    return std::vector<unsigned char>((std::istreambuf_iterator<char>(f)),
                                      std::istreambuf_iterator<char>());
}

static pc::AnimFile base_file(pc::Tier tier = pc::TIER_ORDINARY) {
    pc::AnimFile f;
    f.version = 1;
    f.cols = 10;
    f.rows = 10;
    f.dt_ms = 10;
    f.n = 3;
    f.duration_ms = 4000;
    f.tier = tier;
    f.base_um = 1000000;
    return f;
}

static pc::Layer slow_layer(pc::Op op) {
    pc::Layer l;
    l.op = op;
    l.amplitude_um = 50000;
    l.period_ms = 4000;
    l.wavelength_cols = 10;
    l.axis = 0;
    l.direction = 1;
    l.phase_deg = 0;
    l.seed = 12345;
    l.group.center_col = 5;
    l.group.center_row = 5;
    return l;
}

static pc::Trajectory constant_trajectory(const pc::AnimFile &f, int frames,
                                          int32_t pos_um) {
    pc::Trajectory t;
    t.cols = f.cols;
    t.rows = f.rows;
    t.dt_ms = f.dt_ms;
    t.n = f.n;
    t.duration_ms = (frames - 1) * f.dt_ms;
    t.tier = f.tier;
    t.frame_count = frames;
    t.samples.assign((size_t)frames * f.cols * f.rows, pos_um);
    return t;
}

static bool has_limit(const pc::CheckResult &r, const char *name) {
    for (const auto &v : r.violations)
        if (v.limit == name)
            return true;
    return false;
}

/* ------------------------------------------------------------------ */
/*  A1 — version-1 schema                                             */
/* ------------------------------------------------------------------ */

static int test_a1_schema(void) {
    pc::AnimFile f;
    std::string err;
    ASSERT(pc::parse_anim_file("patterns/golden_sine.anim.json", &f, &err));
    ASSERT(f.version == 1);
    ASSERT(f.cols == 10 && f.rows == 12);
    ASSERT(f.dt_ms == 10 && f.n == 3 && f.duration_ms == 1000);
    ASSERT(f.tier == pc::TIER_ORDINARY);
    ASSERT(f.layers.size() == 1);
    ASSERT(f.layers[0].op == pc::OP_SINE);
    ASSERT(f.layers[0].amplitude_um == 50000);
    ASSERT(f.layers[0].period_ms == 5000);
    ASSERT(f.layers[0].wavelength_cols == 10);
    ASSERT(f.layers[0].axis == 0);

    const char *ok10 =
        "{\"version\":1,\"grid\":[10,10],\"dt_ms\":10,\"n\":3,"
        "\"duration_ms\":1000,\"tier\":\"ordinary\",\"layers\":[]}";
    const char *ok12 =
        "{\"version\":1,\"grid\":[10,12],\"dt_ms\":15,\"n\":4,"
        "\"duration_ms\":1000,\"tier\":\"optional\",\"layers\":[]}";
    ASSERT(pc::parse_anim_json(ok10, &f, &err));
    ASSERT(f.cols == 10 && f.rows == 10);
    ASSERT(pc::parse_anim_json(ok12, &f, &err));
    ASSERT(f.cols == 10 && f.rows == 12 && f.tier == pc::TIER_OPTIONAL);

    const char *bad[] = {
        "{\"grid\":[10,10],\"dt_ms\":10,\"n\":3,\"duration_ms\":1000,"
        "\"tier\":\"ordinary\",\"layers\":[]}",
        "{\"version\":1,\"dt_ms\":10,\"n\":3,\"duration_ms\":1000,"
        "\"tier\":\"ordinary\",\"layers\":[]}",
        "{\"version\":1,\"grid\":[10,10],\"n\":3,\"duration_ms\":1000,"
        "\"tier\":\"ordinary\",\"layers\":[]}",
        "{\"version\":1,\"grid\":[10,10],\"dt_ms\":10,\"duration_ms\":1000,"
        "\"tier\":\"ordinary\",\"layers\":[]}",
        "{\"version\":1,\"grid\":[10,10],\"dt_ms\":10,\"n\":3,"
        "\"tier\":\"ordinary\",\"layers\":[]}",
        "{\"version\":1,\"grid\":[10,10],\"dt_ms\":10,\"n\":3,"
        "\"duration_ms\":1000,\"layers\":[]}",
        "{\"version\":1,\"grid\":[10,10],\"dt_ms\":10,\"n\":3,"
        "\"duration_ms\":1000,\"tier\":\"ordinary\"}",
        "{\"version\":1,\"grid\":[8,8],\"dt_ms\":10,\"n\":3,"
        "\"duration_ms\":1000,\"tier\":\"ordinary\",\"layers\":[]}",
        "{\"version\":1,\"grid\":[10,10],\"dt_ms\":10.5,\"n\":3,"
        "\"duration_ms\":1000,\"tier\":\"ordinary\",\"layers\":[]}",
        "{\"version\":1,\"grid\":[10,10],\"dt_ms\":10,\"n\":3,"
        "\"duration_ms\":1000,\"tier\":\"ordinary\",\"layers\":"
        "[{\"op\":\"sine\",\"amplitude_um\":5.5,\"period_ms\":5000,"
        "\"wavelength_cols\":10,\"axis\":\"col\"}]}",
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
        ASSERT(!pc::parse_anim_json(bad[i], &f, &err));

    ASSERT(!pc::parse_anim_file("tests/fixtures/write_sine.py", &f, &err));
    ASSERT(contains(err, "Python"));
    return 0;
}

/* ------------------------------------------------------------------ */
/*  A2 — deterministic bake                                            */
/* ------------------------------------------------------------------ */

static int test_a2_deterministic(void) {
    pc::AnimFile f;
    ASSERT(pc::parse_anim_file("patterns/golden_sine.anim.json", &f, nullptr));
    pc::Trajectory a = pc::bake(f);
    pc::Trajectory b = pc::bake(f);
    ASSERT(a.samples.size() == b.samples.size());
    ASSERT(a.samples == b.samples);

    pc::AnimFile changed = f;
    changed.layers[0].amplitude_um ^= 1;
    pc::Trajectory c = pc::bake(changed);
    ASSERT(c.samples.size() == a.samples.size());
    bool different = false;
    for (size_t i = 0; i < a.samples.size(); ++i)
        if (a.samples[i] != c.samples[i])
            different = true;
    ASSERT(different);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  A3 — every procedural layer bakes and passes a slow take           */
/* ------------------------------------------------------------------ */

static int test_a3_layers(void) {
    const pc::Op ops[] = {
        pc::OP_KEYFRAMES, pc::OP_SINE, pc::OP_TRAVELLING_WAVE,
        pc::OP_INTERFERENCE, pc::OP_SPIRAL, pc::OP_RIPPLE,
        pc::OP_CASCADE, pc::OP_RANDOM_WALK,
    };
    for (pc::Op op : ops) {
        pc::AnimFile f = base_file();
        pc::Layer l = slow_layer(op);
        if (op == pc::OP_KEYFRAMES) {
            l.keys.push_back({0, 0});
            l.keys.push_back({4000, 50000});
        } else if (op == pc::OP_INTERFERENCE) {
            l.amplitude_um = 25000;
            l.amplitude2_um = 25000;
            l.period2_ms = 5000;
            l.wavelength2_cols = 6;
            l.phase2_deg = 90;
        } else if (op == pc::OP_RANDOM_WALK) {
            l.period_ms = 1000;
        } else if (op == pc::OP_CASCADE) {
            l.phase_deg = 100;
        }
        f.layers.push_back(l);
        pc::Trajectory t = pc::bake(f);
        pc::CheckResult r = pc::check(f, t);
        ASSERT(r.ok());
        for (int32_t s : t.samples)
            ASSERT(s >= 0 && s <= 2000000);
    }

    /* The seeded walk repeats exactly and changes with the seed. */
    pc::AnimFile w = base_file();
    pc::Layer walk = slow_layer(pc::OP_RANDOM_WALK);
    walk.period_ms = 1000;
    walk.seed = 4242;
    w.layers.push_back(walk);
    pc::Trajectory w1 = pc::bake(w);
    pc::Trajectory w2 = pc::bake(w);
    ASSERT(w1.samples == w2.samples);
    w.layers[0].seed = 4243;
    pc::Trajectory w3 = pc::bake(w);
    bool different = false;
    for (size_t i = 0; i < w1.samples.size(); ++i)
        if (w1.samples[i] != w3.samples[i])
            different = true;
    ASSERT(different);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  A4 — groups affect only their balls                                */
/* ------------------------------------------------------------------ */

static bool group_member(pc::GroupKind kind, int ball, int cols, int rows) {
    int col = ball % cols;
    int row = ball / cols;
    (void)rows;
    switch (kind) {
    case pc::GROUP_ROW: return row == 2;
    case pc::GROUP_COLUMN: return col == 3;
    case pc::GROUP_CHECKERBOARD: return ((col + row) & 1) == 0;
    case pc::GROUP_MASK: return ball == 0 || ball == 7 || ball == 42;
    case pc::GROUP_RING: {
        int dc = col - 5, dr = row - 5;
        if (dc < 0) dc = -dc;
        if (dr < 0) dr = -dr;
        return (dc > dr ? dc : dr) == 2;
    }
    case pc::GROUP_ALL: return true;
    }
    return false;
}

static int test_a4_groups(void) {
    const pc::GroupKind kinds[] = {
        pc::GROUP_ROW, pc::GROUP_COLUMN, pc::GROUP_RING,
        pc::GROUP_CHECKERBOARD, pc::GROUP_MASK,
    };
    for (pc::GroupKind kind : kinds) {
        pc::AnimFile f = base_file();
        pc::Layer l = slow_layer(pc::OP_SINE);
        l.amplitude_um = 100000;
        l.period_ms = 4000;
        l.wavelength_cols = 1000000;
        l.group.kind = kind;
        l.group.index = kind == pc::GROUP_ROW ? 2 : 3;
        l.group.parity = 0;
        l.group.radius = 2;
        l.group.center_col = 5;
        l.group.center_row = 5;
        l.group.balls = {0, 7, 42};
        f.layers.push_back(l);

        pc::Trajectory t = pc::bake(f);
        const int frame = 100;   /* t = 1000 ms, temporal phase = π/2 */
        for (int b = 0; b < f.cols * f.rows; ++b) {
            int32_t p = t.at(frame, b);
            if (group_member(kind, b, f.cols, f.rows))
                ASSERT(p > 1050000);
            else
                ASSERT(p == 1000000);
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/*  A5 — limit checker and report                                      */
/* ------------------------------------------------------------------ */

static int test_a5_limits_and_report(void) {
    pc::AnimFile f = base_file();

    /* Position outside the travel: a violation, not a clamp. */
    pc::Trajectory t = constant_trajectory(f, 10, 1000000);
    t.samples[3 * 100 + 5] = -100;
    pc::CheckResult r = pc::check(f, t);
    ASSERT(has_limit(r, "position"));
    ASSERT(t.at(3, 5) == -100);

    /* Ordinary take above 0.3 m/s. */
    t = constant_trajectory(f, 10, 1000000);
    for (int fr = 0; fr < 10; ++fr)
        for (int b = 0; b < 100; ++b)
            t.samples[fr * 100 + b] = 1000000 + fr * 3500;
    r = pc::check(f, t);
    ASSERT(has_limit(r, "peak_speed"));

    /* Nominal tier inside 0.44–0.59 m/s and 1.0–1.5 m/s² is clean. */
    pc::AnimFile nf = base_file(pc::TIER_NOMINAL);
    pc::Trajectory nt = constant_trajectory(nf, 12, 1000000);
    const int64_t v0 = 440000, acc = 1200000;
    for (int fr = 0; fr < 12; ++fr) {
        int64_t us = (int64_t)fr * 10000;
        int64_t p = 1000000 + v0 * us / 1000000 +
                    (600000LL * us / 1000000) * us / 1000000;
        for (int b = 0; b < 100; ++b)
            nt.samples[fr * 100 + b] = (int32_t)p;
    }
    r = pc::check(nf, nt);
    ASSERT(r.ok());

    /* Optional tier inside 0.8–1.3 m/s and 2–4 m/s² is clean. */
    pc::AnimFile of = base_file(pc::TIER_OPTIONAL);
    pc::Trajectory ot = constant_trajectory(of, 20, 1000000);
    const int64_t ov0 = 800000, oa = 2500000;
    for (int fr = 0; fr < 20; ++fr) {
        int64_t us = (int64_t)fr * 10000;
        int64_t p = 1000000 + ov0 * us / 1000000 +
                    (oa / 2 * us / 1000000) * us / 1000000;
        for (int b = 0; b < 100; ++b)
            ot.samples[fr * 100 + b] = (int32_t)p;
    }
    r = pc::check(of, ot);
    ASSERT(r.ok());

    /* Downward acceleration at or above g (FR-MOT-012). */
    pc::Trajectory dt = constant_trajectory(f, 8, 1200000);
    for (int fr = 0; fr < 8; ++fr) {
        int64_t p = 1200000;
        int64_t v = -(int64_t)fr * 100000;
        if (fr >= 2) {
            int64_t prev2 = dt.samples[(fr - 2) * 100];
            p = prev2 + v * 20 / 1000;
        }
        for (int b = 0; b < 100; ++b)
            dt.samples[fr * 100 + b] = (int32_t)p;
    }
    r = pc::check(f, dt);
    ASSERT(has_limit(r, "downward_accel"));

    /* Two positions at the same time (FR-PAT-008). */
    t = constant_trajectory(f, 4, 1000000);
    t.times_ms = {0, 0, 20, 30};
    t.samples[100 + 0] = 1001000;
    r = pc::check(f, t);
    ASSERT(has_limit(r, "duplicate_time"));

    /* A 1 s blend between two continuous takes is clean. */
    t = constant_trajectory(f, 101, 1000000);
    for (int fr = 0; fr < 101; ++fr)
        for (int b = 0; b < 100; ++b)
            t.samples[fr * 100 + b] = 1000000 + fr * 2000;
    r = pc::check(f, t);
    ASSERT(r.ok());

    /* Δt/N above the 60 % bus budget. */
    pc::AnimFile bf = base_file();
    bf.n = 2;
    bf.dt_ms = 10;
    pc::Trajectory bt = constant_trajectory(bf, 10, 1000000);
    r = pc::check(bf, bt);
    ASSERT(has_limit(r, "bus_load"));

    /* A 2 ms chunk above 128 steps (S4). */
    pc::AnimFile cf = base_file(pc::TIER_OPTIONAL);
    pc::Trajectory ct = constant_trajectory(cf, 8, 900000);
    for (int fr = 0; fr < 8; ++fr)
        for (int b = 0; b < 100; ++b)
            ct.samples[fr * 100 + b] = 900000 + fr * 30000;
    r = pc::check(cf, ct);
    ASSERT(has_limit(r, "chunk_steps"));

    /* The report JSON names ball, time, limit, measured and allowed. */
    pc::CheckResult one;
    one.violations.push_back({5, 30, "position", -100, 0});
    std::string json = pc::report_json(one);
    ASSERT(contains(json, "\"violations\":["));
    ASSERT(contains(json, "\"ball\":5"));
    ASSERT(contains(json, "\"time_ms\":30"));
    ASSERT(contains(json, "\"limit\":\"position\""));
    ASSERT(contains(json, "\"measured\":-100"));
    ASSERT(contains(json, "\"allowed\":0"));

    const std::string path = "/tmp/suspended_spheres.report.json";
    ASSERT(pc::write_report_file(path, one));
    ASSERT(read_text(path) == json + "\n");
    return 0;
}

/* ------------------------------------------------------------------ */
/*  A6 — .hbp only from a clean report                                  */
/* ------------------------------------------------------------------ */

static int test_a6_hbp(void) {
    pc::AnimFile f;
    ASSERT(pc::parse_anim_file("patterns/golden_sine.anim.json", &f, nullptr));
    pc::Trajectory t = pc::bake(f);
    pc::CheckResult clean = pc::check(f, t);
    ASSERT(clean.ok());

    const std::string path = "/tmp/suspended_spheres_pattern.hbp";
    std::remove(path.c_str());
    std::string err;
    ASSERT(pc::write_hbp(path, f, t, &err));

    std::vector<unsigned char> raw = read_bytes(path);
    ASSERT(raw.size() > 22);
    ASSERT(raw[0] == 'P' && raw[1] == 'K');
    std::string raw_text((const char *)raw.data(), raw.size());
    ASSERT(contains(raw_text, "manifest.json"));
    ASSERT(contains(raw_text, "trajectory.bin"));
    ASSERT(contains(raw_text, "sha256.txt"));

    pc::AnimFile rf;
    pc::Trajectory rt;
    ASSERT(pc::read_hbp(path, &rf, &rt, &err));
    ASSERT(rf.cols == f.cols && rf.rows == f.rows);
    ASSERT(rf.dt_ms == f.dt_ms && rf.n == f.n);
    ASSERT(rt.samples == t.samples);

    /* A dirty take must not replace the previous export. */
    pc::AnimFile dirty = f;
    dirty.layers[0].amplitude_um = 1500000;
    pc::Trajectory dtraj = pc::bake(dirty);
    ASSERT(!pc::check(dirty, dtraj).ok());
    std::vector<unsigned char> before = read_bytes(path);
    ASSERT(!pc::write_hbp(path, dirty, dtraj, &err));
    ASSERT(read_bytes(path) == before);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  A7 — .hbp replays through the core and packs with P1               */
/* ------------------------------------------------------------------ */

static int test_a7_hbp_replay(void) {
    pc::AnimFile f;
    ASSERT(pc::parse_anim_file("patterns/golden_sine.anim.json", &f, nullptr));
    pc::Trajectory t = pc::bake(f);
    const std::string path = "/tmp/suspended_spheres_pattern.hbp";
    std::string err;
    ASSERT(pc::write_hbp(path, f, t, &err));

    pc::AnimFile rf;
    pc::Trajectory rt;
    ASSERT(pc::read_hbp(path, &rf, &rt, &err));

    const int dt_us = rt.dt_ms * 1000;
    for (int b = 0; b < rt.ball_count(); ++b) {
        for (int fr = 0; fr < rt.frame_count; ++fr) {
            int32_t p = rt.at(fr, b);
            mc_segment_t seg = {mc_um_to_code(p), 0, 0, 0};
            ASSERT(mc_segment_eval_um(&seg, 0) ==
                   mc_code_to_um(mc_um_to_code(p)));

            if (fr > 0) {
                int32_t prev = rt.at(fr - 1, b);
                int16_t v = mc_speed_encode(
                    (int32_t)(((int64_t)p - prev) * 1000000 / dt_us));
                mc_segment_t s = {mc_um_to_code(prev), v, 0, 0};
                mc_core_reset();
                ASSERT(mc_trajectory_push(0, 0, 0, dt_us, &s) == 0);
                int32_t end = mc_eval(0, dt_us);
                ASSERT(abs_i32(end - p) <= 15300);
            }

            uint8_t wire[8];
            ASSERT(mc_pack_frame(0, &seg, wire) == 0);
            mc_segment_t back;
            ASSERT(mc_unpack_frame(wire, 8, 0, &back) == 0);
            ASSERT(back.p0 == mc_um_to_code(p));
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/*  A8 — the Python generator is not an input                          */
/* ------------------------------------------------------------------ */

static int test_a8_python_fixture(void) {
    const std::string path = "/tmp/suspended_spheres_sine.anim.json";
    std::remove(path.c_str());
    std::string cmd =
        "python3 tests/fixtures/write_sine.py " + path + " >/dev/null 2>&1";
    ASSERT(system(cmd.c_str()) == 0);

    pc::AnimFile f;
    std::string err;
    ASSERT(pc::parse_anim_file(path, &f, &err));
    ASSERT(f.version == 1 && f.cols == 10 && f.rows == 12);
    ASSERT(f.layers.size() == 1 && f.layers[0].op == pc::OP_SINE);

    ASSERT(!pc::parse_anim_file("tests/fixtures/write_sine.py", &f, &err));
    ASSERT(!err.empty());
    return 0;
}

/* ------------------------------------------------------------------ */
/*  A9 — 120-ball sine inside the ordinary tier                        */
/* ------------------------------------------------------------------ */

static int test_a9_120_ball(void) {
    pc::AnimFile f;
    ASSERT(pc::parse_anim_file("patterns/golden_sine.anim.json", &f, nullptr));
    ASSERT(f.cols * f.rows == 120);
    pc::Trajectory t = pc::bake(f);
    pc::CheckResult r = pc::check(f, t);
    ASSERT(r.ok());
    for (int32_t s : t.samples)
        ASSERT(s >= 0 && s <= 2000000);
    ASSERT(mc_bus_load_percent_with_overhead(f.n, f.dt_ms) < 60);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  A10 — wire and microstep fidelity                                  */
/* ------------------------------------------------------------------ */

static int test_a10_fidelity(void) {
    pc::AnimFile f;
    ASSERT(pc::parse_anim_file("patterns/golden_sine.anim.json", &f, nullptr));
    pc::Trajectory t = pc::bake(f);

    int max_wire = 0;
    int max_step = 0;
    int step_changes = 0;
    for (int32_t s : t.samples) {
        int32_t wire = pc::wire_sample_um(s);
        ASSERT(wire == mc_code_to_um(mc_um_to_code(s)));
        int e = abs_i32(s - wire);
        if (e > max_wire) max_wire = e;

        int32_t step = pc::step_sample_um(s);
        ASSERT(step == mc_steps_to_um(mc_um_to_steps(step)));
        int es = abs_i32(s - step);
        if (es > max_step) max_step = es;
        if (step != s) ++step_changes;
    }
    ASSERT(max_wire <= 15300);   /* half a 30.5 µm wire code */
    ASSERT(max_step <= 20000);   /* half a 39.3 µm microstep */
    ASSERT(step_changes > 0);
    return 0;
}

static int reg_a1 = add_test("A1", test_a1_schema);
static int reg_a2 = add_test("A2", test_a2_deterministic);
static int reg_a3 = add_test("A3", test_a3_layers);
static int reg_a4 = add_test("A4", test_a4_groups);
static int reg_a5 = add_test("A5", test_a5_limits_and_report);
static int reg_a6 = add_test("A6", test_a6_hbp);
static int reg_a7 = add_test("A7", test_a7_hbp_replay);
static int reg_a8 = add_test("A8", test_a8_python_fixture);
static int reg_a9 = add_test("A9", test_a9_120_ball);
static int reg_a10 = add_test("A10", test_a10_fidelity);
