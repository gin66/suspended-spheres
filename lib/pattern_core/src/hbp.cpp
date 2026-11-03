/* hbp.cpp — manifest, SHA-256, stored ZIP and .hbp export/import. */

#include "pattern_core/hbp.h"
#include "json_internal.h"
#include "pattern_core/limits.h"
#include "motion_core.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>

namespace pc {

using internal::JValue;
using internal::JsonParser;
using internal::get_int;

namespace {

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

std::string manifest_json(const AnimFile &file) {
    std::ostringstream o;
    o << "{"
      << "\"version\":1,"
      << "\"grid\":[" << file.cols << "," << file.rows << "],"
      << "\"units\":\"um\","
      << "\"sample_type\":\"int32\","
      << "\"layout\":\"frame-major\","
      << "\"cadence_ms\":" << file.dt_ms << ","
      << "\"n\":" << file.n << ","
      << "\"duration_ms\":" << file.duration_ms << ","
      << "\"tier\":" << (int)file.tier << ","
      << "\"calibration\":{\"steps_per_mm_q16\":"
      << MC_STEPS_PER_MM_Q16 << "}"
      << "}";
    return o.str();
}

/* ------------------------------------------------------------------ */
/*  CRC32, SHA-256 and a stored-only ZIP writer/reader                 */
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

/* SHA-256, FIPS 180-4. */
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

struct ZipMember {
    std::string name;
    std::vector<uint8_t> data;
};

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

bool write_zip(const std::string &path, const std::vector<ZipMember> &members,
               std::string *error) {
    std::vector<uint8_t> out;
    std::vector<uint32_t> offsets;
    for (const auto &m : members) {
        offsets.push_back((uint32_t)out.size());
        uint32_t crc = crc32_calc(m.data.data(), m.data.size());
        put32(out, 0x04034b50u);
        put16(out, 20); put16(out, 0); put16(out, 0);
        put16(out, 0); put16(out, 0);
        put32(out, crc);
        put32(out, (uint32_t)m.data.size());
        put32(out, (uint32_t)m.data.size());
        put16(out, (uint16_t)m.name.size());
        put16(out, 0);
        out.insert(out.end(), m.name.begin(), m.name.end());
        out.insert(out.end(), m.data.begin(), m.data.end());
    }
    uint32_t cd_start = (uint32_t)out.size();
    for (size_t i = 0; i < members.size(); ++i) {
        const auto &m = members[i];
        uint32_t crc = crc32_calc(m.data.data(), m.data.size());
        put32(out, 0x02014b50u);
        put16(out, 20); put16(out, 20); put16(out, 0); put16(out, 0);
        put16(out, 0); put16(out, 0);
        put32(out, crc);
        put32(out, (uint32_t)m.data.size());
        put32(out, (uint32_t)m.data.size());
        put16(out, (uint16_t)m.name.size());
        put16(out, 0); put16(out, 0);
        put16(out, 0); put16(out, 0);
        put32(out, 0);
        put32(out, offsets[i]);
        out.insert(out.end(), m.name.begin(), m.name.end());
    }
    uint32_t cd_size = (uint32_t)out.size() - cd_start;
    put32(out, 0x06054b50u);
    put16(out, 0); put16(out, 0);
    put16(out, (uint16_t)members.size());
    put16(out, (uint16_t)members.size());
    put32(out, cd_size);
    put32(out, cd_start);
    put16(out, 0);

    std::ofstream f(path, std::ios::binary);
    if (!f) {
        if (error) *error = "cannot open output";
        return false;
    }
    f.write((const char *)out.data(), (std::streamsize)out.size());
    return (bool)f;
}

bool read_zip(const std::string &path, std::vector<ZipMember> *members,
              std::string *error) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        if (error) *error = "cannot open zip";
        return false;
    }
    std::vector<uint8_t> buf((std::istreambuf_iterator<char>(f)),
                             std::istreambuf_iterator<char>());
    if (buf.size() < 22) {
        if (error) *error = "zip too short";
        return false;
    }
    size_t eocd = std::string::npos;
    for (size_t i = buf.size() - 22 + 1; i-- > 0;) {
        if (get32(buf, i) == 0x06054b50u) {
            eocd = i;
            break;
        }
    }
    if (eocd == std::string::npos) {
        if (error) *error = "no zip end record";
        return false;
    }
    uint16_t count = get16(buf, eocd + 10);
    uint32_t cd = get32(buf, eocd + 16);
    size_t p = cd;
    for (uint16_t i = 0; i < count; ++i) {
        if (p + 46 > buf.size() || get32(buf, p) != 0x02014b50u) {
            if (error) *error = "bad central directory";
            return false;
        }
        uint16_t method = get16(buf, p + 10);
        uint32_t csize = get32(buf, p + 20);
        uint16_t nlen = get16(buf, p + 28);
        uint16_t elen = get16(buf, p + 30);
        uint16_t clen = get16(buf, p + 32);
        uint32_t local = get32(buf, p + 42);
        if (method != 0) {
            if (error) *error = "compressed zip entry unsupported";
            return false;
        }
        if (local + 30 > buf.size() || get32(buf, local) != 0x04034b50u) {
            if (error) *error = "bad local header";
            return false;
        }
        uint16_t lnlen = get16(buf, local + 26);
        uint16_t lelen = get16(buf, local + 28);
        size_t data_pos = (size_t)local + 30 + lnlen + lelen;
        if (data_pos + csize > buf.size()) {
            if (error) *error = "truncated zip entry";
            return false;
        }
        ZipMember m;
        m.name.assign((const char *)buf.data() + p + 46, nlen);
        m.data.assign(buf.begin() + data_pos, buf.begin() + data_pos + csize);
        members->push_back(std::move(m));
        p += 46 + nlen + elen + clen;
    }
    return true;
}

