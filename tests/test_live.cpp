/* test_live.cpp — §11 items L1–L6 of motion_streaming_tdd.md.
 *
 * Live .anim.json state with a fake clock: no window, no WebGL, no
 * operating-system clock, and no file-system watcher thread.
 */

#include "assert.h"
#include "pattern_core/pattern_core.h"
#include "test_list.h"

#include <string>

static const char *kName = "show.anim.json";

static std::string anim_json(int amplitude_um, int dt_ms = 10, int n = 3,
                             int duration_ms = 1000) {
    return std::string("{\"version\":1,\"grid\":[10,10],\"dt_ms\":") +
           std::to_string(dt_ms) + ",\"n\":" + std::to_string(n) +
           ",\"duration_ms\":" + std::to_string(duration_ms) +
           ",\"tier\":\"ordinary\",\"layers\":[{\"op\":\"sine\","
           "\"amplitude_um\":" + std::to_string(amplitude_um) +
           ",\"period_ms\":5000,\"wavelength_cols\":10,\"axis\":\"col\"}]}";
}

static bool load(pc::LiveReload &live, int64_t now_us,
                 const std::string &json) {
    live.on_change(now_us, kName, json);
    return live.poll(now_us + pc::LiveReload::kDebounceUs);
}

/* L1 — one bake per debounce window. */
static int test_l1_debounce(void) {
    pc::LiveReload live;
    live.on_change(0, kName, anim_json(50000));
    ASSERT(live.pending());
    ASSERT(!live.poll(49999));
    ASSERT(!live.has_trajectory());
    ASSERT(live.poll(50000));
    ASSERT(live.has_trajectory());
    ASSERT(!live.pending());

    pc::LiveReload burst;
    burst.on_change(100000, kName, anim_json(50000));
    burst.on_change(120000, kName, anim_json(70000));
    ASSERT(!burst.poll(149999));
    ASSERT(burst.poll(150000));
    ASSERT(burst.file().layers[0].amplitude_um == 70000);
    ASSERT(!burst.poll(200000));

    /* A write with unchanged content is not a content change. */
    burst.on_change(300000, kName, anim_json(70000));
    ASSERT(!burst.pending());
    ASSERT(!burst.poll(400000));
    return 0;
}

/* L2 — playhead, selection and camera survive a reload. */
static int test_l2_state_preserved(void) {
    pc::LiveReload live;
    ASSERT(load(live, 0, anim_json(50000)));

    live.playhead_ms = 300;
    live.selected_ball = 7;
    live.camera.x_mm = 1200;
    live.camera.y_mm = 2300;
    live.camera.z_mm = 3400;
    live.camera.yaw_mdeg = 15000;
    live.camera.pitch_mdeg = -5000;

    const int32_t before = live.position_at(7, live.playhead_ms);
    ASSERT(load(live, 100000, anim_json(90000)));

    ASSERT(live.playhead_ms == 300);
    ASSERT(live.selected_ball == 7);
    ASSERT(live.camera.x_mm == 1200 && live.camera.y_mm == 2300);
    ASSERT(live.camera.z_mm == 3400);
    ASSERT(live.camera.yaw_mdeg == 15000);
    ASSERT(live.camera.pitch_mdeg == -5000);

    const int32_t after = live.position_at(7, live.playhead_ms);
    ASSERT(after != before);
    ASSERT(after == live.trajectory().at(30, 7));   /* 300 ms / 10 ms */
    return 0;
}

/* L3 — a parse error keeps the last good take and report. */
static int test_l3_parse_error(void) {
    pc::LiveReload live;
    ASSERT(load(live, 0, anim_json(1200000)));
    ASSERT(!live.report().ok());
    ASSERT(!live.report().violations.empty());
    const pc::Trajectory good = live.trajectory();

    live.on_change(100000, kName, "{not json");
    ASSERT(!live.poll(150000));
    ASSERT(live.has_trajectory());
    ASSERT(live.trajectory().samples == good.samples);
    ASSERT(!live.report().violations.empty());
    ASSERT(!live.report().parse_error.empty());
    ASSERT(!live.export_enabled());
    return 0;
}

/* L4 — a Δt/N change rebakes the whole take. */
static int test_l4_cadence_change(void) {
    pc::LiveReload live;
    ASSERT(load(live, 0, anim_json(50000, 10, 3, 1000)));
    ASSERT(live.file().dt_ms == 10 && live.file().n == 3);
    ASSERT(live.trajectory().frame_count == 101);

    ASSERT(load(live, 100000, anim_json(50000, 15, 4, 1000)));
    ASSERT(live.file().dt_ms == 15 && live.file().n == 4);
    ASSERT(live.trajectory().dt_ms == 15);
    ASSERT(live.trajectory().n == 4);
    ASSERT(live.trajectory().frame_count == 1000 / 15 + 1);
    ASSERT(live.playhead_ms == 0);
    return 0;
}

/* L5 — export is enabled only by the next clean report. */
static int test_l5_export_gate(void) {
    pc::LiveReload live;
    ASSERT(load(live, 0, anim_json(1200000)));
    ASSERT(!live.export_enabled());

    ASSERT(load(live, 100000, anim_json(50000)));
    ASSERT(live.export_enabled());
    return 0;
}

/* L6 — only .anim.json is an authoring change. */
static int test_l6_ignored_files(void) {
    pc::LiveReload live;
    ASSERT(load(live, 0, anim_json(50000)));
    const pc::Trajectory before = live.trajectory();

    live.on_change(100000, "show.hbp", "PK\003\004not an authoring file");
    ASSERT(!live.pending());
    ASSERT(!live.poll(150000));
    ASSERT(live.trajectory().samples == before.samples);

    live.on_change(200000, "show.report.json", "{\"violations\":[]}");
    ASSERT(!live.pending());
    ASSERT(!live.poll(250000));
    ASSERT(live.trajectory().samples == before.samples);
    return 0;
}

static int reg_l1 = add_test("L1", test_l1_debounce);
static int reg_l2 = add_test("L2", test_l2_state_preserved);
static int reg_l3 = add_test("L3", test_l3_parse_error);
static int reg_l4 = add_test("L4", test_l4_cadence_change);
static int reg_l5 = add_test("L5", test_l5_export_gate);
static int reg_l6 = add_test("L6", test_l6_ignored_files);
