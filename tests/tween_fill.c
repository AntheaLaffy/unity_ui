#include "asmui.h"
#include "asmui_builder.h"
#include <math.h>
#include <stdio.h>

int main(void) {
    AsmUiContext ctx;
    AsmUiNode nodes[1] = {0};
    AsmUiDrawCmd draws[2] = {0};
    AsmUiTween tweens[1] = {0};
    asm_ui_init(&ctx, nodes, 1, draws, 2, tweens, 1);
    nodes[0] = asmui_node(ASMUI_NODE_IMAGE, -1, 0xffffffffu);
    nodes[0].image_mode = ASMUI_IMAGE_FILLED_HORIZONTAL;
    nodes[0].size_delta_x = 100;
    nodes[0].size_delta_y = 10;
    ctx.node_count = 1;
    tweens[0] = (AsmUiTween){0, ASMUI_PROP_FILL, ASMUI_EASE_LINEAR,
        ASMUI_TWEEN_ACTIVE, 0.0f, 1.0f, 0.0f, 1.0f};
    ctx.tween_count = 1;
    asm_ui_begin_frame(&ctx, 0.25f);
    asm_ui_tick_tweens(&ctx);
    if (fabsf(nodes[0].fill_amount - 0.25f) > 0.001f) {
        fprintf(stderr, "fill tween = %f\n", nodes[0].fill_amount);
        return 1;
    }
    puts("fill tween OK at t=.25");
    return 0;
}
