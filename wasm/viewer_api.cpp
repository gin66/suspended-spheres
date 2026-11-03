/* viewer_api.cpp — minimal C ABI between the browser viewer and the
 * shared integer core.  Compiled with Emscripten; no JavaScript bake or
 * limit logic is needed in the viewer. */

#include "motion_core.h"
#include "pattern_core/pattern_core.h"

#include <stdint.h>
#include <string>
#include <vector>

namespace {

struct Entry {
    pc::AnimFile file;
    pc::Trajectory traj;
    pc::CheckResult result;
    std::string report;
};

std::string g_error;
std::vector<Entry *> g_entries;

Entry *entry(int handle) {
    if (handle <= 0 || (size_t)handle > g_entries.size())
        return nullptr;
    return g_entries[(size_t)handle - 1];
}

}  // namespace

extern "C" {

int pc_load_json(const char *text, int bake_it) {
    g_error.clear();
    pc::AnimFile file;
    std::string error;
    if (!pc::parse_anim_json(text ? text : "", &file, &error)) {
        g_error = error.empty() ? "parse error" : error;
        return 0;
    }

    Entry *e = new Entry;
    e->file = std::move(file);
    if (bake_it) {
        e->traj = pc::bake(e->file);
        e->result = pc::check(e->file, e->traj);
        e->report = pc::report_json(e->result);
    } else {
        e->report = "{\"violations\":[]}";
    }

    g_entries.push_back(e);
    return (int)g_entries.size();
}

void pc_free(int handle) {
    Entry *e = entry(handle);
    if (!e)
        return;
    delete e;
    g_entries[(size_t)handle - 1] = nullptr;
}

const char *pc_last_error(void) {
    return g_error.c_str();
}

int pc_grid_cols(int handle) {
    Entry *e = entry(handle);
    return e ? e->file.cols : 0;
}

int pc_grid_rows(int handle) {
    Entry *e = entry(handle);
    return e ? e->file.rows : 0;
}

int pc_dt_ms(int handle) {
    Entry *e = entry(handle);
    return e ? e->file.dt_ms : 0;
}

int pc_n(int handle) {
    Entry *e = entry(handle);
    return e ? e->file.n : 0;
}

int pc_duration_ms(int handle) {
    Entry *e = entry(handle);
    return e ? e->file.duration_ms : 0;
}

int pc_tier(int handle) {
    Entry *e = entry(handle);
    return e ? (int)e->file.tier : 0;
}

int pc_layer_count(int handle) {
    Entry *e = entry(handle);
    return e ? (int)e->file.layers.size() : 0;
}

int pc_layer_op(int handle, int layer) {
    Entry *e = entry(handle);
    if (!e || layer < 0 || (size_t)layer >= e->file.layers.size())
        return -1;
    return (int)e->file.layers[(size_t)layer].op;
}

int pc_layer_axis(int handle, int layer) {
    Entry *e = entry(handle);
    if (!e || layer < 0 || (size_t)layer >= e->file.layers.size())
        return -1;
    return e->file.layers[(size_t)layer].axis;
}

int32_t pc_eval_um(int handle, int ball, int t_ms) {
    Entry *e = entry(handle);
    if (!e)
        return -1;
    return pc::evaluate(e->file, ball, t_ms);
}

const char *pc_report_json(int handle) {
    Entry *e = entry(handle);
    return e ? e->report.c_str() : "{\"violations\":[]}";
}

int pc_bus_load_percent(int handle) {
    Entry *e = entry(handle);
    if (!e)
        return -1;
    return mc_bus_load_percent_with_overhead(e->file.n, e->file.dt_ms);
}

}  // extern "C"
