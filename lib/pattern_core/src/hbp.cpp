/* hbp.cpp — HBP2 playback container: precomputed wire frames for the
 * installation master.  One file, block table, optional codec field. */

#include "pattern_core/hbp.h"
#include "json_internal.h"
#include "pattern_core/limits.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>
#include <vector>

namespace pc {
namespace {

using internal::JValue;
using internal::JsonParser;
using internal::get_int;
using internal::get_string;

const int BLOCK_WINDOWS = 64;
const size_t BLOCK_ENTRY_BYTES = 24;

/* ------------------------------------------------------------------ */
/*  SHA-256 and CRC32                                                 */
/* ------------------------------------------------------------------ */

uint32_t crc32_calc(const uint8_t *data, size_t len) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        init = true;
    }
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i)
        c = table[(c ^ data[i]) & 0xFFu] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

struct Sha256 {
    uint32_t h[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                     0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
    uint8_t buf[64];
    size_t buf_len = 0;
    uint64_t total = 0;

    static uint32_t rotr(uint32_t x, int n) {
        return (x >> n) | (x << (32 - n));
    }

    void block(const uint8_t *p) {
        static const uint32_t k[64] = {
            0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
            0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
            0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
            0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
            0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
            0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
            0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
            0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u};
        uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = ((uint32_t)p[i*4] << 24) | ((uint32_t)p[i*4+1] << 16) |
                   ((uint32_t)p[i*4+2] << 8) | p[i*4+3];
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = rotr(w[i-15],7) ^ rotr(w[i-15],18) ^ (w[i-15] >> 3);
            uint32_t s1 = rotr(w[i-2],17) ^ rotr(w[i-2],19) ^ (w[i-2] >> 10);
            w[i] = w[i-16] + s0 + w[i-7] + s1;
        }
        uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
        for (int i = 0; i < 64; ++i) {
            uint32_t S1 = rotr(e,6) ^ rotr(e,11) ^ rotr(e,25);
            uint32_t ch = (e & f) ^ ((~e) & g);
            uint32_t t1 = hh + S1 + ch + k[i] + w[i];
            uint32_t S0 = rotr(a,2) ^ rotr(a,13) ^ rotr(a,22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t t2 = S0 + maj;
            hh=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
        }
        h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
    }

    void update(const uint8_t *data, size_t len) {
        total += len;
        while (len) {
            size_t n = 64 - buf_len;
            if (n > len) n = len;
            std::memcpy(buf + buf_len, data, n);
            buf_len += n;
            data += n;
            len -= n;
            if (buf_len == 64) {
                block(buf);
                buf_len = 0;
            }
        }
    }

    void final(uint8_t out[32]) {
        uint64_t bits = total * 8;
        uint8_t pad = 0x80;
        update(&pad, 1);
        uint8_t zero = 0;
        while (buf_len != 56)
            update(&zero, 1);
        uint8_t lenb[8];
        for (int i = 0; i < 8; ++i)
            lenb[i] = (uint8_t)(bits >> (56 - i * 8));
        update(lenb, 8);
        for (int i = 0; i < 8; ++i) {
            out[i*4] = (uint8_t)(h[i] >> 24);
            out[i*4+1] = (uint8_t)(h[i] >> 16);
            out[i*4+2] = (uint8_t)(h[i] >> 8);
            out[i*4+3] = (uint8_t)h[i];
        }
    }
};

/* ------------------------------------------------------------------ */
/*  Little-endian helpers                                             */
/* ------------------------------------------------------------------ */

void put16(std::vector<uint8_t> &v, uint16_t x) {
    v.push_back((uint8_t)x);
    v.push_back((uint8_t)(x >> 8));
}
void put32(std::vector<uint8_t> &v, uint32_t x) {
    v.push_back((uint8_t)x);
    v.push_back((uint8_t)(x >> 8));
    v.push_back((uint8_t)(x >> 16));
    v.push_back((uint8_t)(x >> 24));
}
uint16_t get16(const std::vector<uint8_t> &v, size_t p) {
    return (uint16_t)(v[p] | (v[p+1] << 8));
}
uint32_t get32(const std::vector<uint8_t> &v, size_t p) {
    return (uint32_t)v[p] | ((uint32_t)v[p+1] << 8) |
           ((uint32_t)v[p+2] << 16) | ((uint32_t)v[p+3] << 24);
}

/* ------------------------------------------------------------------ */
/*  Frame planning                                                    */
/* ------------------------------------------------------------------ */

int32_t velocity_um_s(const Trajectory &t, int frame, int ball) {
    const int dt_us = t.dt_ms * 1000;
    if (t.frame_count <= 1 || dt_us <= 0)
        return 0;
    if (frame <= 0)
        return (int32_t)(((int64_t)t.at(1, ball) - t.at(0, ball)) *
                         1000000 / dt_us);
    if (frame >= t.frame_count - 1)
        return (int32_t)(((int64_t)t.at(t.frame_count - 1, ball) -
                          t.at(t.frame_count - 2, ball)) *
                         1000000 / dt_us);
    return (int32_t)(((int64_t)t.at(frame + 1, ball) -
                      t.at(frame - 1, ball)) *
                     1000000 / (2 * dt_us));
}

bool quantize_i16(double value, const char *name,
                  int64_t min_value, int64_t max_value,
                  std::string *error, int16_t *out) {
    double rounded = std::round(value);
    if (rounded < (double)min_value || rounded > (double)max_value) {
        if (error) *error = std::string(name) + " out of wire range";
        return false;
    }
    *out = (int16_t)rounded;
    return true;
}

}  // namespace

