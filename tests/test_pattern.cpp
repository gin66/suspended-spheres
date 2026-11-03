/* test_pattern.cpp — §10 items A1–A5, A9, A10 of motion_streaming_tdd.md.
 *
 * Pattern bake, limits, report.  No browser, no WebGL, no hardware.
 * Uses cJSON (single-file MIT library) for .anim.json parsing.
 */

#include "assert.h"
#include "motion_core.h"
#include "test_list.h"
#include "cJSON.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

static int32_t abs_i32(int32_t v) { return v < 0 ? -v : v; }

/* ------------------------------------------------------------------ */
/*  Pattern file structures                                            */
/* ------------------------------------------------------------------ */

#define MAX_LAYERS 16
#define MAX_GRID 2
#define MAX_SAMPLES 20000
#define MAX_VIOLATIONS 1024

struct LayerDef { char op[32]; int64_t amplitude_um; int64_t period_ms; int64_t wavelength_cols; char axis[8]; };

struct PatternFile {
    int version; int grid[MAX_GRID]; int grid_cols, grid_rows;
    int dt_ms, n; int64_t duration_ms; char tier[32]; int n_layers;
    LayerDef layers[MAX_LAYERS];
};

struct TrajectorySample { int ball_index; int64_t time_ms; int32_t pos_um; };
struct Trajectory { TrajectorySample samples[MAX_SAMPLES]; int count; int grid_cols, grid_rows; int dt_ms, n; int64_t duration_ms; };

struct LimitViolation { int ball_index; int64_t time_ms; char limit_name[64]; int64_t measured_um; int64_t allowed_um; };
struct LimitReport { LimitViolation violations[MAX_VIOLATIONS]; int count; };

/* Parse .anim.json using cJSON. */
static int parse_pattern_file(const char *path, PatternFile *pf) {
    memset(pf, 0, sizeof(*pf));
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
    char *buf = (char*)malloc(len + 1);
    fread(buf, 1, len, f); buf[len] = '\0'; fclose(f);

    cJSON *root = cJSON_Parse(buf); free(buf);
    if (!root || !cJSON_IsObject(root)) { cJSON_Delete(root); return -1; }

    /* version */
    cJSON *ver = cJSON_GetObjectItem(root, "version");
    if (!ver || !cJSON_IsNumber(ver) || ver->valuedouble != (double)ver->valueint || ver->valueint != 1) { cJSON_Delete(root); return -1; }
    pf->version = ver->valueint;

    /* grid */
    cJSON *grid = cJSON_GetObjectItem(root, "grid");
    if (!grid || !cJSON_IsArray(grid) || cJSON_GetArraySize(grid) != 2) { cJSON_Delete(root); return -1; }
    int64_t g0 = cJSON_GetArrayItem(grid, 0)->valueint;
    int64_t g1 = cJSON_GetArrayItem(grid, 1)->valueint;
    pf->grid[0] = (int)g0; pf->grid[1] = (int)g1;
    if (!((pf->grid[0]==10&&pf->grid[1]==10)||(pf->grid[0]==10&&pf->grid[1]==12))) { cJSON_Delete(root); return -1; }
    pf->grid_cols = pf->grid[0]; pf->grid_rows = pf->grid[1];

    /* dt_ms */
    cJSON *dt = cJSON_GetObjectItem(root, "dt_ms");
    if (!dt || !cJSON_IsNumber(dt)) { cJSON_Delete(root); return -1; }
    pf->dt_ms = dt->valueint;
    if (pf->dt_ms!=10&&pf->dt_ms!=11&&pf->dt_ms!=15) { cJSON_Delete(root); return -1; }

    /* n */
    cJSON *nv = cJSON_GetObjectItem(root, "n");
    if (!nv || !cJSON_IsNumber(nv)) { cJSON_Delete(root); return -1; }
    pf->n = nv->valueint;
    if (pf->n!=3&&pf->n!=4) { cJSON_Delete(root); return -1; }

    /* duration_ms */
    cJSON *dur = cJSON_GetObjectItem(root, "duration_ms");
    if (!dur || !cJSON_IsNumber(dur)) { cJSON_Delete(root); return -1; }
    pf->duration_ms = dur->valueint;

    /* tier */
    cJSON *tier = cJSON_GetObjectItem(root, "tier");
    if (!tier || !cJSON_IsString(tier)) { cJSON_Delete(root); return -1; }
    strncpy(pf->tier, tier->valuestring, 31); pf->tier[31] = '\0';

    /* layers */
    cJSON *lv = cJSON_GetObjectItem(root, "layers");
    if (!lv || !cJSON_IsArray(lv)) { cJSON_Delete(root); return -1; }

    pf->n_layers = 0;
    int nlay = cJSON_GetArraySize(lv);
    for (int i = 0; i < nlay && pf->n_layers < MAX_LAYERS; ++i) {
        cJSON *lay = cJSON_GetArrayItem(lv, i);
        if (!cJSON_IsObject(lay)) { cJSON_Delete(root); return -1; }
        LayerDef *pl = &pf->layers[pf->n_layers];

        cJSON *op = cJSON_GetObjectItem(lay, "op");
        if (!op || !cJSON_IsString(op)) { cJSON_Delete(root); return -1; }
        strncpy(pl->op, op->valuestring, 31); pl->op[31] = '\0';

        cJSON *amp = cJSON_GetObjectItem(lay, "amplitude_um");
        if (!amp || !cJSON_IsNumber(amp)) { cJSON_Delete(root); return -1; }
        pl->amplitude_um = amp->valueint;

        cJSON *per = cJSON_GetObjectItem(lay, "period_ms");
        if (!per || !cJSON_IsNumber(per)) { cJSON_Delete(root); return -1; }
        pl->period_ms = per->valueint;

        cJSON *wl = cJSON_GetObjectItem(lay, "wavelength_cols");
        if (!wl || !cJSON_IsNumber(wl)) { cJSON_Delete(root); return -1; }
        pl->wavelength_cols = wl->valueint;

        cJSON *ax = cJSON_GetObjectItem(lay, "axis");
        if (!ax || !cJSON_IsString(ax)) { cJSON_Delete(root); return -1; }
        strncpy(pl->axis, ax->valuestring, 7); pl->axis[7] = '\0';

        ++pf->n_layers;
    }

    cJSON_Delete(root);
    return 0;
}

