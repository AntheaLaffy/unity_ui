#include "asmui.h"
#include "asmui_builder.h"
#include <math.h>
#include <stdio.h>

static int nearf(float a, float b) { return fabsf(a - b) < 0.01f; }

int main(void) {
    AsmUiContext ctx;
    AsmUiNode nodes[4] = {0};
    AsmUiDrawCmd draws[8] = {0};
    AsmUiTween tweens[1] = {0};
    AsmUiBatch batches[8] = {0};
    AsmUiGpuVertex node_vertices[16] = {0};

    asm_ui_init(&ctx, nodes, 4, draws, 8, tweens, 1);
    asm_ui_set_viewport(&ctx, 640.0f, 360.0f);

    nodes[0] = asmui_node(ASMUI_NODE_CONTAINER, -1, 0xffffffffu);
    nodes[0].size_delta_x = 240.0f;
    nodes[0].size_delta_y = 180.0f;
    nodes[0].rotation = 0.15f;
    asmui_stencil_mask(&nodes[0]);

    nodes[1] = asmui_node(ASMUI_NODE_CONTAINER, 0, 0xffffffffu);
    nodes[1].size_delta_x = 140.0f;
    nodes[1].size_delta_y = 100.0f;
    asmui_stencil_mask(&nodes[1]);

    nodes[2] = asmui_node(ASMUI_NODE_IMAGE, 1, 0xff4040ffu);
    nodes[2].size_delta_x = 180.0f;
    nodes[2].size_delta_y = 120.0f;

    nodes[3] = asmui_node(ASMUI_NODE_IMAGE, 0, 0x40ff40ffu);
    nodes[3].size_delta_x = 180.0f;
    nodes[3].size_delta_y = 120.0f;

    ctx.node_count = 4;
    asm_ui_layout(&ctx);
    asm_ui_build_drawlist(&ctx);
    if (ctx.draw_count != 2) {
        fprintf(stderr, "expected 2 visual draws, got %u\n", ctx.draw_count);
        return 1;
    }
    if (draws[0].stencil_depth != 2 || draws[0].stencil_parent != 2) {
        fprintf(stderr, "nested stencil mismatch: depth=%u parent=%u\n",
                draws[0].stencil_depth, draws[0].stencil_parent);
        return 2;
    }
    if (draws[1].stencil_depth != 1 || draws[1].stencil_parent != 1) {
        fprintf(stderr, "outer stencil mismatch: depth=%u parent=%u\n",
                draws[1].stencil_depth, draws[1].stencil_parent);
        return 3;
    }

    uint32_t batch_count = asm_ui_build_batches(&ctx, batches, 8);
    if (batch_count != 2 || batches[0].stencil_depth != 2 || batches[1].stencil_depth != 1) {
        fprintf(stderr, "stencil-aware batching mismatch: batches=%u depths=%u,%u\n",
                batch_count, batches[0].stencil_depth, batches[1].stencil_depth);
        return 4;
    }

    uint32_t nv = asm_ui_build_node_vertices(&ctx, node_vertices, 16);
    if (nv != 16) {
        fprintf(stderr, "node vertex count mismatch: %u\n", nv);
        return 5;
    }
    if (!nearf(node_vertices[0].x, nodes[0].m02) ||
        !nearf(node_vertices[0].y, nodes[0].m12)) {
        fprintf(stderr, "node quad p0 mismatch: got %.3f,%.3f expected %.3f,%.3f\n",
                node_vertices[0].x, node_vertices[0].y, nodes[0].m02, nodes[0].m12);
        return 6;
    }

    puts("stencil core OK: nested ancestry -> batch key -> stable ASM node quads");
    return 0;
}
