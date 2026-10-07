#include "asmui.h"
#include "asmui_builder.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    AsmUiNode nodes[6] = {0};
    AsmUiDrawCmd draws[32] = {0};
    AsmUiTween tweens[1] = {0};
    AsmUiContext ctx;
    asm_ui_init(&ctx, nodes, 6, draws, 32, tweens, 1);
    ctx.node_count = 6;
    asm_ui_set_viewport(&ctx, 1280, 720);

    nodes[0] = asmui_node(ASMUI_NODE_IMAGE, -1, 0xffffffffu); // BG
    asmui_stretch(&nodes[0]);

    nodes[1] = asmui_node(ASMUI_NODE_CONTAINER, -1, 0xffffffffu); // layer root
    nodes[1].size_delta_x = 600; nodes[1].size_delta_y = 300;
    asmui_offscreen_layer(&nodes[1]);

    nodes[2] = asmui_node(ASMUI_NODE_IMAGE, 1, 0xff0000ffu);
    nodes[2].size_delta_x = 200; nodes[2].size_delta_y = 100;
    nodes[2].anchored_x = -120;

    nodes[3] = asmui_node(ASMUI_NODE_IMAGE, 1, 0x00ff00ffu);
    nodes[3].size_delta_x = 200; nodes[3].size_delta_y = 100;
    nodes[3].anchored_x = 120;

    nodes[4] = asmui_node(ASMUI_NODE_IMAGE, -1, 0x0000ffffu); // foreground
    nodes[4].size_delta_x = 80; nodes[4].size_delta_y = 80;

    nodes[5] = asmui_node(ASMUI_NODE_CONTAINER, -1, 0xffffffffu); // invisible/no draw

    asm_ui_layout(&ctx);
    asm_ui_build_drawlist(&ctx);
    assert(ctx.draw_count == 4);
    assert(draws[0].source_node == 1);
    assert(draws[1].source_node == 3);
    assert(draws[2].source_node == 4);
    assert(draws[3].source_node == 5);

    AsmUiRenderOp ops[16] = {0};
    uint32_t n = asmui_build_render_ops(&ctx, ops, 16);
    assert(n == 6);

    assert(ops[0].type == ASMUI_RENDER_DRAW_RANGE && ops[0].first_draw == 0 && ops[0].draw_count == 1);
    assert(ops[1].type == ASMUI_RENDER_BEGIN_LAYER && ops[1].node_index == 1);
    assert(ops[2].type == ASMUI_RENDER_DRAW_RANGE && ops[2].node_index == 1 && ops[2].first_draw == 1 && ops[2].draw_count == 2);
    assert(ops[3].type == ASMUI_RENDER_END_LAYER && ops[3].node_index == 1);
    assert(ops[4].type == ASMUI_RENDER_COMPOSITE_LAYER && ops[4].node_index == 1);
    assert(ops[5].type == ASMUI_RENDER_DRAW_RANGE && ops[5].first_draw == 3 && ops[5].draw_count == 1);

    printf("offscreen render-op planning OK: 4 draws -> %u ops (1 layer, painter order preserved)\n", n);
    return 0;
}
