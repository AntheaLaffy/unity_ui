#define _POSIX_C_SOURCE 200809L
#include "asmui.h"
#include "asmui_text.h"
#include "asmui_font_sdf_ft.h"
#include "demo/song_select_scene_text.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static uint64_t ns_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static int cmp_u64(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

int main(void) {
    const char *font = asmui_font_find_default();
    if (!font) {
        puts("real-text scene perf SKIP: no default font; set ASMUI_FONT");
        return 0;
    }
    size_t sample_count = 0;
    const char *const *samples = asmui_song_text_samples(&sample_count);
    AsmUiFontSdfAtlas atlas = {0};
    if (!asmui_font_sdf_build(&atlas, font, 1u, 64u, samples, sample_count)) return 1;
    AsmUiHbShaper shaper = asmui_font_sdf_shaper(&atlas);

    static AsmUiNode nodes[ASMUI_SONG_MAX_NODES];
    static AsmUiDrawCmd raw[ASMUI_SONG_MAX_NODES * 2u];
    static AsmUiDrawCmd expanded[ASMUI_SONG_MAX_NODES * 2u];
    static AsmUiGlyphQuad scratch[2048];
    static AsmUiBatch batches[ASMUI_SONG_MAX_NODES * 2u];
    static AsmUiGpuVertex vertices[ASMUI_SONG_MAX_NODES * 8u];
    AsmUiSongTextSceneInfo scene = asmui_song_select_build_text(nodes, ASMUI_SONG_MAX_NODES,
                                                                atlas.px_range);
    AsmUiContext ctx;
    asm_ui_init(&ctx, nodes, ASMUI_SONG_MAX_NODES, raw, ASMUI_SONG_MAX_NODES * 2u,
                NULL, 0u);
    asm_ui_set_viewport(&ctx, (float)ASMUI_SONG_W, (float)ASMUI_SONG_H);
    ctx.node_count = scene.base.node_count;

    enum { SAMPLES = 9, ITERS = 240 };
    uint64_t times[SAMPLES];
    uint32_t last_draws = 0, last_batches = 0;
    for (int s = 0; s < SAMPLES; ++s) {
        uint64_t t0 = ns_now();
        for (int it = 0; it < ITERS; ++it) {
            asm_ui_begin_frame(&ctx, 1.0f / 120.0f);
            asmui_song_select_update(nodes, &scene.base, ctx.time, 0);
            asm_ui_layout(&ctx);
            asm_ui_build_drawlist(&ctx);
            uint32_t n = asmui_expand_text_stream(&ctx, expanded,
                    ASMUI_SONG_MAX_NODES * 2u, asmui_hb_shape, &shaper,
                    scratch, (uint32_t)(sizeof(scratch)/sizeof(scratch[0])));
            AsmUiContext r = ctx;
            r.draws = expanded; r.draw_count = n; r.draw_capacity = ASMUI_SONG_MAX_NODES * 2u;
            last_batches = asm_ui_build_batches(&r, batches, ASMUI_SONG_MAX_NODES * 2u);
            uint32_t v = asm_ui_build_gpu_vertices(&r, vertices,
                    (uint32_t)(sizeof(vertices)/sizeof(vertices[0])));
            if (!n || !last_batches || v != n * 4u) {
                asmui_font_sdf_destroy(&atlas); return 2;
            }
            last_draws = n;
        }
        times[s] = ns_now() - t0;
    }
    qsort(times, SAMPLES, sizeof(times[0]), cmp_u64);
    double med_us = (double)times[SAMPLES/2] / (double)ITERS / 1000.0;
    double ns_per_draw = (double)times[SAMPLES/2] / ((double)ITERS * last_draws);
    double max_ns = 2500.0;
    const char *e = getenv("ASMUI_TEXT_SCENE_MAX_NS_PER_DRAW");
    if (e && *e) max_ns = strtod(e, NULL);

    printf("real-text scene: nodes=%u text-nodes=%u expanded-draws=%u batches=%u frame-med=%.2f us ns/draw=%.1f atlas=%ux%u glyphs=%u missing=%u\n",
           scene.base.node_count, scene.text_node_count, last_draws, last_batches,
           med_us, ns_per_draw, atlas.width, atlas.height,
           atlas.glyph_count, atlas.missing_glyph_count);

    /* Key architectural regression guard: real text must stay dramatically
       smaller than the old 915-node pixel-font workload. */
    int ok = scene.base.node_count < 96u && last_draws < 180u && last_batches < 64u &&
             ns_per_draw <= max_ns;
    asmui_font_sdf_destroy(&atlas);
    if (!ok) {
        fprintf(stderr, "real-text scene structural/perf regression\n");
        return 3;
    }
    puts("real-text scene regression OK");
    return 0;
}
