#define _POSIX_C_SOURCE 200809L
#include "asmui.h"
#include "demo/song_select_scene.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define FRAMES 1200u

static uint64_t ns_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static int cmp_u64(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

int main(void) {
    static AsmUiNode nodes[ASMUI_SONG_MAX_NODES];
    static AsmUiDrawCmd draws[ASMUI_SONG_MAX_NODES * 2u];
    static AsmUiTween tweens[16];
    static AsmUiBatch batches[ASMUI_SONG_MAX_NODES * 2u];
    static AsmUiGpuVertex verts[ASMUI_SONG_MAX_NODES * 8u];
    uint64_t samples[FRAMES];

    AsmUiSongSceneInfo scene = asmui_song_select_build(nodes, ASMUI_SONG_MAX_NODES);
    if (scene.node_count < 800u || scene.gauge_node == UINT32_MAX ||
        scene.play_panel_node == UINT32_MAX || scene.jacket_accent_node == UINT32_MAX) {
        fprintf(stderr, "realtime scene handles invalid\n");
        return 2;
    }

    AsmUiContext ctx;
    asm_ui_init(&ctx, nodes, ASMUI_SONG_MAX_NODES,
                draws, ASMUI_SONG_MAX_NODES * 2u,
                tweens, 16u);
    asm_ui_set_viewport(&ctx, (float)ASMUI_SONG_W, (float)ASMUI_SONG_H);
    ctx.node_count = scene.node_count;

    uint32_t worst_batches = 0;
    for (uint32_t f = 0; f < FRAMES; ++f) {
        const float dt = 1.0f / 120.0f;
        uint64_t t0 = ns_now();
        asm_ui_begin_frame(&ctx, dt);
        asmui_song_select_update(nodes, &scene, ctx.time, (int)((f / 120u) % 3u) - 1);
        asm_ui_tick_tweens(&ctx);
        asm_ui_layout(&ctx);
        asm_ui_build_drawlist(&ctx);
        uint32_t nb = asm_ui_build_batches(&ctx, batches, ASMUI_SONG_MAX_NODES * 2u);
        uint32_t nv = asm_ui_build_gpu_vertices(&ctx, verts, ASMUI_SONG_MAX_NODES * 8u);
        uint64_t t1 = ns_now();
        samples[f] = t1 - t0;
        if (nb > worst_batches) worst_batches = nb;

        /* The optional external jacket node is retained but invisible until a PNG is bound. */
        uint32_t expected_draws = scene.node_count - 1u;
        if (ctx.draw_count != expected_draws || nv != ctx.draw_count * 4u || nb == 0u || nb > 96u) {
            fprintf(stderr, "realtime stream regression at frame %u: draws=%u batches=%u verts=%u\n",
                    f, ctx.draw_count, nb, nv);
            return 3;
        }
        float fill = nodes[scene.gauge_node].fill_amount;
        float scale = nodes[scene.play_panel_node].scale_x;
        if (fill < 0.67f || fill > 0.89f || scale < 0.999f || scale > 1.03f) {
            fprintf(stderr, "realtime animation range regression frame=%u fill=%.3f scale=%.3f\n",
                    f, fill, scale);
            return 4;
        }
    }

    qsort(samples, FRAMES, sizeof(samples[0]), cmp_u64);
    double p50_us = samples[FRAMES / 2u] / 1000.0;
    double p95_us = samples[(FRAMES * 95u) / 100u] / 1000.0;
    double p99_us = samples[(FRAMES * 99u) / 100u] / 1000.0;
    double max_us = samples[FRAMES - 1u] / 1000.0;
    printf("realtime CPU: frames=%u nodes=%u draws=%u worst-batches=%u p50/p95/p99/max=%.2f/%.2f/%.2f/%.2f us\n",
           FRAMES, scene.node_count, ctx.draw_count, worst_batches,
           p50_us, p95_us, p99_us, max_us);

    double p95_guard_us = 1000.0;
    double p99_guard_us = 5000.0;
    const char *env95 = getenv("ASMUI_REALTIME_CPU_P95_US");
    const char *env99 = getenv("ASMUI_REALTIME_CPU_P99_US");
    if (env95 && *env95) p95_guard_us = strtod(env95, NULL);
    if (env99 && *env99) p99_guard_us = strtod(env99, NULL);
    if (p95_us > p95_guard_us || p99_us > p99_guard_us) {
        fprintf(stderr, "REALTIME CPU PERF REGRESSION: p95 %.2f/%.2f us p99 %.2f/%.2f us\n",
                p95_us, p95_guard_us, p99_us, p99_guard_us);
        return 5;
    }
    puts("realtime animation/CPU regression OK");
    return 0;
}