std::vector<uint8_t> to_bytes(const std::string &s) {
    return std::vector<uint8_t>(s.begin(), s.end());
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

bool write_hbp(const std::string &path, const AnimFile &file,
               const Trajectory &traj, std::string *error) {
    CheckResult r = check(file, traj);
    if (!r.ok()) {
        if (error) *error = "report is not clean";
        return false;
    }

    std::string manifest = manifest_json(file);
    std::vector<uint8_t> traj_bytes;
    traj_bytes.reserve(traj.samples.size() * 4);
    for (int32_t s : traj.samples) {
        uint32_t u = (uint32_t)s;
        traj_bytes.push_back((uint8_t)u);
        traj_bytes.push_back((uint8_t)(u >> 8));
        traj_bytes.push_back((uint8_t)(u >> 16));
        traj_bytes.push_back((uint8_t)(u >> 24));
    }

    std::string sha_manifest = sha256_hex((const uint8_t *)manifest.data(),
                                          manifest.size());
    std::string sha_traj = sha256_hex(traj_bytes.data(), traj_bytes.size());
    std::string sha = sha_manifest + "  manifest.json\n" +
                      sha_traj + "  trajectory.bin\n";

    std::vector<ZipMember> members;
    members.push_back({"manifest.json", to_bytes(manifest)});
    members.push_back({"trajectory.bin", traj_bytes});
    members.push_back({"sha256.txt", to_bytes(sha)});
    return write_zip(path, members, error);
}

bool read_hbp(const std::string &path, AnimFile *file, Trajectory *traj,
              std::string *error) {
    std::vector<ZipMember> members;
    if (!read_zip(path, &members, error))
        return false;

    const ZipMember *manifest = nullptr;
    const ZipMember *bin = nullptr;
    const ZipMember *sha = nullptr;
    for (const auto &m : members) {
        if (m.name == "manifest.json") manifest = &m;
        else if (m.name == "trajectory.bin") bin = &m;
        else if (m.name == "sha256.txt") sha = &m;
    }
    if (!manifest || !bin || !sha) {
        if (error) *error = "hbp is missing a member";
        return false;
    }

    std::string mtext((const char *)manifest->data.data(),
                      manifest->data.size());
    JValue root;
    JsonParser p(mtext);
    if (!p.parse_value(&root, 0) || !p.error.empty() ||
        root.type != JValue::OBJ) {
        if (error) *error = "bad manifest";
        return false;
    }

    AnimFile f;
    int64_t i = 0;
    const JValue *grid = root.get("grid");
    if (!grid || grid->type != JValue::ARR || grid->a.size() != 2)
        return false;
    f.cols = (int)grid->a[0].i;
    f.rows = (int)grid->a[1].i;
    if (get_int(root, "cadence_ms", &i, nullptr, false)) f.dt_ms = (int)i;
    if (get_int(root, "n", &i, nullptr, false)) f.n = (int)i;
    if (get_int(root, "duration_ms", &i, nullptr, false)) f.duration_ms = (int)i;
    if (get_int(root, "tier", &i, nullptr, false)) f.tier = (Tier)i;

    Trajectory t;
    t.cols = f.cols;
    t.rows = f.rows;
    t.dt_ms = f.dt_ms;
    t.n = f.n;
    t.duration_ms = f.duration_ms;
    t.tier = f.tier;
    t.frame_count = f.duration_ms / f.dt_ms + 1;
    size_t balls = (size_t)f.cols * f.rows;
    size_t need = (size_t)t.frame_count * balls * 4;
    if (bin->data.size() != need) {
        if (error) *error = "trajectory.bin size mismatch";
        return false;
    }
    t.samples.resize((size_t)t.frame_count * balls);
    for (size_t k = 0; k < t.samples.size(); ++k) {
        uint32_t u = get32(bin->data, k * 4);
        t.samples[k] = (int32_t)u;
    }

    *file = f;
    *traj = t;
    return true;
}


}  // namespace pc
