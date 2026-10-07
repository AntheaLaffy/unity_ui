#define _POSIX_C_SOURCE 200809L
#include "asmui.h"
#include "asmui_builder.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdint.h>

static uint64_t ns_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static int cmp_double(const void *a, const void *b) {
    const double da = *(const double *)a;
    const double db = *(const double *)b;
    return (da > db) - (da < db);
}

int main(void) {
    enum { N = 4096, ITERS = 160, SAMPLES = 9 };
    AsmUiNode *nodes = calloc(N, sizeof(*nodes));
    AsmUiDrawCmd *draws = calloc(N, sizeof(*draws));
    AsmUiBatch *batches = calloc(N, sizeof(*batches));
    AsmUiGpuVertex *verts = calloc((size_t)N * 4u, sizeof(*verts));
    AsmUiTween tween = {0};
    if (!nodes || !draws || !batches || !verts) return 2;

    AsmUiContext ctx;
    asm_ui_init(&ctx, nodes, N, draws, N, &tween, 1);
    asm_ui_set_viewport(&ctx, 1920, 1080);
    ctx.node_count = N;

    for (uint32_t i = 0; i < N; ++i) {
        nodes[i] = asmui_node(ASMUI_NODE_IMAGE, -1, 0xffffffffu);
        nodes[i].size_delta_x = 16.0f;
        nodes[i].size_delta_y = 16.0f;
        nodes[i].anchored_x = (float)((int)(i % 120u) * 16 - 960);
        nodes[i].anchored_y = (float)((int)(i / 120u) * 16 - 270);
        /* Deliberately alternate resources: worst-case adjacent batch pressure. */
        nodes[i].resource_id = i & 3u;
    }

    for (int i = 0; i < 30; ++i) {
        asm_ui_begin_frame(&ctx, 1.0f / 60.0f);
        asm_ui_layout(&ctx);
        asm_ui_build_drawlist(&ctx);
        asm_ui_build_batches(&ctx, batches, N);
        asm_ui_build_gpu_vertices(&ctx, verts, N * 4u);
    }

    double samples[SAMPLES] = {0};
    double batch_samples[SAMPLES] = {0};
    for (int s = 0; s < SAMPLES; ++s) {
        uint64_t t0 = ns_now();
        uint64_t total_draws = 0, total_batches = 0;
        for (int i = 0; i < ITERS; ++i) {
            asm_ui_begin_frame(&ctx, 1.0f / 60.0f);
            asm_ui_layout(&ctx);
            asm_ui_build_drawlist(&ctx);
            uint32_t bc = asm_ui_build_batches(&ctx, batches, N);
            uint32_t vc = asm_ui_build_gpu_vertices(&ctx, verts, N * 4u);
            if (ctx.draw_count != N || vc != N * 4u || bc == 0) return 3;
            total_draws += ctx.draw_count;
            total_batches += bc;
        }
        uint64_t t1 = ns_now();
        samples[s] = (double)(t1 - t0) / (double)total_draws;
        batch_samples[s] = (double)total_batches / ITERS;
    }

    qsort(samples, SAMPLES, sizeof(samples[0]), cmp_double);
    qsort(batch_samples, SAMPLES, sizeof(batch_samples[0]), cmp_double);
    const double min_ns = samples[0];
    const double median_ns = samples[SAMPLES / 2];
    const double max_ns = samples[SAMPLES - 1];
    const double mdraw_s = 1000.0 / median_ns;
    const double median_batches = batch_samples[SAMPLES / 2];

    printf("perf core: N=%d samples=%d x iters=%d min/median/max=%.1f/%.1f/%.1f ns/draw "
           "median-throughput=%.2f Mdraw/s median-batches=%.1f\n",
           N, SAMPLES, ITERS, min_ns, median_ns, max_ns, mdraw_s, median_batches);

    /* Median makes the guardrail less sensitive to scheduler/turbo outliers.
       Default remains deliberately broad; override locally/CI for real tracking. */
    double guard_ns = 1000.0;
    const char *env = getenv("ASMUI_PERF_MAX_NS_PER_DRAW");
    if (env && *env) guard_ns = strtod(env, NULL);
    if (median_ns > guard_ns) {
        fprintf(stderr, "PERF REGRESSION: median %.1f ns/draw > %.1f ns/draw guardrail\n",
                median_ns, guard_ns);
        return 4;
    }

    free(verts); free(batches); free(draws); free(nodes);
    return 0;
}
