#define _POSIX_C_SOURCE 200809L
#include "asmui.h"
#include "asmui_interaction.h"
#include "demo/song_select_scene_text.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <string.h>

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}
static int cmp_u64(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

int main(void) {
    enum { FRAMES = 2400 };
    static AsmUiNode nodes[ASMUI_SONG_MAX_NODES];
    static AsmUiDrawCmd draws[ASMUI_SONG_MAX_NODES * 2u];
    static AsmUiTween tweens[8];
    static AsmUiBatch batches[ASMUI_SONG_MAX_NODES * 2u];
    uint64_t *samples = calloc(FRAMES, sizeof(*samples));
    if (!samples) return 1;

    AsmUiSongTextSceneInfo s = asmui_song_select_build_text(nodes, ASMUI_SONG_MAX_NODES, 8.0f);
    if (s.carousel_count != 7u) return 2;
    AsmUiContext ctx;
    asm_ui_init(&ctx, nodes, ASMUI_SONG_MAX_NODES, draws, ASMUI_SONG_MAX_NODES * 2u, tweens, 8u);
    asm_ui_set_viewport(&ctx, 1280.0f, 720.0f);
    ctx.node_count = s.base.node_count;

    AsmUiCarousel c;
    asmui_carousel_init(&c, s.carousel_count, 3u, 0.0f, 0.0f, 156.0f, 125.0f);
    if (!asmui_song_select_bind(nodes, &s, c.selected)) return 3;
    uint32_t bound_song = c.selected;
    uint32_t worst_batches = 0;
    for (uint32_t f = 0; f < FRAMES; ++f) {
        if (f && f % 240u == 0u) {
            int dir = ((f / 240u) & 1u) ? 1 : -1;
            asmui_carousel_nudge(&c, dir);
        }
        uint64_t t0 = now_ns();
        asm_ui_begin_frame(&ctx, 1.0f / 120.0f);
        asmui_carousel_step(&c, 1.0f / 120.0f);
        if (bound_song != c.selected) {
            if (!asmui_song_select_bind(nodes, &s, c.selected)) return 3;
            bound_song = c.selected;
        }
        for (uint32_t i = 0; i < s.carousel_count; ++i) {
            asmui_carousel_apply_node(&c, i, &nodes[s.carousel_root[i]]);
        }
        asm_ui_layout(&ctx);
        asm_ui_build_drawlist(&ctx);
        uint32_t bc = asm_ui_build_batches(&ctx, batches, ASMUI_SONG_MAX_NODES * 2u);
        if (bc > worst_batches) worst_batches = bc;
        samples[f] = now_ns() - t0;
    }

    qsort(samples, FRAMES, sizeof(*samples), cmp_u64);
    double p50 = samples[FRAMES / 2] / 1000.0;
    double p95 = samples[(FRAMES * 95u) / 100u] / 1000.0;
    double p99 = samples[(FRAMES * 99u) / 100u] / 1000.0;
    free(samples);

    AsmUiCarouselItemVisual selected = asmui_carousel_item(&c, c.selected);
    uint32_t song_count = 0;
    const AsmUiSongMeta *songs = asmui_song_catalog(&song_count);
    if (ctx.draw_count < 50u || worst_batches > 64u || selected.alpha < .95f || selected.scale < 1.05f ||
        !songs || c.selected >= song_count || strcmp(nodes[s.title_node].payload, songs[c.selected].title)) {
        fprintf(stderr, "carousel scene regression: draws=%u batches=%u alpha=%.3f scale=%.3f\n",
                ctx.draw_count, worst_batches, selected.alpha, selected.scale);
        return 3;
    }
    const char *env = getenv("ASMUI_CAROUSEL_CPU_P95_US");
    double limit = env ? strtod(env, NULL) : 1000.0;
    if (p95 > limit) {
        fprintf(stderr, "carousel CPU regression: p95 %.2f us > %.2f us\n", p95, limit);
        return 4;
    }
    printf("carousel scene: frames=%u nodes=%u draws=%u worst-batches=%u selected=%u p50/p95/p99=%.2f/%.2f/%.2f us\n",
           FRAMES, ctx.node_count, ctx.draw_count, worst_batches, c.selected, p50, p95, p99);
    puts("carousel interaction/perf regression OK");
    return 0;
}
