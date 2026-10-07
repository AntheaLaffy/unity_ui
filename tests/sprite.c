#include "asmui.h"
#include "asmui_builder.h"
#include <math.h>
#include <stdio.h>

static int nearf(float a, float b) { return fabsf(a-b) < 0.01f; }

int main(void) {
    AsmUiContext ctx;
    AsmUiNode nodes[4] = {0};
    AsmUiDrawCmd draws[32] = {0};
    AsmUiTween tweens[1] = {0};
    asm_ui_init(&ctx, nodes, 4, draws, 32, tweens, 1);
    asm_ui_set_viewport(&ctx, 800.0f, 600.0f);

    nodes[0] = asmui_node(ASMUI_NODE_IMAGE, -1, 0xFFFFFFFFu);
    nodes[0].size_delta_x = 300.0f;
    nodes[0].size_delta_y = 180.0f;
    nodes[0].material_id = ASMUI_MATERIAL_ADDITIVE;

    AsmUiSpriteRegion panel = {
        .texture_id = 7,
        .u0 = 0.25f, .v0 = 0.125f, .u1 = 0.75f, .v1 = 0.625f,
        .source_w = 200.0f, .source_h = 100.0f,
        .border_l = 20.0f, .border_r = 30.0f, .border_b = 10.0f, .border_t = 15.0f,
    };
    asmui_apply_sprite(&nodes[0], &panel, ASMUI_IMAGE_SLICED);
    ctx.node_count = 1;

    asm_ui_begin_frame(&ctx, 0.0f);
    asm_ui_layout(&ctx);
    asm_ui_build_drawlist(&ctx);

    if (ctx.draw_count != 9) {
        fprintf(stderr, "expected 9 slices, got %u\n", ctx.draw_count);
        return 1;
    }

    const AsmUiDrawCmd *bl = &ctx.draws[0];
    const AsmUiDrawCmd *center = &ctx.draws[4];
    const AsmUiDrawCmd *tr = &ctx.draws[8];

    // Atlas split: u1 = .25 + .5*(20/200) = .30; right split=.675.
    if (!nearf(bl->u0, .25f) || !nearf(bl->u1, .30f) ||
        !nearf(center->u0, .30f) || !nearf(center->u1, .675f) ||
        !nearf(tr->u0, .675f) || !nearf(tr->u1, .75f)) {
        fprintf(stderr, "nine-slice U coordinates are wrong\n");
        return 2;
    }
    if (bl->resource_id != 7 || bl->material_id != ASMUI_MATERIAL_ADDITIVE) {
        fprintf(stderr, "resource/material metadata not propagated\n");
        return 3;
    }

    printf("nine-slice draw_count=%u, u=[%.3f %.3f %.3f %.3f]\n",
           ctx.draw_count, bl->u0, bl->u1, center->u1, tr->u1);
    /* Real OpenSekai atlas panels have borders meeting at one texel. Their
       center quads must not fall back to sampling the entire atlas. */
    nodes[0].image_mode = ASMUI_IMAGE_SIMPLE;
    nodes[0].uv_x0 = nodes[0].uv_x1 = .75f;
    nodes[0].uv_y0 = nodes[0].uv_y1 = .25f;
    asm_ui_begin_frame(&ctx, 0.0f);
    asm_ui_build_drawlist(&ctx);
    if (ctx.draw_count != 1 || draws[0].u0 != .75f || draws[0].u1 != .75f ||
        draws[0].v0 != .25f || draws[0].v1 != .25f) {
        fprintf(stderr, "explicit single-texel UVs were replaced\n");
        return 4;
    }
    nodes[0].flags &= ~ASMUI_UV_EXPLICIT;
    asm_ui_begin_frame(&ctx, 0.0f);
    asm_ui_build_drawlist(&ctx);
    if (draws[0].u0 != 0 || draws[0].v0 != 0 || draws[0].u1 != 1 || draws[0].v1 != 1) {
        fprintf(stderr, "legacy unspecified UV fallback changed\n");
        return 5;
    }
    puts("explicit zero-span atlas UV and legacy defaults OK");
    return 0;
}
