/* show_hbp.cpp — HBP2 view, CRC and the per-window wire plan. */

#include "show_core/show_core.h"

#include <stdlib.h>
#include <string.h>

namespace {

uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

uint16_t le16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

void set_error(char *error, size_t cap, const char *text) {
    if (error != NULL && cap > 0) {
        if (text == NULL) text = "bad hbp";
        size_t n = strlen(text);
        if (n >= cap) n = cap - 1;
        memcpy(error, text, n);
        error[n] = '\0';
    }
}

/* JSON is only used for the flat HBP2 header.  These helpers do not build a
 * DOM: the writer emits one object with scalar members and a two-element
 * grid array, so a key scan is enough and keeps the node free of a JSON
 * allocation. */
const char *find_key(const char *json, const char *key) {
    char pattern[64];
    const size_t n = strlen(key);
    if (n + 3 > sizeof(pattern)) return NULL;
    pattern[0] = '"';
    memcpy(pattern + 1, key, n);
    pattern[n + 1] = '"';
    pattern[n + 2] = '\0';
    const char *p = strstr(json, pattern);
    if (p == NULL) return NULL;
    p += n + 2;
    while (*p == ' ') ++p;
    if (*p != ':') return NULL;
    ++p;
    while (*p == ' ') ++p;
    return p;
}

bool parse_i64(const char *p, int64_t *out) {
    if (p == NULL) return false;
    char *end = NULL;
    long long v = strtoll(p, &end, 10);
    if (end == p) return false;
    *out = (int64_t)v;
    return true;
}

bool get_i64(const char *json, const char *key, int64_t *out) {
    return parse_i64(find_key(json, key), out);
}

bool get_string(const char *json, const char *key, char *out, size_t cap) {
    const char *p = find_key(json, key);
    if (p == NULL || *p != '"') return false;
    ++p;
    const char *end = strchr(p, '"');
    if (end == NULL) return false;
    size_t n = (size_t)(end - p);
    if (n >= cap) n = cap - 1;
    memcpy(out, p, n);
    out[n] = '\0';
    return true;
}

bool get_grid(const char *json, int *cols, int *rows) {
    const char *p = find_key(json, "grid");
    if (p == NULL || *p != '[') return false;
    ++p;
    char *end = NULL;
    long c = strtol(p, &end, 10);
    if (end == p) return false;
    p = end;
    while (*p == ' ') ++p;
    if (*p != ',') return false;
    ++p;
    long r = strtol(p, &end, 10);
    if (end == p) return false;
    if (c <= 0 || r <= 0 || c * r > SHOW_MAX_POSITIONS) return false;
    *cols = (int)c;
    *rows = (int)r;
    return true;
}

uint32_t crc_table[256];
bool crc_ready = false;

void crc_init(void) {
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (int k = 0; k < 8; ++k)
            c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_table[i] = c;
    }
    crc_ready = true;
}

}  // namespace

uint32_t show_crc32(const uint8_t *data, size_t len) {
    if (!crc_ready) crc_init();
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i)
        c = crc_table[(c ^ data[i]) & 0xFFu] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

int show_hbp_parse(const uint8_t *data, size_t size, show_hbp_t *out,
                   char *error, size_t error_cap) {
    if (out == NULL || data == NULL) {
        set_error(error, error_cap, "null hbp");
        return -1;
    }
    memset(out, 0, sizeof(*out));
    if (size < 16 || memcmp(data, "HBP2", 4) != 0) {
        set_error(error, error_cap, "not an HBP2 file");
        return -1;
    }
    if (le16(data + 4) != 2) {
        set_error(error, error_cap, "unsupported HBP2 version");
        return -1;
    }
    const size_t hlen = le16(data + 6);
    if (hlen == 0 || hlen > 2048 || 8 + hlen + 4 > size) {
        set_error(error, error_cap, "truncated HBP2 header");
        return -1;
    }

    char header[2049];
    memcpy(header, data + 8, hlen);
    header[hlen] = '\0';

    int64_t version = 0, dt = 0, n = 0, window_ms = 0, window_count = 0;
    int64_t frame_count = 0, duration = 0, block_windows = 0, playback = 0;
    if (!get_i64(header, "version", &version) || version != 2 ||
        !get_i64(header, "dt_ms", &dt) || dt <= 0 ||
        !get_i64(header, "n", &n) || n <= 0 ||
        !get_i64(header, "window_ms", &window_ms) || window_ms <= 0 ||
        !get_i64(header, "window_count", &window_count) || window_count <= 0 ||
        !get_i64(header, "frame_count", &frame_count) ||
        !get_i64(header, "duration_ms", &duration) ||
        !get_i64(header, "block_windows", &block_windows) ||
        block_windows <= 0) {
        set_error(error, error_cap, "bad HBP2 header fields");
        return -1;
    }
    (void)get_i64(header, "playback_ms", &playback);

    char codec[16];
    if (!get_string(header, "codec", codec, sizeof(codec)) ||
        strcmp(codec, "none") != 0) {
        set_error(error, error_cap, "unsupported HBP2 codec");
        return -1;
    }

    int cols = 0, rows = 0;
    if (!get_grid(header, &cols, &rows)) {
        set_error(error, error_cap, "bad HBP2 grid");
        return -1;
    }
    const int balls = cols * rows;
    if (window_ms != n * dt) {
        set_error(error, error_cap, "window_ms does not match n·dt_ms");
        return -1;
    }
    if (frame_count != window_count * balls) {
        set_error(error, error_cap, "HBP2 frame count mismatch");
        return -1;
    }

    const size_t count_pos = 8 + hlen;
    const uint32_t block_count = le32(data + count_pos);
    const size_t table_pos = count_pos + 4;
    if (block_count == 0 ||
        table_pos + (size_t)block_count * 24 > size) {
        set_error(error, error_cap, "truncated HBP2 block table");
        return -1;
    }

    const size_t body_start = table_pos + (size_t)block_count * 24;
    size_t expect_offset = body_start;
    int64_t expect_window = 0;
    for (uint32_t b = 0; b < block_count; ++b) {
        const uint8_t *e = data + table_pos + (size_t)b * 24;
        const uint32_t w0 = le32(e + 0);
        const uint32_t wc = le32(e + 4);
        const uint32_t raw = le32(e + 8);
        const uint32_t stored = le32(e + 12);
        const uint32_t crc = le32(e + 16);
        const uint32_t offset = le32(e + 20);
        if (wc == 0 || raw != wc * (uint32_t)balls * 8u || stored != raw) {
            set_error(error, error_cap, "bad HBP2 block size");
            return -1;
        }
        if ((int64_t)w0 != expect_window) {
            set_error(error, error_cap, "HBP2 block out of order");
            return -1;
        }
        if (offset != expect_offset || (size_t)offset + raw > size) {
            set_error(error, error_cap, "HBP2 block offset out of range");
            return -1;
        }
        if (show_crc32(data + offset, raw) != crc) {
            set_error(error, error_cap, "HBP2 block CRC mismatch");
            return -1;
        }
        expect_window += wc;
        expect_offset += raw;
    }
    if (expect_window != window_count ||
        expect_offset - body_start != (size_t)frame_count * 8) {
        set_error(error, error_cap, "HBP2 block table does not cover the body");
        return -1;
    }

    out->data = data;
    out->size = size;
    out->frames = data + body_start;
    out->frame_bytes = (size_t)frame_count * 8;
    out->cols = cols;
    out->rows = rows;
    out->balls = balls;
    out->dt_ms = (int)dt;
    out->n = (int)n;
    out->window_ms = (int)window_ms;
    out->window_count = (int)window_count;
    out->frame_count = (int)frame_count;
    out->block_count = (int)block_count;
    out->duration_ms = (uint32_t)duration;
    out->playback_ms = (uint32_t)playback;
    return 0;
}

