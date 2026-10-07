#include "asmui.h"
#include "asmui_builder.h"
#include <stdio.h>
#include <string.h>

static AsmUiNode node_image(int parent, uint32_t color) {
    return asmui_node(ASMUI_NODE_IMAGE, parent, color);
}

int main(void) {
    AsmUiContext ctx;
    AsmUiNode nodes[16] = {0};
    AsmUiDrawCmd draws[32] = {0};
    AsmUiTween tweens[8] = {0};
    asm_ui_init(&ctx, nodes, 16, draws, 32, tweens, 8);
    asm_ui_set_viewport(&ctx, 1920.0f, 1080.0f);

    nodes[0] = node_image(-1, 0x20243CFFu);
    nodes[0].anchor_min_x = 0.0f; nodes[0].anchor_min_y = 0.0f;
    nodes[0].anchor_max_x = 1.0f; nodes[0].anchor_max_y = 1.0f;
    nodes[0].size_delta_x = 0.0f; nodes[0].size_delta_y = 0.0f;

    nodes[1] = node_image(0, 0xF4D35EFFu);
    nodes[1].anchored_x = 220.0f;
    nodes[1].size_delta_x = 520.0f;
    nodes[1].size_delta_y = 180.0f;
    nodes[1].rotation = 0.08f;

    nodes[2] = node_image(0, 0xEE6C4DFFu);
    nodes[2].anchored_x = -280.0f;
    nodes[2].anchored_y = -250.0f;
    nodes[2].size_delta_x = 300.0f;
    nodes[2].size_delta_y = 90.0f;

    ctx.node_count = 3;
    tweens[0] = (AsmUiTween){
        .node_index = 1, .property = ASMUI_PROP_ANCHORED_Y,
        .easing = ASMUI_EASE_OUT_CUBIC, .flags = ASMUI_TWEEN_ACTIVE,
        .start_time = 0.0f, .duration = 0.5f, .from = -60.0f, .to = 40.0f
    };
    ctx.tween_count = 1;

    asm_ui_begin_frame(&ctx, 0.25f);
    asm_ui_tick_tweens(&ctx);
    asm_ui_layout(&ctx);
    asm_ui_build_drawlist(&ctx);

    printf("draw_count=%u\n", ctx.draw_count);
    for (uint32_t i = 0; i < ctx.draw_count; ++i) {
        const AsmUiDrawCmd *d = &ctx.draws[i];
        printf("%u: p0=(%.1f,%.1f) p2=(%.1f,%.1f) alpha=%.2f\n",
               i, d->x0, d->y0, d->x2, d->y2, d->alpha);
    }
    return ctx.draw_count == 3 ? 0 : 1;
}
