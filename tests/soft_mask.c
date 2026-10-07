#include "asmui.h"
#include "asmui_builder.h"
#include <math.h>
#include <stdio.h>

static int nearf(float a, float b) { return fabsf(a-b) < 0.001f; }

int main(void) {
    AsmUiNode nodes[2] = {0};
    AsmUiDrawCmd draws[8] = {0};
    AsmUiTween tweens[1] = {0};
    AsmUiContext ctx;
    asm_ui_init(&ctx, nodes, 2, draws, 8, tweens, 1);
    ctx.node_count = 2;
    asm_ui_set_viewport(&ctx, 800.0f, 600.0f);

    nodes[0] = asmui_node(ASMUI_NODE_CONTAINER, -1, 0xffffffffu);
    nodes[0].size_delta_x = 200.0f;
    nodes[0].size_delta_y = 100.0f;
    asmui_soft_mask(&nodes[0], 18.0f, 2.5f);

    nodes[1] = asmui_node(ASMUI_NODE_IMAGE, 0, 0xffffffffu);
    nodes[1].size_delta_x = 260.0f;
    nodes[1].size_delta_y = 80.0f;

    asm_ui_layout(&ctx);
    asm_ui_build_drawlist(&ctx);
    if (ctx.draw_count != 1) return 1;
    AsmUiDrawCmd *d = &draws[0];
    if (!(d->mask_flags & ASMUI_DRAW_HAS_SOFT_MASK) ||
        !nearf(d->mask_x, 300.0f) || !nearf(d->mask_y, 250.0f) ||
        !nearf(d->mask_w, 200.0f) || !nearf(d->mask_h, 100.0f) ||
        !nearf(d->mask_radius, 18.0f) || !nearf(d->mask_softness, 2.5f)) {
        fprintf(stderr, "soft mask mismatch: flags=%u rect=(%.1f %.1f %.1f %.1f) r=%.1f s=%.1f\n",
                d->mask_flags, d->mask_x,d->mask_y,d->mask_w,d->mask_h,
                d->mask_radius,d->mask_softness);
        return 2;
    }
    puts("soft-mask inheritance OK: rounded parent mask propagated to child draw");
    return 0;
}