int show_hbp_window_ms(const show_hbp_t *hbp) {
    return hbp != NULL ? hbp->window_ms : 0;
}

int show_hbp_frame_count(const show_hbp_t *hbp) {
    return hbp != NULL ? hbp->frame_count : 0;
}

int show_hbp_segment(const show_hbp_t *hbp, int window, int ball,
                     mc_segment_t *seg) {
    if (hbp == NULL || seg == NULL || window < 0 || ball < 0 ||
        window >= hbp->window_count || ball >= hbp->balls)
        return -1;
    const size_t idx = (size_t)window * (size_t)hbp->balls + (size_t)ball;
    if ((idx + 1) * 8 > hbp->frame_bytes) return -1;
    const uint8_t *p = hbp->frames + idx * 8;
    seg->p0 = le16(p + 0);
    seg->v0 = (int16_t)le16(p + 2);
    seg->a0 = (int16_t)le16(p + 4);
    seg->j = (int16_t)le16(p + 6);
    return 0;
}

int show_hbp_loopable(const show_hbp_t *hbp, int32_t position_tol_um,
                      int32_t speed_tol_um_s) {
    if (hbp == NULL || hbp->window_count < 1 || hbp->balls < 1) return 0;
    if (position_tol_um < 0) position_tol_um = 0;
    if (speed_tol_um_s < 0) speed_tol_um_s = 0;
    const int64_t tseg = (int64_t)hbp->window_ms * 1000;
    for (int ball = 0; ball < hbp->balls; ++ball) {
        mc_segment_t first, last;
        if (show_hbp_segment(hbp, 0, ball, &first) != 0) return 0;
        if (show_hbp_segment(hbp, hbp->window_count - 1, ball, &last) != 0)
            return 0;
        const int32_t start_um = mc_code_to_um(first.p0);
        const int32_t end_um = mc_segment_eval_um(&last, tseg);
        const int32_t end_v = mc_segment_speed_um_s(&last, tseg);
        const int32_t start_v = mc_speed_decode(first.v0);
        const int32_t dp = end_um - start_um;
        const int32_t dv = end_v - start_v;
        if (dp > position_tol_um || dp < -position_tol_um) return 0;
        if (dv > speed_tol_um_s || dv < -speed_tol_um_s) return 0;
    }
    return 1;
}

int show_plan_window(const show_hbp_t *hbp, int window, int64_t t_start_us,
                     uint8_t seq, show_frame_t *out, int cap) {
    if (hbp == NULL || out == NULL || window < 0 ||
        window >= hbp->window_count || cap < 1 + hbp->balls)
        return -1;

    mc_sync_t sync;
    sync.t_us_low = (uint32_t)t_start_us;
    sync.dt_us = (uint16_t)(hbp->dt_ms * 1000);
    sync.n = (uint8_t)hbp->n;
    sync.seq = seq;

    memset(&out[0], 0, sizeof(out[0]));
    out[0].id = (uint16_t)mc_id_make(0, 2);
    if (mc_pack_sync(&sync, out[0].data) != 0) return -1;

    for (int ball = 0; ball < hbp->balls; ++ball) {
        const int field = mc_position_field(ball);
        if (field < 0) return -1;
        const int id = mc_id_make(field, 0);
        if (id < 0) return -1;
        mc_segment_t seg;
        if (show_hbp_segment(hbp, window, ball, &seg) != 0) return -1;
        memset(&out[1 + ball], 0, sizeof(out[1 + ball]));
        out[1 + ball].id = (uint16_t)id;
        if (mc_pack_frame(0, &seg, out[1 + ball].data) != 0) return -1;
    }
    return 1 + hbp->balls;
}
