/* pattern_driver.cpp — deterministic native/WASM parity transcript.
 *
 * Usage: pattern_driver <pattern.anim.json>
 *
 * The same source is compiled natively and with Emscripten.  The two
 * stdout transcripts must be byte-identical (TDD W1/W2): sample bytes,
 * packed type-0 frames, 2 ms chunk bytes and the report JSON.
 */

#include "motion_core.h"
#include "pattern_core/pattern_core.h"

#include <stdint.h>
#include <stdio.h>
#include <string>

static void put_u16(std::string *out, uint16_t v) {
    out->push_back((char)(v & 0xFF));
    out->push_back((char)((v >> 8) & 0xFF));
}

static void put_i32(std::string *out, int32_t v) {
    uint32_t u = (uint32_t)v;
    out->push_back((char)(u & 0xFF));
    out->push_back((char)((u >> 8) & 0xFF));
    out->push_back((char)((u >> 16) & 0xFF));
    out->push_back((char)((u >> 24) & 0xFF));
}

static void print_hex(const char *label, const std::string &bytes) {
    static const char *hex = "0123456789abcdef";
    std::string text;
    text.reserve(bytes.size() * 2);
    for (unsigned char c : bytes) {
        text.push_back(hex[c >> 4]);
        text.push_back(hex[c & 0x0F]);
    }
    printf("%s %s\n", label, text.c_str());
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: pattern_driver <pattern.anim.json>\n");
        return 2;
    }

    pc::AnimFile file;
    std::string error;
    if (!pc::parse_anim_file(argv[1], &file, &error)) {
        fprintf(stderr, "parse error: %s\n", error.c_str());
        return 1;
    }

    pc::Trajectory traj = pc::bake(file);
    pc::CheckResult report = pc::check(file, traj);

    printf("PATTERN-PARITY 1\n");
    printf("grid %d %d\n", file.cols, file.rows);
    printf("cadence %d %d %d %d\n", file.dt_ms, file.n, file.duration_ms,
           (int)file.tier);

    /* Sample bytes: int32 µm, little-endian, frame-major. */
    std::string samples;
    samples.reserve(traj.samples.size() * 4);
    for (int32_t s : traj.samples)
        put_i32(&samples, s);
    print_hex("samples", samples);

    /* Packed type-0 frames: p0 = wire code of each sample, v0 = a0 = j = 0. */
    std::string frames;
    frames.reserve(traj.samples.size() * 8);
    for (int32_t s : traj.samples) {
        mc_segment_t seg = {mc_um_to_code(s), 0, 0, 0};
        uint8_t wire[8];
        if (mc_pack_frame(0, &seg, wire) != 0) {
            fprintf(stderr, "pack failed\n");
            return 1;
        }
        frames.append((const char *)wire, 8);
    }
    print_hex("frames", frames);

    /* Chunk bytes: one constant-speed segment per frame, stepped on the
     * 2 ms grid.  The core is reset per ball and per frame so only the
     * integer planning path is compared. */
    std::string chunks;
    const int dt_us = file.dt_ms * 1000;
    for (int b = 0; b < traj.ball_count(); ++b) {
        for (int fr = 0; fr + 1 < traj.frame_count; ++fr) {
            int32_t p0 = traj.at(fr, b);
            int32_t p1 = traj.at(fr + 1, b);
            int16_t v0 = mc_speed_encode(
                (int32_t)(((int64_t)p1 - p0) * 1000000 / dt_us));
            mc_segment_t seg = {mc_um_to_code(p0), v0, 0, 0};
            mc_core_reset();
            if (mc_trajectory_push(0, 0, 0, dt_us, &seg) != 0) {
                fprintf(stderr, "push failed\n");
                return 1;
            }
            for (int elapsed = 0; elapsed < dt_us; elapsed += 2000) {
                int chunk_us = dt_us - elapsed;
                if (chunk_us > 2000)
                    chunk_us = 2000;
                int16_t steps[4];
                uint16_t ticks = 0;
                if (mc_step_chunk(steps, &ticks, elapsed + chunk_us,
                                  chunk_us) != 0) {
                    fprintf(stderr, "chunk failed\n");
                    return 1;
                }
                put_u16(&chunks, (uint16_t)steps[0]);
                put_u16(&chunks, ticks);
            }
        }
    }
    print_hex("chunks", chunks);

    printf("report %s\n", pc::report_json(report).c_str());
    return 0;
}
