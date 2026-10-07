#define _POSIX_C_SOURCE 200809L
#include "asmui.h"
#include "asmui_interaction.h"
#include "demo/song_select_scene_text.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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
    uint32_t drag_count = 0, wheel_count = 0;

    for (uint32_t f = 0; f < FRAMES; ++f) {
        uint32_t phase = f % 240u;
        double t = (double)f / 120.0;
        if (phase == 0u) {
            asmui_carousel_pointer_begin(&c, 90.0f, t);
            ++drag_count;
        } else if (phase >= 1u && phase <= 12u) {
            /* Alternate left/right drags.  Around one card width over 100 ms. */
            float sign = ((f / 240u) & 1u) ? 1.0f : -1.0f;
            float x = 90.0f + sign * 156.0f * ((float)phase / 12.0f);
            asmui_carousel_pointer_move(&c, x, t);
        } else if (phase == 13u) {
            asmui_carousel_pointer_end(&c);
        } else if (phase == 120u) {
            float wy = ((f / 240u) & 1u) ? -1.0f : 1.0f;
            asmui_carousel_wheel(&c, wy);
            ++wheel_count;
        }

        uint64_t t0 = now_ns();
        asm_ui_begin_frame(&ctx, 1.0f / 120.0f);
        asmui_carousel_step(&c, 1.0f / 120.0f);
        if (bound_song != c.selected) {
            if (!asmui_song_select_bind(nodes, &s, c.selected)) return 4;
            bound_song = c.selected;
        }
        for (uint32_t i = 0; i < s.carousel_count; ++i)
            asmui_carousel_apply_node(&c, i, &nodes[s.carousel_root[i]]);
        asm_ui_layout(&ctx);
        asm_ui_build_drawlist(&ctx);
        uint32_t bc = asm_ui_build_batches(&ctx, batches, ASMUI_SONG_MAX_NODES * 2u);
        if (bc > worst_batches) worst_batches = bc;
        samples[f] = now_ns() - t0;
    }

    if (c.pointer_dragging) asmui_carousel_pointer_end(&c);
    qsort(samples, FRAMES, sizeof(*samples), cmp_u64);
    double p50 = samples[FRAMES / 2] / 1000.0;
    double p95 = samples[(FRAMES * 95u) / 100u] / 1000.0;
    double p99 = samples[(FRAMES * 99u) / 100u] / 1000.0;
    free(samples);

    uint32_t song_count = 0;
    const AsmUiSongMeta *songs = asmui_song_catalog(&song_count);
    if (!songs || c.selected >= song_count || bound_song != c.selected ||
        strcmp(nodes[s.title_node].payload, songs[c.selected].title) ||
        worst_batches > 64u || ctx.draw_count < 50u || drag_count < 5u || wheel_count < 5u) {
        fprintf(stderr, "pointer/carousel regression: selected=%u bound=%u draws=%u batches=%u drags=%u wheels=%u\n",
                c.selected, bound_song, ctx.draw_count, worst_batches, drag_count, wheel_count);
        return 5;
    }

    const char *env = getenv("ASMUI_POINTER_CPU_P95_US");
    double limit = env ? strtod(env, NULL) : 1000.0;
    if (p95 > limit) {
        fprintf(stderr, "pointer CPU regression: p95 %.2f us > %.2f us\n", p95, limit);
        return 6;
    }

    printf("pointer ScrollRect: frames=%u drags=%u wheels=%u selected=%u draws=%u worst-batches=%u p50/p95/p99=%.2f/%.2f/%.2f us\n",
           FRAMES, drag_count, wheel_count, c.selected, ctx.draw_count, worst_batches, p50, p95, p99);
    puts("pointer click/drag/wheel + carousel binding regression OK");
    return 0;
}
