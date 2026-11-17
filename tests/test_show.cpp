/* test_show.cpp — HBP2 view, position plan and node playback (show_core). */

#include "assert.h"
#include "show_core/show_core.h"
#include "test_list.h"

#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

/* ------------------------------------------------------------------ */
/*  S1 — POSMAP payload and position fields                            */
/* ------------------------------------------------------------------ */

static int test_s1_posmap(void) {
    uint8_t p[MC_AXES_PER_NODE] = {0, 63, 119, MC_POS_UNASSIGNED};
    ASSERT(mc_posmap_valid(p) == 1);
    ASSERT(mc_position_field(0) == 0);
    ASSERT(mc_position_field(62) == 62);
    ASSERT(mc_position_field(63) == 64);   /* Katapult 0x3F0 is skipped */
    ASSERT(mc_position_field(119) == 120);
    ASSERT(mc_posmap_motor(p, 0) == 0);
    ASSERT(mc_posmap_motor(p, 64) == 1);
    ASSERT(mc_posmap_motor(p, 120) == 2);
    ASSERT(mc_posmap_motor(p, 63) == -1);
    ASSERT(mc_posmap_motor(p, 1) == -1);

    uint8_t dup[MC_AXES_PER_NODE] = {1, 1, MC_POS_UNASSIGNED, MC_POS_UNASSIGNED};
    ASSERT(mc_posmap_valid(dup) == 0);
    uint8_t bad[MC_AXES_PER_NODE] = {120, MC_POS_UNASSIGNED,
                                     MC_POS_UNASSIGNED, MC_POS_UNASSIGNED};
    ASSERT(mc_posmap_valid(bad) == 0);

    mc_posmap_t m;
    memcpy(m.position, p, sizeof(p));
    m.seq = 9;
    uint8_t buf[8];
    ASSERT(mc_pack_param_posmap(&m, buf) == 0);
    ASSERT(buf[0] == MC_PARAM_POSMAP);
    ASSERT(buf[5] == 9);
    mc_posmap_t out;
    ASSERT(mc_unpack_param_posmap(buf, 8, &out) == 0);
    ASSERT(memcmp(out.position, m.position, sizeof(p)) == 0);
    ASSERT(out.seq == 9);

    buf[0] = 2;
    ASSERT(mc_unpack_param_posmap(buf, 8, &out) == -1);
    ASSERT(mc_pack_param_posmap(&m, 0) == -1);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  A small in-memory HBP2 writer for the plan and node tests          */
/* ------------------------------------------------------------------ */

static void put16(std::vector<uint8_t> &v, unsigned x) {
    v.push_back((uint8_t)(x & 0xFF));
    v.push_back((uint8_t)((x >> 8) & 0xFF));
}

static void put32(std::vector<uint8_t> &v, unsigned x) {
    for (int i = 0; i < 4; ++i) v.push_back((uint8_t)((x >> (8 * i)) & 0xFF));
}

static std::vector<uint8_t> build_hbp(int cols, int rows, int dt_ms, int n,
                                      int windows,
                                      const std::vector<mc_segment_t> &frames) {
    const int balls = cols * rows;
    const int window_ms = n * dt_ms;
    const int frame_count = windows * balls;
    if ((int)frames.size() != frame_count) return std::vector<uint8_t>();

    std::vector<uint8_t> body;
    body.reserve((size_t)frame_count * 8);
    for (int i = 0; i < frame_count; ++i) {
        put16(body, frames[(size_t)i].p0);
        put16(body, (uint16_t)frames[(size_t)i].v0);
        put16(body, (uint16_t)frames[(size_t)i].a0);
        put16(body, (uint16_t)frames[(size_t)i].j);
    }

    char header[384];
    std::snprintf(header, sizeof(header),
                  "{\"version\":2,\"grid\":[%d,%d],\"dt_ms\":%d,\"n\":%d,"
                  "\"window_ms\":%d,\"window_count\":%d,\"frame_count\":%d,"
                  "\"duration_ms\":%d,\"playback_ms\":%d,\"tier\":0,"
                  "\"units\":\"wire\",\"layout\":\"window-major\","
                  "\"wire\":\"p0u16_v0s16_a0s16_js16\",\"codec\":\"none\","
                  "\"block_windows\":64,\"sha256\":\"00\"}",
                  cols, rows, dt_ms, n, window_ms, windows, frame_count,
                  windows * window_ms, windows * window_ms);
    const size_t header_len = strlen(header);

    std::vector<uint8_t> out;
    out.push_back('H');
    out.push_back('B');
    out.push_back('P');
    out.push_back('2');
    put16(out, 2);
    put16(out, (unsigned)header_len);
    out.insert(out.end(), header, header + header_len);
    put32(out, 1);                          /* one block */
    const size_t body_start = out.size() + 24;
    put32(out, 0);                          /* w0 */
    put32(out, (unsigned)windows);          /* wc */
    put32(out, (unsigned)body.size());      /* raw */
    put32(out, (unsigned)body.size());      /* stored */
    put32(out, show_crc32(body.data(), body.size()));
    put32(out, (unsigned)body_start);
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

static mc_segment_t seg_um(int p0_um, int v0_um_s) {
    mc_segment_t s;
    s.p0 = mc_um_to_code(p0_um);
    s.v0 = mc_speed_encode(v0_um_s);
    s.a0 = 0;
    s.j = 0;
    return s;
}

/* ------------------------------------------------------------------ */
/*  S2 — parse a synthetic HBP2 file                                   */
/* ------------------------------------------------------------------ */

static int test_s2_parse(void) {
    std::vector<mc_segment_t> frames;
    for (int w = 0; w < 2; ++w)
        for (int b = 0; b < 4; ++b)
            frames.push_back(seg_um(1000000 + b * 10000 + w * 100, 1000 * b));

    std::vector<uint8_t> file = build_hbp(2, 2, 10, 3, 2, frames);
    show_hbp_t hbp;
    char err[64];
    if (show_hbp_parse(file.data(), file.size(), &hbp, err, sizeof(err)) != 0) {
        const unsigned hl = (unsigned)(file[6] | (file[7] << 8));
        std::printf("S2 parse error: %s\nheader: %.*s\n", err, (int)hl,
                    (const char *)file.data() + 8);
        return 1;
    }
    ASSERT(hbp.cols == 2 && hbp.rows == 2 && hbp.balls == 4);
    ASSERT(hbp.window_ms == 30 && hbp.window_count == 2);
    ASSERT(hbp.frame_count == 8 && hbp.dt_ms == 10 && hbp.n == 3);

    mc_segment_t s;
    ASSERT(show_hbp_segment(&hbp, 1, 2, &s) == 0);
    ASSERT(s.p0 == frames[(size_t)(1 * 4 + 2)].p0);

    /* A single flipped body byte must fail the block CRC. */
    std::vector<uint8_t> broken = file;
    broken[broken.size() - 1] ^= 0x01;
    ASSERT(show_hbp_parse(broken.data(), broken.size(), &hbp, err,
                          sizeof(err)) == -1);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  S3 — the real golden file parses                                   */
/* ------------------------------------------------------------------ */

static int test_s3_golden(void) {
#ifdef SHOW_GOLDEN_HBP
    FILE *f = fopen(SHOW_GOLDEN_HBP, "rb");
    ASSERT(f != NULL);
    ASSERT(fseek(f, 0, SEEK_END) == 0);
    const long size = ftell(f);
    ASSERT(size > 0);
    ASSERT(fseek(f, 0, SEEK_SET) == 0);
    std::vector<uint8_t> buf((size_t)size);
    ASSERT(fread(buf.data(), 1, buf.size(), f) == buf.size());
    fclose(f);

    show_hbp_t hbp;
    char err[64];
    ASSERT(show_hbp_parse(buf.data(), buf.size(), &hbp, err, sizeof(err)) == 0);
    ASSERT(hbp.cols == 10 && hbp.rows == 12 && hbp.balls == 120);
    ASSERT(hbp.window_count == 34 && hbp.frame_count == 4080);
    ASSERT(hbp.window_ms == 30);
#endif
#ifdef SHOW_SINE60_HBP
    FILE *g = fopen(SHOW_SINE60_HBP, "rb");
    ASSERT(g != NULL);
    ASSERT(fseek(g, 0, SEEK_END) == 0);
    const long gsize = ftell(g);
    ASSERT(gsize > 0);
    ASSERT(fseek(g, 0, SEEK_SET) == 0);
    std::vector<uint8_t> gbuf((size_t)gsize);
    ASSERT(fread(gbuf.data(), 1, gbuf.size(), g) == gbuf.size());
    fclose(g);
    show_hbp_t big;
    ASSERT(show_hbp_parse(gbuf.data(), gbuf.size(), &big, err, sizeof(err)) == 0);
    ASSERT(big.cols == 10 && big.rows == 12 && big.balls == 120);
    ASSERT(big.window_count == 2000 && big.frame_count == 240000);
    ASSERT(big.duration_ms == 60000);
#endif
    return 0;
}

/* ------------------------------------------------------------------ */
/*  S4 — one window is sync plus one frame per position                */
/* ------------------------------------------------------------------ */

static int test_s4_plan(void) {
    std::vector<mc_segment_t> frames;
    for (int w = 0; w < 2; ++w)
        for (int b = 0; b < 4; ++b)
            frames.push_back(seg_um(500000 + b * 1000 + w * 50, 0));
    std::vector<uint8_t> file = build_hbp(2, 2, 10, 3, 2, frames);
    show_hbp_t hbp;
    char err[64];
    ASSERT(show_hbp_parse(file.data(), file.size(), &hbp, err, sizeof(err)) == 0);

    show_frame_t plan[SHOW_MAX_WINDOW_FRAMES];
    const int n = show_plan_window(&hbp, 1, 130000, 7, plan,
                                   SHOW_MAX_WINDOW_FRAMES);
    ASSERT(n == 5);
    ASSERT(plan[0].id == mc_id_make(0, 2));
    mc_sync_t sync;
    ASSERT(mc_unpack_sync(plan[0].data, &sync) == 0);
    ASSERT(sync.t_us_low == 130000);
    ASSERT(sync.dt_us == 10000);
    ASSERT(sync.n == 3);
    ASSERT(sync.seq == 7);

    for (int ball = 0; ball < 4; ++ball) {
        ASSERT(plan[1 + ball].id ==
               (uint16_t)mc_id_make(mc_position_field(ball), 0));
        mc_segment_t want, got;
        ASSERT(show_hbp_segment(&hbp, 1, ball, &want) == 0);
        ASSERT(mc_unpack_frame(plan[1 + ball].data, 8, 0, &got) == 0);
        ASSERT(got.p0 == want.p0 && got.v0 == want.v0);
    }

    /* Position 63 is the first skipped field. */
    ASSERT(mc_id_make(mc_position_field(63), 0) == 0x400);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  S5 — node chunks follow the trajectory                             */
/* ------------------------------------------------------------------ */

static int test_s5_node_chunks(void) {
    const int cols = 2, rows = 2, dt = 10, n = 3, windows = 2;
    const int wms = n * dt;
    std::vector<mc_segment_t> frames;
    int p_um[4] = {1000000, 1200000, 800000, 1500000};
    int v_um[4] = {40000, -40000, 0, 20000};
    for (int w = 0; w < windows; ++w) {
        for (int b = 0; b < 4; ++b) {
            const int start = p_um[b] + v_um[b] * (w * wms) / 1000;
            frames.push_back(seg_um(start, v_um[b]));
        }
    }
    std::vector<uint8_t> file = build_hbp(cols, rows, dt, n, windows, frames);
    show_hbp_t hbp;
    char err[64];
    ASSERT(show_hbp_parse(file.data(), file.size(), &hbp, err, sizeof(err)) == 0);

    show_node_init(NULL);   /* no-op guard */
    show_node_t node;
    show_node_init(&node);
    mc_posmap_t map;
    for (int i = 0; i < 4; ++i) map.position[i] = (uint8_t)i;
    map.seq = 1;
    ASSERT(show_node_set_posmap(&node, &map) == 0);

    const int64_t t0 = 100000;
    mc_sync_t sync;
    bool fed[2] = {false, false};
    int chunks = 0;
    int64_t total[4] = {0, 0, 0, 0};

    /* Feed each window at its real sync time, exactly as the master does. */
    for (int64_t t = t0 - SHOW_SYNC_LEAD_US; t <= t0 + 70000; t += 500) {
        for (int w = 0; w < windows; ++w) {
            const int64_t t_sync =
                t0 + (int64_t)w * wms * 1000 - SHOW_SYNC_LEAD_US;
            if (fed[w] || t < t_sync) continue;
            show_frame_t plan[SHOW_MAX_WINDOW_FRAMES];
            ASSERT(show_plan_window(&hbp, w,
                                    t0 + (int64_t)w * wms * 1000,
                                    (uint8_t)w, plan,
                                    SHOW_MAX_WINDOW_FRAMES) == 5);
            ASSERT(mc_unpack_sync(plan[0].data, &sync) == 0);
            ASSERT(show_node_sync(&node, &sync, t) == 0);
            for (int ball = 0; ball < 4; ++ball) {
                mc_segment_t s;
                ASSERT(mc_unpack_frame(plan[1 + ball].data, 8, 0, &s) == 0);
                ASSERT(show_node_motion(&node, mc_position_field(ball), &s,
                                        t) == 0);
            }
            fed[w] = true;
        }
        show_chunk_t chunk;
        for (;;) {
            const int r = show_node_tick(&node, t, &chunk);
            ASSERT(r >= 0);
            if (r == 0) break;
            ++chunks;
            for (int i = 0; i < 4; ++i) total[i] += chunk.steps[i];
        }
        if (t < t0) {
            /* The first chunk must not be queued before T. */
            ASSERT(chunks == 0);
        }
    }
    ASSERT(chunks == 2 * (wms / 2));       /* 2 windows x 15 chunks */
    for (int b = 0; b < 4; ++b) {
        /* Each window resets the accumulator to its own quantized p0, so the
         * commanded total is the sum of the per-window deltas. */
        int64_t want = 0;
        for (int w = 0; w < windows; ++w) {
            const mc_segment_t &seg = frames[(size_t)(w * 4 + b)];
            const int32_t start_um = mc_code_to_um(seg.p0);
            const int32_t end_um = mc_segment_eval_um(&seg, wms * 1000);
            want += (int64_t)mc_um_to_steps(end_um) -
                    (int64_t)mc_um_to_steps(start_um);
        }
        if (total[b] != want)
            std::printf("S5 axis %d total=%lld want=%lld\n", b,
                        (long long)total[b], (long long)want);
        ASSERT(total[b] == want);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/*  S6 — a node ignores positions it does not drive                    */
/* ------------------------------------------------------------------ */

static int test_s6_posmap_filter(void) {
    show_node_t node;
    show_node_init(&node);
    mc_posmap_t map;
    map.position[0] = 0;
    map.position[1] = 2;
    map.position[2] = MC_POS_UNASSIGNED;
    map.position[3] = MC_POS_UNASSIGNED;
    map.seq = 1;
    ASSERT(show_node_set_posmap(&node, &map) == 0);

    mc_sync_t sync;
    sync.t_us_low = 100000;
    sync.dt_us = 10000;
    sync.n = 3;
    sync.seq = 1;
    ASSERT(show_node_sync(&node, &sync, 80000) == 0);

    mc_segment_t s = seg_um(1000000, 1000);
    ASSERT(show_node_motion(&node, mc_position_field(0), &s, 80000) == 0);
    ASSERT(node.pending_count[0] == 1);
    ASSERT(show_node_motion(&node, mc_position_field(1), &s, 80000) == 0);
    ASSERT(node.pending_count[1] == 0);   /* position 1 is another board's */
    ASSERT(show_node_motion(&node, mc_position_field(2), &s, 80000) == 0);
    ASSERT(node.pending_count[1] == 1);
    /* The firmware path resolves the field in the CAN filter and passes the
     * motor index directly. */
    ASSERT(show_node_motion_motor(&node, 3, &s, 80000) == 0);
    ASSERT(node.pending_count[3] == 1);
    return 0;
}

static int reg_s1 = add_test("S1", test_s1_posmap);
static int reg_s2 = add_test("S2", test_s2_parse);
static int reg_s3 = add_test("S3", test_s3_golden);
static int reg_s4 = add_test("S4", test_s4_plan);
static int reg_s5 = add_test("S5", test_s5_node_chunks);
static int reg_s6 = add_test("S6", test_s6_posmap_filter);

/* ------------------------------------------------------------------ */
/*  S7 — loopability                                                    */
/* ------------------------------------------------------------------ */

static int test_s7_loopable(void) {
    /* A 30 ms window with zero velocity is loopable at any start position. */
    std::vector<mc_segment_t> frames;
    for (int w = 0; w < 2; ++w)
        for (int b = 0; b < 4; ++b)
            frames.push_back(seg_um(700000 + b * 1000, 0));
    std::vector<uint8_t> file = build_hbp(2, 2, 10, 3, 2, frames);
    show_hbp_t hbp;
    char err[64];
    ASSERT(show_hbp_parse(file.data(), file.size(), &hbp, err, sizeof(err)) == 0);
    ASSERT(show_hbp_loopable(&hbp, 0, 0) == 1);

    /* Constant speed leaves the second window 1200 µm further on. */
    std::vector<mc_segment_t> moving;
    for (int w = 0; w < 2; ++w)
        for (int b = 0; b < 4; ++b)
            moving.push_back(seg_um(700000 + b * 1000 + w * 1200, 40000));
    std::vector<uint8_t> file2 = build_hbp(2, 2, 10, 3, 2, moving);
    ASSERT(show_hbp_parse(file2.data(), file2.size(), &hbp, err, sizeof(err)) == 0);
    ASSERT(show_hbp_loopable(&hbp, 0, 0) == 0);
    return 0;
}

static int reg_s7 = add_test("S7", test_s7_loopable);

/* ------------------------------------------------------------------ */
/*  S8 — the real golden take feeds a node for all 34 windows          */
/* ------------------------------------------------------------------ */

static void tick_all(show_node_t &node, int64_t local, int64_t total[4],
                     int *chunks) {
    show_chunk_t chunk;
    for (;;) {
        const int r = show_node_tick(&node, local, &chunk);
        if (r < 0) {
            node.fault = 1;   /* reported by the caller's ASSERT */
            return;
        }
        if (r == 0) break;
        ++*chunks;
        for (int i = 0; i < 4; ++i) total[i] += chunk.steps[i];
    }
    /* The queue must stay filled ahead of the timeline; a window-start guard
     * that blocks lookahead leaves last_tick 20 ms behind. */
    if (node.last_tick_us < local - 2000 && node.active[0]) node.fault = 2;
}

static int test_s8_golden_playback(void) {
#ifdef SHOW_GOLDEN_HBP
    FILE *f = fopen(SHOW_GOLDEN_HBP, "rb");
    ASSERT(f != NULL);
    ASSERT(fseek(f, 0, SEEK_END) == 0);
    const long size = ftell(f);
    ASSERT(size > 0);
    ASSERT(fseek(f, 0, SEEK_SET) == 0);
    std::vector<uint8_t> buf((size_t)size);
    ASSERT(fread(buf.data(), 1, buf.size(), f) == buf.size());
    fclose(f);

    show_hbp_t hbp;
    char err[64];
    ASSERT(show_hbp_parse(buf.data(), buf.size(), &hbp, err, sizeof(err)) == 0);

    show_node_t node;
    show_node_init(&node);
    mc_posmap_t map;
    for (int i = 0; i < 4; ++i) map.position[i] = (uint8_t)i;
    map.seq = 1;
    ASSERT(show_node_set_posmap(&node, &map) == 0);

    const int64_t t0 = 100000;
    const int64_t wms_us = (int64_t)hbp.window_ms * 1000;
    int64_t local = t0 - SHOW_SYNC_LEAD_US;
    int64_t total[4] = {0, 0, 0, 0};
    int chunks = 0;

    for (int w = 0; w < hbp.window_count; ++w) {
        const int64_t t = t0 + (int64_t)w * wms_us;
        while (local < t - SHOW_SYNC_LEAD_US) {
            tick_all(node, local, total, &chunks);
            local += 500;
        }
        show_frame_t plan[SHOW_MAX_WINDOW_FRAMES];
        ASSERT(show_plan_window(&hbp, w, t, (uint8_t)w, plan,
                                SHOW_MAX_WINDOW_FRAMES) == 1 + hbp.balls);
        mc_sync_t sync;
        ASSERT(mc_unpack_sync(plan[0].data, &sync) == 0);
        ASSERT(show_node_sync(&node, &sync, local) == 0);
        for (int ball = 0; ball < 4; ++ball) {
            mc_segment_t seg;
            ASSERT(mc_unpack_frame(plan[1 + ball].data, 8, 0, &seg) == 0);
            ASSERT(show_node_motion(&node, mc_position_field(ball), &seg,
                                    local) == 0);
        }
        if (w == 0) {
            /* The first chunk must not start before T. */
            show_chunk_t before;
            ASSERT(show_node_tick(&node, t - 1000, &before) == 0);
        }
    }
    const int64_t end = t0 + (int64_t)hbp.window_count * wms_us + 50000;
    while (local <= end) {
        tick_all(node, local, total, &chunks);
        local += 500;
    }
    ASSERT(node.fault == 0);
    ASSERT(chunks == hbp.window_count * (hbp.window_ms / 2));
    for (int b = 0; b < 4; ++b) {
        int64_t want = 0;
        for (int w = 0; w < hbp.window_count; ++w) {
            mc_segment_t seg;
            ASSERT(show_hbp_segment(&hbp, w, b, &seg) == 0);
            const int32_t start_um = mc_code_to_um(seg.p0);
            const int32_t end_um = mc_segment_eval_um(&seg, wms_us);
            want += (int64_t)mc_um_to_steps(end_um) -
                    (int64_t)mc_um_to_steps(start_um);
        }
        ASSERT(total[b] == want);
    }
#endif
    return 0;
}

static int reg_s8 = add_test("S8", test_s8_golden_playback);

/* ------------------------------------------------------------------ */
/*  S9 — sync-rate correction bounds crystal drift                     */
/* ------------------------------------------------------------------ */

static int test_s9_drift(void) {
    show_node_t node;
    show_node_init(&node);
    mc_posmap_t map;
    for (int i = 0; i < 4; ++i) map.position[i] = MC_POS_UNASSIGNED;
    map.seq = 1;
    ASSERT(show_node_set_posmap(&node, &map) == 0);

    const int64_t ppm = 35;
    const int64_t window_us = 30000;
    mc_sync_t sync;
    sync.dt_us = 10000;
    sync.n = 3;
    sync.seq = 0;
    int64_t worst = 0;
    for (int w = 0; w < 2000; ++w) {
        const int64_t T = (int64_t)w * window_us;
        const int64_t real = T - SHOW_SYNC_LEAD_US;   /* master send time */
        const int64_t local = real + real * ppm / 1000000;
        sync.t_us_low = (uint32_t)T;
        sync.seq = (uint8_t)w;
        ASSERT(show_node_sync(&node, &sync, local) == 0);

        const int64_t local_at_T = T + T * ppm / 1000000;
        const int64_t error = show_node_master_us(&node, local_at_T) - T;
        if (error > worst) worst = error;
        if (-error > worst) worst = -error;
    }
    /* Free-running at 35 ppm for 60 s is 2100 µs; the correction must keep it
     * far inside the 1 ms sync budget. */
    ASSERT(worst < 200);
    return 0;
}

static int reg_s9 = add_test("S9", test_s9_drift);