/* Sine layer position. */
static int64_t sine_layer_position(const LayerDef *lay, int col, int row, int64_t time_ms, int gc) {
    double phase = 2.0 * M_PI * time_ms / (double)lay->period_ms;
    double spatial = 2.0 * M_PI * col / (double)lay->wavelength_cols;
    int64_t result = 1000000 + (int64_t)lround((double)lay->amplitude_um * sin(phase + spatial));
    return result;
}

/* Bake trajectory. */
static int bake_trajectory(const PatternFile *pf, Trajectory *tr) {
    int total_balls = pf->grid_cols * pf->grid_rows;
    int64_t n_samples = pf->duration_ms / pf->dt_ms;
    if ((int)n_samples * total_balls > MAX_SAMPLES) return -1;
    tr->grid_cols = pf->grid_cols; tr->grid_rows = pf->grid_rows;
    tr->dt_ms = pf->dt_ms; tr->n = pf->n; tr->duration_ms = pf->duration_ms; tr->count = 0;
    for (int64_t t = 0; t < n_samples; ++t) {
        int64_t time_ms = t * pf->dt_ms;
        for (int ball = 0; ball < total_balls; ++ball) {
            int col = ball % pf->grid_cols, row = ball / pf->grid_cols;
            int64_t pos_um = 1000000;
            for (int l = 0; l < pf->n_layers; ++l)
                pos_um = sine_layer_position(&pf->layers[l], col, row, time_ms, pf->grid_cols);
            TrajectorySample *s = &tr->samples[tr->count];
            s->ball_index = ball; s->time_ms = time_ms; s->pos_um = (int32_t)pos_um;
            ++tr->count;
        }
    }
    return 0;
}

