/* json.cpp — .anim.json parsing and the .report.json writer. */

#include "pattern_core/json.h"
#include "json_internal.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <sstream>

namespace pc {

using internal::JValue;
using internal::JsonParser;
using internal::get_int;
using internal::get_string;

namespace {

bool parse_tier(const std::string &s, Tier *t) {
    if (s == "ordinary") { *t = TIER_ORDINARY; return true; }
    if (s == "nominal") { *t = TIER_NOMINAL; return true; }
    if (s == "optional") { *t = TIER_OPTIONAL; return true; }
    return false;
}

bool parse_op(const std::string &s, Op *op) {
    if (s == "keyframes") { *op = OP_KEYFRAMES; return true; }
    if (s == "sine") { *op = OP_SINE; return true; }
    if (s == "travelling_wave") { *op = OP_TRAVELLING_WAVE; return true; }
    if (s == "interference") { *op = OP_INTERFERENCE; return true; }
    if (s == "spiral") { *op = OP_SPIRAL; return true; }
    if (s == "ripple") { *op = OP_RIPPLE; return true; }
    if (s == "cascade") { *op = OP_CASCADE; return true; }
    if (s == "random_walk") { *op = OP_RANDOM_WALK; return true; }
    return false;
}

void parse_group(const JValue &v, Group *g) {
    std::string kind;
    if (!get_string(v, "kind", &kind, nullptr, false))
        return;
    if (kind == "row") g->kind = GROUP_ROW;
    else if (kind == "column") g->kind = GROUP_COLUMN;
    else if (kind == "ring") g->kind = GROUP_RING;
    else if (kind == "checkerboard") g->kind = GROUP_CHECKERBOARD;
    else if (kind == "mask") g->kind = GROUP_MASK;
    else g->kind = GROUP_ALL;

    int64_t i = 0;
    if (get_int(v, "index", &i, nullptr, false)) g->index = (int)i;
    if (get_int(v, "radius", &i, nullptr, false)) g->radius = (int)i;
    if (get_int(v, "parity", &i, nullptr, false)) g->parity = (int)i;
    const JValue *c = v.get("center");
    if (c && c->type == JValue::ARR && c->a.size() == 2) {
        if (c->a[0].type == JValue::INT) g->center_col = (int)c->a[0].i;
        if (c->a[1].type == JValue::INT) g->center_row = (int)c->a[1].i;
    }
    const JValue *b = v.get("balls");
    if (b && b->type == JValue::ARR) {
        for (const auto &e : b->a)
            if (e.type == JValue::INT)
                g->balls.push_back((int)e.i);
    }
}

bool parse_layer(const JValue &v, Layer *layer, std::string *error) {
    std::string op;
    if (!get_string(v, "op", &op, error, true))
        return false;
    if (!parse_op(op, &layer->op)) {
        if (error) *error = "unknown layer op: " + op;
        return false;
    }

    int64_t i = 0;
    if (get_int(v, "amplitude_um", &i, nullptr, false)) layer->amplitude_um = (int32_t)i;
    if (get_int(v, "period_ms", &i, nullptr, false)) layer->period_ms = (int32_t)i;
    if (get_int(v, "wavelength_cols", &i, nullptr, false)) layer->wavelength_cols = (int32_t)i;
    if (get_int(v, "phase_deg", &i, nullptr, false)) layer->phase_deg = (int32_t)i;
    if (get_int(v, "direction", &i, nullptr, false)) layer->direction = (int)i;
    if (get_int(v, "seed", &i, nullptr, false)) layer->seed = (uint32_t)i;
    if (get_int(v, "amplitude2_um", &i, nullptr, false)) layer->amplitude2_um = (int32_t)i;
    if (get_int(v, "period2_ms", &i, nullptr, false)) layer->period2_ms = (int32_t)i;
    if (get_int(v, "wavelength2_cols", &i, nullptr, false)) layer->wavelength2_cols = (int32_t)i;
    if (get_int(v, "phase2_deg", &i, nullptr, false)) layer->phase2_deg = (int32_t)i;

    std::string axis;
    if (get_string(v, "axis", &axis, nullptr, false))
        layer->axis = (axis == "row") ? 1 : 0;
    if (get_string(v, "axis2", &axis, nullptr, false))
        layer->axis2 = (axis == "row") ? 1 : 0;

    const JValue *keys = v.get("keys");
    if (keys) {
        if (keys->type != JValue::ARR) {
            if (error) *error = "keys must be an array";
            return false;
        }
        for (const auto &k : keys->a) {
            if (k.type != JValue::OBJ) {
                if (error) *error = "keyframe must be an object";
                return false;
            }
            int64_t t = 0, p = 0;
            if (!get_int(k, "t_ms", &t, error, true) ||
                !get_int(k, "pos_um", &p, error, true))
                return false;
            layer->keys.push_back({t, (int32_t)p});
        }
        std::sort(layer->keys.begin(), layer->keys.end(),
                  [](const Keyframe &a, const Keyframe &b) {
                      return a.t_ms < b.t_ms;
                  });
    }

    const JValue *g = v.get("group");
    if (g && g->type == JValue::OBJ)
        parse_group(*g, &layer->group);
    return true;
}

/* ------------------------------------------------------------------ */

std::string json_escape(const std::string &s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if ((unsigned char)c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", (unsigned)c);
                out += buf;
            } else {
                out.push_back(c);
            }
        }
    }
    return out;
}

}  // namespace

