#include "asmui.h"
#include "asmui_interaction.h"
#include "demo/song_select_scene_text.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static int materialize(AsmUiNode *nodes, const AsmUiSongTextSceneInfo *s,
                       AsmUiVirtualCarousel *v) {
    for (uint32_t slot = 0; slot < s->carousel_count; ++slot) {
        uint32_t logical = 0;
        asmui_virtual_carousel_apply_slot(v, slot, &nodes[s->carousel_root[slot]], &logical);
        if (!asmui_song_carousel_bind_slot(nodes, s, slot, logical)) return 0;
    }
    return asmui_song_select_bind_logical(nodes, s, v->motion.selected);
}

int main(void) {
    static AsmUiNode nodes[ASMUI_SONG_MAX_NODES];
    static AsmUiDrawCmd draws[ASMUI_SONG_MAX_NODES * 2u];
    static AsmUiTween tweens[8];
    static AsmUiBatch batches[ASMUI_SONG_MAX_NODES * 2u];

    AsmUiSongTextSceneInfo s = asmui_song_select_build_text(nodes, ASMUI_SONG_MAX_NODES, 8.0f);
    if (s.carousel_count != 7u || s.carousel_viewport_node >= s.base.node_count) return 1;
    for (uint32_t i = 0; i < s.carousel_count; ++i)
        if (nodes[s.carousel_root[i]].parent != (int32_t)s.carousel_viewport_node) return 2;

    AsmUiContext ctx;
    asm_ui_init(&ctx, nodes, ASMUI_SONG_MAX_NODES, draws, ASMUI_SONG_MAX_NODES * 2u, tweens, 8u);
    asm_ui_set_viewport(&ctx, 1280.0f, 720.0f);
    ctx.node_count = s.base.node_count;

    AsmUiVirtualCarousel v;
    asmui_virtual_carousel_init(&v, 100000u, 50000u, s.carousel_count,
                                90.0f, -318.0f, 0.0f, 0.0f, 156.0f, 125.0f);
    if (!materialize(nodes, &s, &v)) return 3;

    uint32_t logical = 0;
    AsmUiCarouselItemVisual vis;
    if (!asmui_virtual_carousel_slot(&v, 3u, &logical, &vis) || logical != 50000u) return 4;
    if (fabsf(vis.x) > 0.01f || fabsf(vis.y) > 0.01f) return 5;

    asm_ui_begin_frame(&ctx, 1.0f / 120.0f);
    asm_ui_layout(&ctx);
    asm_ui_build_drawlist(&ctx);
    uint32_t bc = asm_ui_build_batches(&ctx, batches, ASMUI_SONG_MAX_NODES * 2u);
    if (ctx.node_count > 80u || ctx.draw_count > 80u || bc > 64u) return 6;

    const AsmUiNode *vp = &nodes[s.carousel_viewport_node];
    const AsmUiNode *root = &nodes[s.carousel_root[3]];
    if (root->clip_w > vp->width + 0.01f || root->clip_h > vp->height + 0.01f ||
        root->clip_h < 90.0f) return 7;

    asmui_virtual_carousel_select(&v, 99999u);
    v.motion.scroll.position = v.motion.scroll.target;
    if (!materialize(nodes, &s, &v)) return 8;
    if (!asmui_virtual_carousel_slot(&v, 6u, &logical, &vis) || logical != 99999u) return 9;
    if (!asmui_virtual_carousel_slot(&v, 0u, &logical, &vis) || logical != 99993u) return 10;

    int hit = asmui_virtual_carousel_hit_test(&v,
        v.viewport_x + vis.x, v.viewport_y + vis.y, 142.0f, 66.0f);
    if (hit < 0) return 11;

    printf("virtual ScrollRect OK: logical=%u retained-slots=%u nodes=%u draws=%u batches=%u viewport=%.0fx%.0f tail=[99993..99999]\n",
           v.logical_count, v.slot_count, ctx.node_count, ctx.draw_count, bc, vp->width, vp->height);
    return 0;
}