std::string sha256_hex(const uint8_t *data, size_t size) {
    Sha256 s;
    s.update(data, size);
    uint8_t d[32];
    s.final(d);
    static const char *hex = "0123456789abcdef";
    std::string out;
    for (int i = 0; i < 32; ++i) {
        out.push_back(hex[d[i] >> 4]);
        out.push_back(hex[d[i] & 15]);
    }
    return out;
}

bool make_wire_frames(const AnimFile &file, const Trajectory &traj,
                      std::vector<mc_segment_t> *frames, int *window_ms,
                      std::string *error) {
    if (error) error->clear();
    if (!frames || !window_ms) {
        if (error) *error = "null output";
        return false;
    }
    frames->clear();

    const int balls = file.cols * file.rows;
    if (balls <= 0 || file.n <= 0 || file.dt_ms <= 0 || file.n > 120 ||
        traj.frame_count < 2 ||
        (int)traj.samples.size() < balls * traj.frame_count) {
        if (error) *error = "trajectory does not match the file";
        return false;
    }

    const int wms = file.n * file.dt_ms;
    const int64_t t_us = (int64_t)wms * 1000;
    const int windows = (traj.frame_count + file.n - 2) / file.n;
    if (windows <= 0) {
        if (error) *error = "empty trajectory";
        return false;
    }

    std::vector<mc_segment_t> prev((size_t)balls);
    frames->reserve((size_t)windows * balls);
    const double T = (double)wms / 1000.0;

    for (int w = 0; w < windows; ++w) {
        const int start = w * file.n;
        int end = start + file.n;
        if (end >= traj.frame_count)
            end = traj.frame_count - 1;

        for (int b = 0; b < balls; ++b) {
            int32_t p0_um;
            int32_t v0_um_s;
            if (w == 0) {
                p0_um = traj.at(start, b);
                v0_um_s = velocity_um_s(traj, start, b);
            } else {
                p0_um = mc_segment_eval_um(&prev[(size_t)b], t_us);
                v0_um_s = mc_segment_speed_um_s(&prev[(size_t)b], t_us);
            }
            const bool partial = (end - start) < file.n;
            const int32_t p1_um = traj.at(end, b);
            /* A short final window still lasts N·Δt on the wire.  Finish
             * that window at rest instead of forcing the interior velocity
             * into a shorter distance. */
            const int32_t v1_um_s = partial ? 0 : velocity_um_s(traj, end, b);
            if (p0_um < 0 || p0_um > 2000000 ||
                p1_um < 0 || p1_um > 2000000) {
                if (error) *error = "frame leaves the travel";
                return false;
            }

            const double dp = (double)p1_um - p0_um;
            const double v0 = v0_um_s;
            const double v1 = v1_um_s;
            const double a0_um = 6.0 * dp / (T * T) - (4.0 * v0 + 2.0 * v1) / T;
            const double j_um = 2.0 * ((v1 - v0) - a0_um * T) / (T * T);

            mc_segment_t seg;
            seg.p0 = mc_um_to_code(p0_um);
            if (v0_um_s < -1310720 || v0_um_s > 1310719) {
                if (error) *error = "speed out of wire range";
                return false;
            }
            seg.v0 = mc_speed_encode(v0_um_s);
            if (!quantize_i16(a0_um / 1000.0, "acceleration",
                              -32768, 32767, error, &seg.a0))
                return false;
            if (!quantize_i16(j_um / 1000000.0, "jerk",
                              -32768, 32767, error, &seg.j))
                return false;

            const int64_t a0_um_s2 = (int64_t)seg.a0 * 1000;
            const int64_t a_end_um_s2 = a0_um_s2 + (int64_t)seg.j * t_us;
            if (a0_um_s2 < -9810000 || a_end_um_s2 < -9810000) {
                if (error) *error = "downward acceleration at window edge";
                return false;
            }

            const int32_t p_end = mc_segment_eval_um(&seg, t_us);
            if (p_end < 0 || p_end > 2000000) {
                if (error) *error = "frame leaves the travel";
                return false;
            }

            frames->push_back(seg);
            prev[(size_t)b] = seg;
        }
    }

    *window_ms = wms;
    return true;
}

