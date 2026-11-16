/*
 * release_core.cpp — release identity, canonical manifest and SHA-256.
 * No filesystem, network, wall clock or git (firmware_release_tdd.md R1).
 */

#include "release_core.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  SHA-256                                                            */
/* ------------------------------------------------------------------ */

static const uint32_t sha_k[64] = {
    0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,
    0x923f82a4u,0xab1c5ed5u,0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,
    0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,0xe49b69c1u,0xefbe4786u,
    0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
    0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,
    0x06ca6351u,0x14292967u,0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,
    0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,0xa2bfe8a1u,0xa81a664bu,
    0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
    0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,
    0x5b9cca4fu,0x682e6ff3u,0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,
    0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
};

#define ROR32(x,n) (((x) >> (n)) | ((x) << (32 - (n))))

static void sha_transform(rc_sha256_ctx_t *ctx, const uint8_t block[64]) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i)
        w[i] = ((uint32_t)block[i * 4] << 24) |
               ((uint32_t)block[i * 4 + 1] << 16) |
               ((uint32_t)block[i * 4 + 2] << 8) |
               (uint32_t)block[i * 4 + 3];
    for (int i = 16; i < 64; ++i) {
        uint32_t s0 = ROR32(w[i - 15], 7) ^ ROR32(w[i - 15], 18) ^
                      (w[i - 15] >> 3);
        uint32_t s1 = ROR32(w[i - 2], 17) ^ ROR32(w[i - 2], 19) ^
                      (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = ctx->state[0], b = ctx->state[1], c = ctx->state[2];
    uint32_t d = ctx->state[3], e = ctx->state[4], f = ctx->state[5];
    uint32_t g = ctx->state[6], h = ctx->state[7];
    for (int i = 0; i < 64; ++i) {
        uint32_t s1 = ROR32(e, 6) ^ ROR32(e, 11) ^ ROR32(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = h + s1 + ch + sha_k[i] + w[i];
        uint32_t s0 = ROR32(a, 2) ^ ROR32(a, 13) ^ ROR32(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = s0 + maj;
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c;
    ctx->state[3] += d; ctx->state[4] += e; ctx->state[5] += f;
    ctx->state[6] += g; ctx->state[7] += h;
}

void rc_sha256_init(rc_sha256_ctx_t *ctx) {
    static const uint32_t init[8] = {
        0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,
        0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u
    };
    for (int i = 0; i < 8; ++i)
        ctx->state[i] = init[i];
    ctx->bitlen = 0;
    ctx->buflen = 0;
}

void rc_sha256_update(rc_sha256_ctx_t *ctx, const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    ctx->bitlen += (uint64_t)len * 8u;
    while (len > 0) {
        uint32_t room = 64u - ctx->buflen;
        uint32_t take = (len < room) ? (uint32_t)len : room;
        memcpy(ctx->buf + ctx->buflen, p, take);
        ctx->buflen += take;
        p += take;
        len -= take;
        if (ctx->buflen == 64) {
            sha_transform(ctx, ctx->buf);
            ctx->buflen = 0;
        }
    }
}

void rc_sha256_final(rc_sha256_ctx_t *ctx, uint8_t out[32]) {
    uint64_t bitlen = ctx->bitlen;
    uint8_t pad = 0x80;
    rc_sha256_update(ctx, &pad, 1);
    uint8_t zero = 0;
    while (ctx->buflen != 56)
        rc_sha256_update(ctx, &zero, 1);
    uint8_t len_be[8];
    for (int i = 0; i < 8; ++i)
        len_be[i] = (uint8_t)(bitlen >> (56 - 8 * i));
    rc_sha256_update(ctx, len_be, 8);
    for (int i = 0; i < 8; ++i) {
        out[i * 4] = (uint8_t)(ctx->state[i] >> 24);
        out[i * 4 + 1] = (uint8_t)(ctx->state[i] >> 16);
        out[i * 4 + 2] = (uint8_t)(ctx->state[i] >> 8);
        out[i * 4 + 3] = (uint8_t)ctx->state[i];
    }
}

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int rc_hex_is_sha256(const char *s) {
    if (s == 0 || strlen(s) != 64)
        return 0;
    for (int i = 0; i < 64; ++i)
        if (hexval(s[i]) < 0)
            return 0;
    return 1;
}

int rc_sha256_hex(const void *data, size_t len, char out[65]) {
    rc_sha256_ctx_t ctx;
    uint8_t digest[32];
    static const char hex[] = "0123456789abcdef";
    if (out == 0)
        return -1;
    rc_sha256_init(&ctx);
    rc_sha256_update(&ctx, data, len);
    rc_sha256_final(&ctx, digest);
    for (int i = 0; i < 32; ++i) {
        out[i * 2] = hex[digest[i] >> 4];
        out[i * 2 + 1] = hex[digest[i] & 0x0F];
    }
    out[64] = '\0';
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Version and build id                                               */
/* ------------------------------------------------------------------ */

static int parse_component(const char **pp, uint8_t *out) {
    const char *p = *pp;
    if (*p < '0' || *p > '9')
        return -1;
    if (p[0] == '0' && p[1] >= '0' && p[1] <= '9')
        return -1;                       /* leading zero */
    uint32_t v = 0;
    while (*p >= '0' && *p <= '9') {
        v = v * 10u + (uint32_t)(*p - '0');
        if (v > 255u)
            return -1;
        ++p;
    }
    *out = (uint8_t)v;
    *pp = p;
    return 0;
}

static int version_body_parse(const char **pp, rc_version_t *out) {
    const char *p = *pp;
    rc_version_t v;
    if (parse_component(&p, &v.major) < 0) return -1;
    if (*p++ != '.') return -1;
    if (parse_component(&p, &v.minor) < 0) return -1;
    if (*p++ != '.') return -1;
    if (parse_component(&p, &v.patch) < 0) return -1;
    *out = v;
    *pp = p;
    return 0;
}

int rc_semver_parse(const char *tag, rc_version_t *out) {
    if (tag == 0 || out == 0 || tag[0] != 'v')
        return -1;
    const char *p = tag + 1;
    rc_version_t v;
    if (version_body_parse(&p, &v) < 0)
        return -1;
    if (*p != '\0')
        return -1;                       /* suffix, prerelease, trailing text */
    *out = v;
    return 0;
}

int rc_semver_format(const rc_version_t *v, char out[16]) {
    if (v == 0 || out == 0)
        return -1;
    int n = snprintf(out, 16, "%u.%u.%u",
                     (unsigned)v->major, (unsigned)v->minor,
                     (unsigned)v->patch);
    return (n > 0 && n < 16) ? 0 : -1;
}

int rc_build_id_accept(uint32_t new_id, uint32_t previous_id) {
    if (new_id == 0)
        return 0;
    if (previous_id == 0)
        return 1;
    return new_id > previous_id ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/*  Time and filename                                                  */
/* ------------------------------------------------------------------ */

/* Days from 1970-01-01 to a civil date (Howard Hinnant's algorithm). */
static int64_t days_from_civil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153u * (m + (m > 2 ? -3 : 9)) + 2u) / 5u + d - 1u;
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

static void civil_from_days(int64_t z, int *y, unsigned *m, unsigned *d) {
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = (unsigned)(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) /
                         365u;
    const int64_t yy = (int64_t)yoe + era * 400;
    const unsigned doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
    const unsigned mp = (5u * doy + 2u) / 153u;
    const unsigned dd = doy - (153u * mp + 2u) / 5u + 1u;
    const unsigned mm = mp + (mp < 10u ? 3u : -9u);
    *y = (int)(yy + (mm <= 2u));
    *m = mm;
    *d = dd;
}

static void split_time(uint32_t epoch_s, int *y, unsigned *mo, unsigned *d,
                       unsigned *h, unsigned *mi, unsigned *s) {
    int64_t days = (int64_t)epoch_s / 86400;
    uint32_t rem = (uint32_t)((int64_t)epoch_s - days * 86400);
    civil_from_days(days, y, mo, d);
    *h = rem / 3600u;
    *mi = (rem / 60u) % 60u;
    *s = rem % 60u;
}

int rc_utc_compact(uint32_t epoch_s, char out[17]) {
    int y; unsigned mo, d, h, mi, s;
    if (out == 0)
        return -1;
    split_time(epoch_s, &y, &mo, &d, &h, &mi, &s);
    snprintf(out, 17, "%04d%02u%02uT%02u%02u%02uZ", y, mo, d, h, mi, s);
    return 0;
}

int rc_utc_iso(uint32_t epoch_s, char out[21]) {
    int y; unsigned mo, d, h, mi, s;
    if (out == 0)
        return -1;
    split_time(epoch_s, &y, &mo, &d, &h, &mi, &s);
    snprintf(out, 21, "%04d-%02u-%02uT%02u:%02u:%02uZ", y, mo, d, h, mi, s);
    return 0;
}

static int dirty_safe_char(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
}

int rc_make_filename(const char *target, const char *kind, uint32_t commit_time,
                     const rc_version_t *version, const char *git, int dirty,
                     char *out, size_t cap) {
    char utc[17];
    char git8[9];
    char ver[16];
    if (target == 0 || kind == 0 || version == 0 || git == 0 || out == 0)
        return -1;
    for (const char *p = target; *p; ++p)
        if (!dirty_safe_char(*p))
            return -1;
    if (strcmp(kind, "app") != 0 && strcmp(kind, "factory") != 0)
        return -1;
    for (int i = 0; i < 8; ++i) {
        if (hexval(git[i]) < 0)
            return -1;
        git8[i] = git[i];
    }
    git8[8] = '\0';
    if (rc_utc_compact(commit_time, utc) < 0)
        return -1;
    if (rc_semver_format(version, ver) < 0)
        return -1;
    int n = snprintf(out, cap, "%s_%s_v%s_%s_%s%s.bin",
                     target, utc, ver, git8, kind, dirty ? "_dirty" : "");
    return (n > 0 && (size_t)n < cap) ? 0 : -1;
}

/* ------------------------------------------------------------------ */
/*  A small flat JSON reader                                           */
/* ------------------------------------------------------------------ */

typedef struct {
    const char *p;
} jcur_t;

static void j_ws(jcur_t *j) {
    while (*j->p == ' ' || *j->p == '\t' || *j->p == '\r' || *j->p == '\n')
        ++j->p;
}

static int j_expect(jcur_t *j, char c) {
    j_ws(j);
    if (*j->p != c)
        return -1;
    ++j->p;
    return 0;
}

static int j_string(jcur_t *j, char *out, size_t cap) {
    j_ws(j);
    if (*j->p != '"')
        return -1;
    ++j->p;
    size_t n = 0;
    while (*j->p != '"') {
        char c = *j->p++;
        if (c == '\0')
            return -1;
        if (c == '\\') {
            char e = *j->p++;
            switch (e) {
            case '"': c = '"'; break;
            case '\\': c = '\\'; break;
            case '/': c = '/'; break;
            case 'n': c = '\n'; break;
            case 't': c = '\t'; break;
            case 'r': c = '\r'; break;
            default: return -1;
            }
        }
        if (out != 0) {
            if (n + 1 >= cap)
                return -1;
            out[n] = c;
        }
        ++n;
    }
    ++j->p;
    if (out != 0)
        out[n] = '\0';
    return 0;
}

static int j_u64(jcur_t *j, uint64_t *out) {
    j_ws(j);
    if (*j->p < '0' || *j->p > '9')
        return -1;
    uint64_t v = 0;
    while (*j->p >= '0' && *j->p <= '9') {
        v = v * 10u + (uint64_t)(*j->p - '0');
        ++j->p;
    }
    *out = v;
    return 0;
}

static int j_bool(jcur_t *j, int *out) {
    j_ws(j);
    if (strncmp(j->p, "true", 4) == 0) {
        *out = 1;
        j->p += 4;
        return 0;
    }
    if (strncmp(j->p, "false", 5) == 0) {
        *out = 0;
        j->p += 5;
        return 0;
    }
    return -1;
}

static int j_skip_value(jcur_t *j) {
    j_ws(j);
    if (*j->p == '"')
        return j_string(j, 0, 0);
    if (*j->p == 't' || *j->p == 'f') {
        int b;
        return j_bool(j, &b);
    }
    if (*j->p == '{' || *j->p == '[') {
        char open = *j->p, close = (open == '{') ? '}' : ']';
        int depth = 0;
        while (*j->p) {
            if (*j->p == open) ++depth;
            else if (*j->p == close) {
                --depth;
                if (depth == 0) { ++j->p; return 0; }
            }
            ++j->p;
        }
        return -1;
    }
    uint64_t v;
    return j_u64(j, &v);
}

/* ------------------------------------------------------------------ */
/*  Manifest                                                           */
/* ------------------------------------------------------------------ */

static void str_copy(char *dst, size_t cap, const char *src) {
    if (cap == 0)
        return;
    size_t i = 0;
    for (; src[i] && i + 1 < cap; ++i)
        dst[i] = src[i];
    dst[i] = '\0';
}

void rc_manifest_init(rc_manifest_t *m, const char *target, const char *kind,
                      const rc_version_t *version, uint32_t build,
                      uint32_t commit_time, const char *git, int dirty,
                      const char *file, uint32_t size, const char *sha256) {
    memset(m, 0, sizeof(*m));
    m->schema = RC_SCHEMA;
    str_copy(m->target, sizeof(m->target), target ? target : "");
    str_copy(m->kind, sizeof(m->kind), kind ? kind : "");
    if (version)
        m->version = *version;
    m->build = build;
    rc_utc_iso(commit_time, m->utc);
    str_copy(m->git, sizeof(m->git), git ? git : "");
    m->dirty = dirty ? 1 : 0;
    str_copy(m->file, sizeof(m->file), file ? file : "");
    m->size = size;
    str_copy(m->sha256, sizeof(m->sha256), sha256 ? sha256 : "");
    if (target && strcmp(target, RC_TARGET_FLY) == 0) {
        m->load_address = RC_LOAD_ADDRESS_FLY;
        str_copy(m->mcu, sizeof(m->mcu), RC_MCU_FLY);
        str_copy(m->katapult_min, sizeof(m->katapult_min), "1.0.0");
    } else {
        str_copy(m->chip, sizeof(m->chip), RC_CHIP_PANEL);
        m->flash_size = RC_FLASH_SIZE_PANEL;
        str_copy(m->slot, sizeof(m->slot), "app");
        m->rollback = 1;
    }
}

typedef struct {
    char *out;
    size_t cap;
    size_t n;
    int fail;
} sbuf_t;

static void sb_put(sbuf_t *b, const char *s) {
    if (b->fail)
        return;
    size_t len = strlen(s);
    if (b->n + len + 1 > b->cap) {
        b->fail = 1;
        return;
    }
    memcpy(b->out + b->n, s, len + 1);
    b->n += len;
}

static void sb_num(sbuf_t *b, uint64_t v) {
    char tmp[32];
    snprintf(tmp, sizeof(tmp), "%llu", (unsigned long long)v);
    sb_put(b, tmp);
}

static void sb_str(sbuf_t *b, const char *key, const char *val) {
    sb_put(b, ",\"");
    sb_put(b, key);
    sb_put(b, "\":\"");
    sb_put(b, val);
    sb_put(b, "\"");
}

static void sb_u64(sbuf_t *b, const char *key, uint64_t val) {
    sb_put(b, ",\"");
    sb_put(b, key);
    sb_put(b, "\":");
    sb_num(b, val);
}

static void sb_bool(sbuf_t *b, const char *key, int val) {
    sb_put(b, ",\"");
    sb_put(b, key);
    sb_put(b, val ? "\":true" : "\":false");
}

int rc_manifest_emit(const rc_manifest_t *m, char *out, size_t cap) {
    char ver[16];
    if (m == 0 || out == 0 || cap == 0)
        return -1;
    if (rc_semver_format(&m->version, ver) < 0)
        return -1;
    sbuf_t b = {out, cap, 0, 0};
    sb_put(&b, "{\"schema\":");
    sb_num(&b, (uint64_t)m->schema);
    sb_str(&b, "target", m->target);
    sb_str(&b, "kind", m->kind);
    sb_str(&b, "version", ver);
    sb_u64(&b, "build", m->build);
    sb_str(&b, "utc", m->utc);
    sb_str(&b, "git", m->git);
    sb_bool(&b, "dirty", m->dirty);
    sb_str(&b, "file", m->file);
    sb_u64(&b, "size", m->size);
    sb_str(&b, "sha256", m->sha256);
    if (strcmp(m->target, RC_TARGET_FLY) == 0) {
        sb_u64(&b, "load_address", m->load_address);
        sb_str(&b, "mcu", m->mcu);
        sb_str(&b, "katapult_min", m->katapult_min);
    } else {
        sb_str(&b, "chip", m->chip);
        sb_u64(&b, "flash_size", m->flash_size);
        sb_str(&b, "partition_table_sha256", m->partition_table_sha256);
        sb_str(&b, "slot", m->slot);
        sb_bool(&b, "rollback", m->rollback);
    }
    sb_put(&b, "}");
    return b.fail ? -1 : 0;
}

static int manifest_common_ok(const rc_manifest_t *m) {
    if (m->schema != RC_SCHEMA)
        return 0;
    int fly = strcmp(m->target, RC_TARGET_FLY) == 0;
    int panel = strcmp(m->target, RC_TARGET_PANEL) == 0;
    if (!fly && !panel)
        return 0;
    if (strcmp(m->kind, "app") != 0 && strcmp(m->kind, "factory") != 0)
        return 0;
    if (fly && strcmp(m->kind, "factory") == 0)
        return 0;
    if (m->build == 0)
        return 0;
    if (m->dirty)
        return 0;
    if (!rc_hex_is_sha256(m->sha256))
        return 0;
    if (m->file[0] == '\0')
        return 0;
    if (fly)
        return m->load_address == RC_LOAD_ADDRESS_FLY &&
               strcmp(m->mcu, RC_MCU_FLY) == 0;
    return m->flash_size == RC_FLASH_SIZE_PANEL &&
           strcmp(m->chip, RC_CHIP_PANEL) == 0 &&
           rc_hex_is_sha256(m->partition_table_sha256);
}

int rc_manifest_parse(const char *text, rc_manifest_t *m) {
    if (text == 0 || m == 0)
        return -1;
    memset(m, 0, sizeof(*m));
    jcur_t j = {text};
    if (j_expect(&j, '{') < 0)
        return -1;
    int seen_version = 0, seen_build = 0;
    for (;;) {
        j_ws(&j);
        if (*j.p == '}') {
            ++j.p;
            break;
        }
        char key[40];
        if (j_string(&j, key, sizeof(key)) < 0)
            return -1;
        if (j_expect(&j, ':') < 0)
            return -1;
        j_ws(&j);
        if (strcmp(key, "schema") == 0) {
            uint64_t v;
            if (j_u64(&j, &v) < 0) return -1;
            m->schema = (int)v;
        } else if (strcmp(key, "build") == 0) {
            uint64_t v;
            if (j_u64(&j, &v) < 0 || v > 0xFFFFFFFFull) return -1;
            m->build = (uint32_t)v;
            seen_build = 1;
        } else if (strcmp(key, "size") == 0) {
            uint64_t v;
            if (j_u64(&j, &v) < 0 || v > 0xFFFFFFFFull) return -1;
            m->size = (uint32_t)v;
        } else if (strcmp(key, "load_address") == 0) {
            if (j_u64(&j, &m->load_address) < 0) return -1;
        } else if (strcmp(key, "flash_size") == 0) {
            if (j_u64(&j, &m->flash_size) < 0) return -1;
        } else if (strcmp(key, "dirty") == 0) {
            if (j_bool(&j, &m->dirty) < 0) return -1;
        } else if (strcmp(key, "rollback") == 0) {
            if (j_bool(&j, &m->rollback) < 0) return -1;
        } else if (strcmp(key, "target") == 0) {
            if (j_string(&j, m->target, sizeof(m->target)) < 0) return -1;
        } else if (strcmp(key, "kind") == 0) {
            if (j_string(&j, m->kind, sizeof(m->kind)) < 0) return -1;
        } else if (strcmp(key, "version") == 0) {
            char v[16];
            if (j_string(&j, v, sizeof(v)) < 0) return -1;
            const char *p = v;
            if (version_body_parse(&p, &m->version) < 0 || *p != '\0')
                return -1;
            seen_version = 1;
        } else if (strcmp(key, "utc") == 0) {
            if (j_string(&j, m->utc, sizeof(m->utc)) < 0) return -1;
        } else if (strcmp(key, "git") == 0) {
            if (j_string(&j, m->git, sizeof(m->git)) < 0) return -1;
        } else if (strcmp(key, "file") == 0) {
            if (j_string(&j, m->file, sizeof(m->file)) < 0) return -1;
        } else if (strcmp(key, "sha256") == 0) {
            if (j_string(&j, m->sha256, sizeof(m->sha256)) < 0) return -1;
        } else if (strcmp(key, "mcu") == 0) {
            if (j_string(&j, m->mcu, sizeof(m->mcu)) < 0) return -1;
        } else if (strcmp(key, "katapult_min") == 0) {
            if (j_string(&j, m->katapult_min,
                         sizeof(m->katapult_min)) < 0) return -1;
        } else if (strcmp(key, "chip") == 0) {
            if (j_string(&j, m->chip, sizeof(m->chip)) < 0) return -1;
        } else if (strcmp(key, "partition_table_sha256") == 0) {
            if (j_string(&j, m->partition_table_sha256,
                         sizeof(m->partition_table_sha256)) < 0) return -1;
        } else if (strcmp(key, "slot") == 0) {
            if (j_string(&j, m->slot, sizeof(m->slot)) < 0) return -1;
        } else {
            if (j_skip_value(&j) < 0) return -1;
        }
        j_ws(&j);
        if (*j.p == ',') {
            ++j.p;
            continue;
        }
        if (*j.p == '}') {
            ++j.p;
            break;
        }
        return -1;
    }
    j_ws(&j);
    if (*j.p != '\0')
        return -1;
    if (!seen_version || !seen_build)
        return -1;
    return manifest_common_ok(m) ? 0 : -1;
}

int rc_manifest_ota_ok(const rc_manifest_t *m) {
    if (m == 0 || !manifest_common_ok(m))
        return 0;
    return strcmp(m->target, RC_TARGET_PANEL) == 0 &&
           strcmp(m->kind, "app") == 0;
}

int rc_manifest_katapult_ok(const rc_manifest_t *m) {
    if (m == 0 || !manifest_common_ok(m))
        return 0;
    return strcmp(m->target, RC_TARGET_FLY) == 0 &&
           strcmp(m->kind, "app") == 0;
}

int rc_select_highest(const rc_manifest_t *const *list, int count,
                      int panel_ota, const rc_manifest_t **out) {
    if (out)
        *out = 0;
    if (list == 0 || count <= 0)
        return 0;
    uint32_t best = 0;
    int found = 0;
    for (int i = 0; i < count; ++i) {
        const rc_manifest_t *m = list[i];
        int ok = panel_ota ? rc_manifest_ota_ok(m) : rc_manifest_katapult_ok(m);
        if (!ok)
            continue;
        if (!found || m->build > best) {
            best = m->build;
            found = 1;
        }
    }
    if (!found)
        return 0;
    const rc_manifest_t *pick = 0;
    for (int i = 0; i < count; ++i) {
        const rc_manifest_t *m = list[i];
        int ok = panel_ota ? rc_manifest_ota_ok(m) : rc_manifest_katapult_ok(m);
        if (!ok || m->build != best)
            continue;
        if (pick == 0) {
            pick = m;
        } else if (strcmp(pick->sha256, m->sha256) != 0) {
            return -1;                   /* same build, two different files */
        }
    }
    if (out)
        *out = pick;
    return 1;
}

/* ------------------------------------------------------------------ */
/*  release.json                                                       */
/* ------------------------------------------------------------------ */

void rc_release_init(rc_release_t *r, const char *tag, uint32_t published_time) {
    memset(r, 0, sizeof(*r));
    r->schema = RC_SCHEMA;
    str_copy(r->tag, sizeof(r->tag), tag ? tag : "");
    rc_utc_iso(published_time, r->published);
}

int rc_release_add_asset(rc_release_t *r, const char *target,
                         const char *name) {
    rc_release_target_t *t;
    if (r == 0 || target == 0 || name == 0)
        return -1;
    if (strcmp(target, RC_TARGET_FLY) == 0)
        t = &r->fly;
    else if (strcmp(target, RC_TARGET_PANEL) == 0)
        t = &r->panel;
    else
        return -1;
    if (t->asset_count >= RC_MAX_RELEASE_ASSETS)
        return -1;
    str_copy(t->assets[t->asset_count], 128, name);
    ++t->asset_count;
    t->present = 1;
    return 0;
}

static int ascii_ieq(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        char ca = a[i], cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca != cb)
            return 0;
    }
    return 1;
}

static int asset_name_ok(const char *target, const char *name) {
    size_t tl = strlen(target);
    if (strncmp(name, target, tl) != 0 || name[tl] != '_')
        return 0;
    size_t n = strlen(name);
    if (n > 4 && strcmp(name + n - 4, ".uf2") == 0)
        return 0;
    for (size_t i = 0; i + 8 <= n; ++i)
        if (ascii_ieq(name + i, "katapult", 8))
            return 0;
    return 1;
}

int rc_release_validate(const rc_release_t *r) {
    if (r == 0 || r->schema != RC_SCHEMA)
        return 0;
    rc_version_t v;
    if (rc_semver_parse(r->tag, &v) < 0)
        return 0;
    if (!r->fly.present || !r->panel.present)
        return 0;
    if (r->fly.build == 0 || r->fly.build != r->panel.build)
        return 0;
    char a[16], b[16];
    if (rc_semver_format(&r->fly.version, a) < 0 ||
        rc_semver_format(&r->panel.version, b) < 0)
        return 0;
    if (strcmp(a, b) != 0)
        return 0;
    if (r->fly.asset_count == 0 || r->panel.asset_count == 0)
        return 0;
    for (int i = 0; i < r->fly.asset_count; ++i) {
        if (!asset_name_ok(RC_TARGET_FLY, r->fly.assets[i]))
            return 0;
        if (strstr(r->fly.assets[i], "_factory.") != 0)
            return 0;
    }
    for (int i = 0; i < r->panel.asset_count; ++i)
        if (!asset_name_ok(RC_TARGET_PANEL, r->panel.assets[i]))
            return 0;
    return 1;
}

static void sb_target(sbuf_t *b, const char *name,
                      const rc_release_target_t *t) {
    char ver[16];
    rc_semver_format(&t->version, ver);
    sb_put(b, ",\"");
    sb_put(b, name);
    sb_put(b, "\":{\"version\":\"");
    sb_put(b, ver);
    sb_put(b, "\",\"build\":");
    sb_num(b, t->build);
    sb_put(b, ",\"assets\":[");
    for (int i = 0; i < t->asset_count; ++i) {
        if (i)
            sb_put(b, ",");
        sb_put(b, "\"");
        sb_put(b, t->assets[i]);
        sb_put(b, "\"");
    }
    sb_put(b, "]}");
}

int rc_release_emit(const rc_release_t *r, char *out, size_t cap) {
    if (r == 0 || out == 0 || cap == 0)
        return -1;
    sbuf_t b = {out, cap, 0, 0};
    sb_put(&b, "{\"schema\":");
    sb_num(&b, (uint64_t)r->schema);
    sb_str(&b, "tag", r->tag);
    sb_str(&b, "published", r->published);
    sb_put(&b, ",\"targets\":{");
    int first = 1;
    if (r->fly.present) {
        /* sb_target writes a leading comma; strip it for the first target. */
        char tmp[1024];
        sbuf_t t = {tmp, sizeof(tmp), 0, 0};
        sb_target(&t, RC_TARGET_FLY, &r->fly);
        if (t.fail)
            return -1;
        sb_put(&b, tmp + 1);
        first = 0;
    }
    if (r->panel.present) {
        char tmp[1024];
        sbuf_t t = {tmp, sizeof(tmp), 0, 0};
        sb_target(&t, RC_TARGET_PANEL, &r->panel);
        if (t.fail)
            return -1;
        sb_put(&b, first ? tmp + 1 : tmp);
    }
    sb_put(&b, "}}");
    return b.fail ? -1 : 0;
}

static int parse_assets(jcur_t *j, rc_release_target_t *t) {
    if (j_expect(j, '[') < 0)
        return -1;
    for (;;) {
        j_ws(j);
        if (*j->p == ']') {
            ++j->p;
            return 0;
        }
        char name[128];
        if (j_string(j, name, sizeof(name)) < 0)
            return -1;
        if (t->asset_count >= RC_MAX_RELEASE_ASSETS)
            return -1;
        str_copy(t->assets[t->asset_count++], 128, name);
        j_ws(j);
        if (*j->p == ',') { ++j->p; continue; }
        if (*j->p == ']') { ++j->p; return 0; }
        return -1;
    }
}

static int parse_target(jcur_t *j, rc_release_target_t *t) {
    memset(t, 0, sizeof(*t));
    t->present = 1;
    if (j_expect(j, '{') < 0)
        return -1;
    for (;;) {
        j_ws(j);
        if (*j->p == '}') { ++j->p; return 0; }
        char key[32];
        if (j_string(j, key, sizeof(key)) < 0 || j_expect(j, ':') < 0)
            return -1;
        j_ws(j);
        if (strcmp(key, "version") == 0) {
            char v[16];
            if (j_string(j, v, sizeof(v)) < 0)
                return -1;
            const char *p = v;
            if (version_body_parse(&p, &t->version) < 0 || *p != '\0')
                return -1;
        } else if (strcmp(key, "build") == 0) {
            uint64_t v;
            if (j_u64(j, &v) < 0 || v > 0xFFFFFFFFull)
                return -1;
            t->build = (uint32_t)v;
        } else if (strcmp(key, "assets") == 0) {
            if (parse_assets(j, t) < 0)
                return -1;
        } else {
            if (j_skip_value(j) < 0)
                return -1;
        }
        j_ws(j);
        if (*j->p == ',') { ++j->p; continue; }
        if (*j->p == '}') { ++j->p; return 0; }
        return -1;
    }
}

int rc_release_parse(const char *text, rc_release_t *r) {
    if (text == 0 || r == 0)
        return -1;
    memset(r, 0, sizeof(*r));
    jcur_t j = {text};
    if (j_expect(&j, '{') < 0)
        return -1;
    for (;;) {
        j_ws(&j);
        if (*j.p == '}') { ++j.p; break; }
        char key[32];
        if (j_string(&j, key, sizeof(key)) < 0 || j_expect(&j, ':') < 0)
            return -1;
        j_ws(&j);
        if (strcmp(key, "schema") == 0) {
            uint64_t v;
            if (j_u64(&j, &v) < 0) return -1;
            r->schema = (int)v;
        } else if (strcmp(key, "tag") == 0) {
            if (j_string(&j, r->tag, sizeof(r->tag)) < 0) return -1;
        } else if (strcmp(key, "published") == 0) {
            if (j_string(&j, r->published, sizeof(r->published)) < 0) return -1;
        } else if (strcmp(key, "targets") == 0) {
            if (j_expect(&j, '{') < 0) return -1;
            for (;;) {
                j_ws(&j);
                if (*j.p == '}') { ++j.p; break; }
                char tname[32];
                if (j_string(&j, tname, sizeof(tname)) < 0 ||
                    j_expect(&j, ':') < 0) return -1;
                if (strcmp(tname, RC_TARGET_FLY) == 0) {
                    if (parse_target(&j, &r->fly) < 0) return -1;
                } else if (strcmp(tname, RC_TARGET_PANEL) == 0) {
                    if (parse_target(&j, &r->panel) < 0) return -1;
                } else if (j_skip_value(&j) < 0) {
                    return -1;
                }
                j_ws(&j);
                if (*j.p == ',') { ++j.p; continue; }
                if (*j.p == '}') { ++j.p; break; }
                return -1;
            }
        } else {
            if (j_skip_value(&j) < 0) return -1;
        }
        j_ws(&j);
        if (*j.p == ',') { ++j.p; continue; }
        if (*j.p == '}') { ++j.p; break; }
        return -1;
    }
    j_ws(&j);
    if (*j.p != '\0')
        return -1;
    return rc_release_validate(r) ? 0 : -1;
}