bool parse_anim_json(const std::string &text, AnimFile *out,
                     std::string *error) {
    JValue root;
    JsonParser p(text);
    if (!p.parse_value(&root, 0) || !p.error.empty()) {
        if (error) *error = p.error.empty() ? "parse error" : p.error;
        return false;
    }
    if (root.type != JValue::OBJ) {
        if (error) *error = "root must be an object";
        return false;
    }

    int64_t version = 0;
    if (!get_int(root, "version", &version, error, true))
        return false;
    if (version != 1) {
        if (error) *error = "unsupported version";
        return false;
    }

    const JValue *grid = root.get("grid");
    if (!grid || grid->type != JValue::ARR || grid->a.size() != 2 ||
        grid->a[0].type != JValue::INT || grid->a[1].type != JValue::INT) {
        if (error) *error = "grid must be [cols, rows]";
        return false;
    }
    int cols = (int)grid->a[0].i;
    int rows = (int)grid->a[1].i;
    if (!((cols == 10 && rows == 10) || (cols == 10 && rows == 12))) {
        if (error) *error = "unsupported grid";
        return false;
    }

    int64_t dt = 0, n = 0, duration = 0;
    if (!get_int(root, "dt_ms", &dt, error, true) ||
        !get_int(root, "n", &n, error, true) ||
        !get_int(root, "duration_ms", &duration, error, true))
        return false;
    if (dt != 10 && dt != 11 && dt != 15) {
        if (error) *error = "unsupported dt_ms";
        return false;
    }
    if (n != 3 && n != 4) {
        if (error) *error = "unsupported n";
        return false;
    }
    if (duration <= 0) {
        if (error) *error = "duration_ms must be positive";
        return false;
    }

    std::string tier;
    if (!get_string(root, "tier", &tier, error, true))
        return false;
    Tier t;
    if (!parse_tier(tier, &t)) {
        if (error) *error = "unknown tier";
        return false;
    }

    const JValue *layers = root.get("layers");
    if (!layers || layers->type != JValue::ARR) {
        if (error) *error = "layers must be an array";
        return false;
    }

    AnimFile f;
    f.version = (int)version;
    f.cols = cols;
    f.rows = rows;
    f.dt_ms = (int)dt;
    f.n = (int)n;
    f.duration_ms = (int)duration;
    f.tier = t;

    int64_t base = 0;
    if (get_int(root, "base_um", &base, nullptr, false))
        f.base_um = (int32_t)base;

    for (const auto &lv : layers->a) {
        if (lv.type != JValue::OBJ) {
            if (error) *error = "layer must be an object";
            return false;
        }
        Layer layer;
        if (!parse_layer(lv, &layer, error))
            return false;
        f.layers.push_back(std::move(layer));
    }

    *out = std::move(f);
    return true;
}

bool parse_anim_file(const std::string &path, AnimFile *out,
                     std::string *error) {
    if (path.size() >= 3 && path.compare(path.size() - 3, 3, ".py") == 0) {
        if (error) *error = "input is a Python generator, not .anim.json";
        return false;
    }
    std::ifstream f(path);
    if (!f) {
        if (error) *error = "cannot open file";
        return false;
    }
    std::string text((std::istreambuf_iterator<char>(f)),
                     std::istreambuf_iterator<char>());
    return parse_anim_json(text, out, error);
}


    std::ostringstream o;
    o << "{";
    if (!result.parse_error.empty())
        o << "\"parse_error\":\"" << json_escape(result.parse_error)
          << "\",";
    o << "\"violations\":[";
    for (size_t i = 0; i < result.violations.size(); ++i) {
        const auto &v = result.violations[i];
        if (i) o << ",";
        o << "{\"ball\":" << v.ball
          << ",\"time_ms\":" << v.time_ms
          << ",\"limit\":\"" << json_escape(v.limit) << "\""
          << ",\"measured\":" << v.measured
          << ",\"allowed\":" << v.allowed << "}";
    }
    o << "]}";
    return o.str();
}

}  // namespace pc