bool write_hbp(const std::string &path, const AnimFile &file,
               const Trajectory &traj, std::string *error) {
    CheckResult r = check(file, traj);
    if (!r.ok()) {
        if (error) *error = "report is not clean";
        return false;
    }

    std::vector<mc_segment_t> frames;
    int window_ms = 0;
    if (!make_wire_frames(file, traj, &frames, &window_ms, error))
        return false;

    const int balls = file.cols * file.rows;
    const int frame_count = (int)frames.size();
    const int window_count = balls > 0 ? frame_count / balls : 0;
    if (window_count <= 0) {
        if (error) *error = "no playback windows";
        return false;
    }

    std::vector<uint8_t> raw;
    raw.reserve((size_t)frames.size() * 8);
    for (const mc_segment_t &f : frames) {
        put16(raw, f.p0);
        put16(raw, (uint16_t)f.v0);
        put16(raw, (uint16_t)f.a0);
        put16(raw, (uint16_t)f.j);
    }
    const std::string sha = sha256_hex(raw.data(), raw.size());

    const int block_count = (window_count + BLOCK_WINDOWS - 1) / BLOCK_WINDOWS;
    std::ostringstream h;
    h << "{"
      << "\"version\":2,"
      << "\"grid\":[" << file.cols << "," << file.rows << "],"
      << "\"dt_ms\":" << file.dt_ms << ","
      << "\"n\":" << file.n << ","
      << "\"window_ms\":" << window_ms << ","
      << "\"window_count\":" << window_count << ","
      << "\"frame_count\":" << frame_count << ","
      << "\"duration_ms\":" << file.duration_ms << ","
      << "\"playback_ms\":" << window_count * window_ms << ","
      << "\"tier\":" << (int)file.tier << ","
      << "\"units\":\"wire\","
      << "\"layout\":\"window-major\","
      << "\"wire\":\"p0u16_v0s16_a0s16_js16\","
      << "\"codec\":\"none\","
      << "\"block_windows\":" << BLOCK_WINDOWS << ","
      << "\"calibration\":{\"steps_per_mm_q16\":"
      << MC_STEPS_PER_MM_Q16 << "},"
      << "\"sha256\":\"" << sha << "\""
      << "}";
    const std::string header = h.str();
    if (header.size() > 0xFFFF) {
        if (error) *error = "header too large";
        return false;
    }

    const size_t table_bytes = (size_t)block_count * BLOCK_ENTRY_BYTES;
    const size_t data_start = 8 + header.size() + 4 + table_bytes;

    std::vector<uint8_t> out;
    out.reserve(data_start + raw.size());
    out.push_back('H'); out.push_back('B'); out.push_back('P');
    out.push_back('2');
    put16(out, 2);
    put16(out, (uint16_t)header.size());
    out.insert(out.end(), header.begin(), header.end());
    put32(out, (uint32_t)block_count);

    size_t data_offset = data_start;
    for (int b = 0; b < block_count; ++b) {
        const int w0 = b * BLOCK_WINDOWS;
        const int wc = std::min(BLOCK_WINDOWS, window_count - w0);
        const uint32_t raw_size = (uint32_t)wc * balls * 8;
        const uint32_t crc = crc32_calc(raw.data() + (size_t)w0 * balls * 8,
                                        raw_size);
        put32(out, (uint32_t)w0);
        put32(out, (uint32_t)wc);
        put32(out, raw_size);
        put32(out, raw_size);          /* stored size (codec none) */
        put32(out, crc);
        put32(out, (uint32_t)data_offset);
        data_offset += raw_size;
    }
    out.insert(out.end(), raw.begin(), raw.end());

    std::ofstream f(path, std::ios::binary);
    if (!f) {
        if (error) *error = "cannot open output";
        return false;
    }
    f.write((const char *)out.data(), (std::streamsize)out.size());
    return (bool)f;
}