/* Check limits. */
static int check_limits(const Trajectory *tr, const char *tier, LimitReport *report) {
    report->count = 0;
    int total_balls = tr->grid_cols * tr->grid_rows;
    int64_t n_samples = tr->duration_ms / tr->dt_ms;
    for (int ball = 0; ball < total_balls; ++ball) {
        for (int64_t t = 0; t < n_samples; ++t) {
            int idx = (int)t * total_balls + ball;
            if (idx >= tr->count) break;
            const TrajectorySample *s = &tr->samples[idx];
            if (s->pos_um < 0 || s->pos_um > 2000000) {
                LimitViolation *v = &report->violations[report->count];
                v->ball_index = ball; v->time_ms = s->time_ms;
                strncpy(v->limit_name, "FR-MOT-005", 63);
                v->measured_um = s->pos_um; v->allowed_um = 2000000;
                ++report->count;
            }
            if (t > 0 && t < n_samples - 1) {
                int idx_prev = (int)(t-1)*total_balls+ball;
                int idx_next = (int)(t+1)*total_balls+ball;
                if (idx_prev>=0&&idx_prev<tr->count&&idx_next>=0&&idx_next<tr->count) {
                    int64_t dz = (int64_t)tr->samples[idx_next].pos_um - (int64_t)tr->samples[idx_prev].pos_um;
                    int64_t speed_um_s = (int64_t)abs_i32((int)dz) * 1000 / ((int64_t)tr->dt_ms * 2);
                    int64_t speed_limit = 0;
                    if (strcmp(tier,"ordinary")==0) speed_limit = 300000;
                    else if (strcmp(tier,"nominal")==0) speed_limit = 590000;
                    else if (strcmp(tier,"optional")==0) speed_limit = 1300000;
                    if (speed_um_s > speed_limit) {
                        LimitViolation *v = &report->violations[report->count];
                        v->ball_index = ball; v->time_ms = s->time_ms;
                        strncpy(v->limit_name, "ASM-015", 63);
                        v->measured_um = speed_um_s; v->allowed_um = speed_limit;
                        ++report->count;
                    }
                }
            }
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Tests A1, A2, A3, A5, A9, A10                                    */
/* ------------------------------------------------------------------ */

static int test_a1_schema(void) {
    PatternFile pf;
    int rc = parse_pattern_file("patterns/golden_sine.anim.json", &pf);
    ASSERT(rc == 0);
    ASSERT(pf.version == 1);
    ASSERT(pf.grid_cols == 10);
    ASSERT(pf.grid_rows == 12);
    ASSERT(pf.dt_ms == 10);
    ASSERT(pf.n == 3);
    ASSERT(pf.duration_ms == 1000);
    ASSERT(strcmp(pf.tier, "ordinary") == 0);
    ASSERT(pf.n_layers == 1);
    ASSERT(strcmp(pf.layers[0].op, "sine") == 0);
    ASSERT(pf.layers[0].amplitude_um == 50000);
    ASSERT(pf.layers[0].period_ms == 5000);
    ASSERT(pf.layers[0].wavelength_cols == 10);
    ASSERT(strcmp(pf.layers[0].axis, "col") == 0);

    /* Missing version. */
    char bad1[] = "{\"grid\":[10,10],\"dt_ms\":10,\"n\":3,\"duration_ms\":1000,\"tier\":\"ordinary\",\"layers\":[]}";
    FILE *f = fopen("/tmp/test_bad1.json","w"); if(f){fwrite(bad1,1,strlen(bad1),f);fclose(f);}
    ASSERT(parse_pattern_file("/tmp/test_bad1.json",&pf)!=0);

    /* Missing grid. */
    char bad2[] = "{\"version\":1,\"dt_ms\":10,\"n\":3,\"duration_ms\":1000,\"tier\":\"ordinary\",\"layers\":[]}";
    f = fopen("/tmp/test_bad2.json","w"); if(f){fwrite(bad2,1,strlen(bad2),f);fclose(f);}
    ASSERT(parse_pattern_file("/tmp/test_bad2.json",&pf)!=0);

    /* Missing tier. */
    char bad3[] = "{\"version\":1,\"grid\":[10,10],\"dt_ms\":10,\"n\":3,\"duration_ms\":1000,\"layers\":[]}";
    f = fopen("/tmp/test_bad3.json","w"); if(f){fwrite(bad3,1,strlen(bad3),f);fclose(f);}
    ASSERT(parse_pattern_file("/tmp/test_bad3.json",&pf)!=0);

    /* Bad grid. */
    char bad4[] = "{\"version\":1,\"grid\":[8,8],\"dt_ms\":10,\"n\":3,\"duration_ms\":1000,\"tier\":\"ordinary\",\"layers\":[]}";
    f = fopen("/tmp/test_bad4.json","w"); if(f){fwrite(bad4,1,strlen(bad4),f);fclose(f);}
    ASSERT(parse_pattern_file("/tmp/test_bad4.json",&pf)!=0);

    /* Bad dt_ms. */
    char bad5[] = "{\"version\":1,\"grid\":[10,10],\"dt_ms\":20,\"n\":3,\"duration_ms\":1000,\"tier\":\"ordinary\",\"layers\":[]}";
    f = fopen("/tmp/test_bad5.json","w"); if(f){fwrite(bad5,1,strlen(bad5),f);fclose(f);}
    ASSERT(parse_pattern_file("/tmp/test_bad5.json",&pf)!=0);

    /* Bad n. */
    char bad6[] = "{\"version\":1,\"grid\":[10,10],\"dt_ms\":10,\"n\":5,\"duration_ms\":1000,\"tier\":\"ordinary\",\"layers\":[]}";
    f = fopen("/tmp/test_bad6.json","w"); if(f){fwrite(bad6,1,strlen(bad6),f);fclose(f);}
    ASSERT(parse_pattern_file("/tmp/test_bad6.json",&pf)!=0);

    /* Non-integer version. */
    char bad7[] = "{\"version\":1.5,\"grid\":[10,10],\"dt_ms\":10,\"n\":3,\"duration_ms\":1000,\"tier\":\"ordinary\",\"layers\":[]}";
    f = fopen("/tmp/test_bad7.json","w"); if(f){fwrite(bad7,1,strlen(bad7),f);fclose(f);}
    ASSERT(parse_pattern_file("/tmp/test_bad7.json",&pf)!=0);

    /* .py path. */
    ASSERT(parse_pattern_file("tests/fixtures/write_sine.py",&pf)!=0);
    return 0;
}

static int test_a2_deterministic(void) {
    PatternFile pf;
    ASSERT(parse_pattern_file("patterns/golden_sine.anim.json",&pf)==0);
    Trajectory tr1, tr2;
    memset(&tr1,0,sizeof(tr1)); memset(&tr2,0,sizeof(tr2));
    ASSERT(bake_trajectory(&pf,&tr1)==0);
    ASSERT(bake_trajectory(&pf,&tr2)==0);
    ASSERT(tr1.count == tr2.count);
    for (int i = 0; i < tr1.count; ++i) {
        ASSERT(tr1.samples[i].ball_index == tr2.samples[i].ball_index);
        ASSERT(tr1.samples[i].time_ms == tr2.samples[i].time_ms);
        ASSERT(tr1.samples[i].pos_um == tr2.samples[i].pos_um);
    }
    return 0;
}

static int test_a3_sine_layer(void) {
    PatternFile pf;
    ASSERT(parse_pattern_file("patterns/golden_sine.anim.json",&pf)==0);
    Trajectory tr; memset(&tr,0,sizeof(tr));
    ASSERT(bake_trajectory(&pf,&tr)==0);
    int total_balls = pf.grid_cols * pf.grid_rows;
    int64_t n_samples = pf.duration_ms / pf.dt_ms;
    ASSERT(tr.count == (int)(n_samples * total_balls));
    int min_pos = 2000000, max_pos = 0;
    for (int i = 0; i < tr.count; ++i) {
        if (tr.samples[i].pos_um < min_pos) min_pos = tr.samples[i].pos_um;
        if (tr.samples[i].pos_um > max_pos) max_pos = tr.samples[i].pos_um;
    }
    ASSERT(min_pos < 1000000);
    ASSERT(max_pos > 1000000);
    ASSERT(min_pos >= 0);
    ASSERT(max_pos <= 2000000);
    return 0;
}

static int test_a5_violations(void) {
    Trajectory tr; memset(&tr,0,sizeof(tr));
    tr.grid_cols=10; tr.grid_rows=12; tr.dt_ms=10; tr.n=3; tr.duration_ms=1000; tr.count=120;
    int idx = 0*120+5;
    tr.samples[idx].ball_index=5; tr.samples[idx].time_ms=0; tr.samples[idx].pos_um=-100;
    LimitReport report; memset(&report,0,sizeof(report));
    ASSERT(check_limits(&tr,"ordinary",&report)==0);
    ASSERT(report.count > 0);
    ASSERT(strcmp(report.violations[0].limit_name,"FR-MOT-005")==0);
    ASSERT(report.violations[0].ball_index==5);

    /* Speed violation for nominal tier. */
    memset(&tr,0,sizeof(tr));
    tr.grid_cols=10; tr.grid_rows=12; tr.dt_ms=10; tr.n=3; tr.duration_ms=1000; tr.count=100*120;
    for (int t = 0; t < 100; ++t) {
        for (int ball = 0; ball < 120; ++ball) {
            int i = t*120+ball;
            if (ball==10) {
                if (t==0) tr.samples[i].pos_um=950000;
                else if (t==50) tr.samples[i].pos_um=1000000;
                else if (t==100) tr.samples[i].pos_um=1050000;
                else tr.samples[i].pos_um=1000000;
            } else tr.samples[i].pos_um=1000000;
            tr.samples[i].ball_index=ball; tr.samples[i].time_ms=(int64_t)t*tr.dt_ms;
        }
    }
    ASSERT(check_limits(&tr,"nominal",&report)==0);
    int found = 0;
    for (int i = 0; i < report.count; ++i)
        if (report.violations[i].ball_index==10) { found=1; break; }
    ASSERT(found);

    /* Clean take. */
    memset(&tr,0,sizeof(tr));
    tr.grid_cols=10; tr.grid_rows=12; tr.dt_ms=10; tr.n=3; tr.duration_ms=1000;
    for (int i = 0; i < 100*120; ++i) {
        tr.samples[i].ball_index=i%120; tr.samples[i].time_ms=(int64_t)(i/120)*tr.dt_ms;
        tr.samples[i].pos_um=1000000;
    }
    ASSERT(check_limits(&tr,"ordinary",&report)==0);
    ASSERT(report.count == 0);
    return 0;
}

static int test_a9_120_ball_sine(void) {
    PatternFile pf;
    ASSERT(parse_pattern_file("patterns/golden_sine.anim.json",&pf)==0);
    Trajectory tr; memset(&tr,0,sizeof(tr));
    ASSERT(bake_trajectory(&pf,&tr)==0);
    for (int i = 0; i < tr.count; ++i) {
        ASSERT(tr.samples[i].pos_um>=0);
        ASSERT(tr.samples[i].pos_um<=2000000);
    }
    LimitReport report; memset(&report,0,sizeof(report));
    ASSERT(check_limits(&tr,"ordinary",&report)==0);
    ASSERT(report.count == 0);
    int load = mc_bus_load_percent_with_overhead(pf.n, pf.dt_ms);
    ASSERT(load < 60);
    return 0;
}

static int test_a10_fidelity(void) {
    PatternFile pf;
    ASSERT(parse_pattern_file("patterns/golden_sine.anim.json",&pf)==0);
    Trajectory tr; memset(&tr,0,sizeof(tr));
    ASSERT(bake_trajectory(&pf,&tr)==0);
    int max_wire_error = 0;
    for (int i = 0; i < tr.count; ++i) {
        uint16_t code = mc_um_to_code(tr.samples[i].pos_um);
        int32_t back = mc_code_to_um(code);
        int64_t err = abs_i32(tr.samples[i].pos_um - back);
        if (err > max_wire_error) max_wire_error = (int)err;
    }
    ASSERT(max_wire_error <= 30);
    for (int i = 0; i < tr.count; i += 120) {
        int32_t steps = mc_um_to_steps(tr.samples[i].pos_um);
        int32_t back_um = mc_steps_to_um(steps);
        int64_t err = abs_i32(tr.samples[i].pos_um - back_um);
        ASSERT(err < 50);
    }
    return 0;
}

static int reg_a1 = add_test("A1", test_a1_schema);
static int reg_a2 = add_test("A2", test_a2_deterministic);
static int reg_a3 = add_test("A3", test_a3_sine_layer);
static int reg_a5 = add_test("A5", test_a5_violations);
static int reg_a9 = add_test("A9", test_a9_120_ball_sine);
static int reg_a10 = add_test("A10", test_a10_fidelity);