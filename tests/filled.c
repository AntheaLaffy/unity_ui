#include "asmui.h"
#include "asmui_builder.h"
#include <math.h>
#include <stdio.h>

static int nearf(float a, float b) { return fabsf(a-b) < 0.01f; }

int main(void) {
    AsmUiContext ctx;
    AsmUiNode nodes[2] = {0};
    AsmUiDrawCmd draws[4] = {0};
    AsmUiTween tweens[1] = {0};
    asm_ui_init(&ctx, nodes, 2, draws, 4, tweens, 1);
    asm_ui_set_viewport(&ctx, 400, 300);

    nodes[0] = asmui_node(ASMUI_NODE_IMAGE, -1, 0xFFFFFFFFu);
    nodes[0].size_delta_x = 200;
    nodes[0].size_delta_y = 40;
    nodes[0].image_mode = ASMUI_IMAGE_FILLED_HORIZONTAL;
    nodes[0].fill_amount = 0.25f;
    nodes[0].uv_x0 = 0.2f; nodes[0].uv_x1 = 0.8f;
    nodes[0].uv_y0 = 0.1f; nodes[0].uv_y1 = 0.9f;

    nodes[1] = asmui_node(ASMUI_NODE_IMAGE, -1, 0xFFFFFFFFu);
    nodes[1].size_delta_x = 40;
    nodes[1].size_delta_y = 200;
    nodes[1].image_mode = ASMUI_IMAGE_FILLED_VERTICAL;
    nodes[1].fill_amount = 0.5f;
    nodes[1].uv_x0 = 0.2f; nodes[1].uv_x1 = 0.8f;
    nodes[1].uv_y0 = 0.1f; nodes[1].uv_y1 = 0.9f;
    ctx.node_count = 2;

    asm_ui_begin_frame(&ctx, 0);
    asm_ui_layout(&ctx);
    asm_ui_build_drawlist(&ctx);

    if (ctx.draw_count != 2) return 1;
    // Horizontal: 25% of 200 = 50; U=.2 + .6*.25=.35
    if (!nearf(draws[0].x1 - draws[0].x0, 50.0f) || !nearf(draws[0].u1, 0.35f)) {
        fprintf(stderr, "horizontal fill mismatch width=%f u1=%f\n", draws[0].x1-draws[0].x0, draws[0].u1);
        return 2;
    }
    // Vertical: 50% of 200 = 100; top V=.9 - .8*.5=.5
    if (!nearf(draws[1].y0 - draws[1].y3, 100.0f) || !nearf(draws[1].v0, 0.5f)) {
        fprintf(stderr, "vertical fill mismatch height=%f v0=%f\n", draws[1].y3-draws[1].y0, draws[1].v0);
        return 3;
    }
    printf("filled image crop OK: h=%.1f%% v=%.1f%%\n", 25.0f, 50.0f);
    return 0;
}