bool read_hbp(const std::string &path, Playback *playback,
              std::string *error) {
    if (!playback) {
        if (error) *error = "null playback";
        return false;
    }
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        if (error) *error = "cannot open hbp";
        return false;
    }
    std::vector<uint8_t> buf((std::istreambuf_iterator<char>(f)),
                             std::istreambuf_iterator<char>());
    if (buf.size() < 16 || buf[0] != 'H' || buf[1] != 'B' ||
        buf[2] != 'P' || buf[3] != '2') {
        if (error) *error = "not an HBP2 file";
        return false;
    }
    if (get16(buf, 4) != 2) {
        if (error) *error = "unsupported hbp version";
        return false;
    }
    const size_t header_len = get16(buf, 6);
    if (8 + header_len + 4 > buf.size()) {
        if (error) *error = "truncated hbp header";
        return false;
    }

    std::string header((const char *)buf.data() + 8, header_len);
    JValue root;
    JsonParser p(header);
    if (!p.parse_value(&root, 0) || !p.error.empty() ||
        root.type != JValue::OBJ) {
        if (error) *error = "bad hbp header";
        return false;
    }

    int64_t version = 0, dt = 0, n = 0, window_ms = 0, window_count = 0;
    int64_t frame_count = 0, duration = 0, tier = 0;
    std::string codec, sha;
    const JValue *grid = root.get("grid");
    if (!grid || grid->type != JValue::ARR || grid->a.size() != 2 ||
        !get_int(root, "version", &version, error, true) || version != 2 ||
        !get_int(root, "dt_ms", &dt, error, true) ||
        !get_int(root, "n", &n, error, true) ||
        !get_int(root, "window_ms", &window_ms, error, true) ||
        !get_int(root, "window_count", &window_count, error, true) ||
        !get_int(root, "frame_count", &frame_count, error, true) ||
        !get_int(root, "duration_ms", &duration, error, true) ||
        !get_int(root, "tier", &tier, error, true) ||
        !get_string(root, "codec", &codec, error, true) ||
        !get_string(root, "sha256", &sha, error, true)) {
        if (error && error->empty()) *error = "missing hbp field";
        return false;
    }
    if (codec != "none") {
        if (error) *error = "unsupported hbp codec";
        return false;
    }

    const int cols = (int)grid->a[0].i;
    const int rows = (int)grid->a[1].i;
    const int balls = cols * rows;
    if (balls <= 0 || window_count <= 0 ||
        frame_count != window_count * balls) {
        if (error) *error = "hbp geometry mismatch";
        return false;
    }

    const size_t block_count_pos = 8 + header_len;
    const uint32_t block_count = get32(buf, block_count_pos);
    const size_t table_pos = block_count_pos + 4;
    if (table_pos + (size_t)block_count * BLOCK_ENTRY_BYTES > buf.size()) {
        if (error) *error = "truncated hbp table";
        return false;
    }

    std::vector<uint8_t> raw;
    raw.reserve((size_t)frame_count * 8);
    for (uint32_t i = 0; i < block_count; ++i) {
        const size_t e = table_pos + (size_t)i * BLOCK_ENTRY_BYTES;
        const uint32_t raw_size = get32(buf, e + 8);
        const uint32_t stored_size = get32(buf, e + 12);
        const uint32_t crc = get32(buf, e + 16);
        const uint32_t offset = get32(buf, e + 20);
        if (stored_size != raw_size || offset + stored_size > buf.size()) {
            if (error) *error = "bad hbp block";
            return false;
        }
        if (crc32_calc(buf.data() + offset, stored_size) != crc) {
            if (error) *error = "hbp block CRC mismatch";
            return false;
        }
        raw.insert(raw.end(), buf.begin() + offset,
                   buf.begin() + offset + stored_size);
    }
    if (raw.size() != (size_t)frame_count * 8) {
        if (error) *error = "hbp frame count mismatch";
        return false;
    }
    if (sha256_hex(raw.data(), raw.size()) != sha) {
        if (error) *error = "hbp sha256 mismatch";
        return false;
    }

    Playback out;
    out.file.version = 2;
    out.file.cols = cols;
    out.file.rows = rows;
    out.file.dt_ms = (int)dt;
    out.file.n = (int)n;
    out.file.duration_ms = (int)duration;
    out.file.tier = (Tier)tier;
    out.window_ms = (int)window_ms;
    out.window_count = (int)window_count;
    out.codec = codec;
    out.frames.resize((size_t)frame_count);
    for (size_t i = 0; i < out.frames.size(); ++i) {
        const uint8_t *p = raw.data() + i * 8;
        out.frames[i].p0 = (uint16_t)(p[0] | (p[1] << 8));
        out.frames[i].v0 = (int16_t)(p[2] | (p[3] << 8));
        out.frames[i].a0 = (int16_t)(p[4] | (p[5] << 8));
        out.frames[i].j = (int16_t)(p[6] | (p[7] << 8));
    }

    *playback = std::move(out);
    return true;
}

}  // namespace pc
