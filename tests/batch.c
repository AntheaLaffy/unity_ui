#include "asmui.h"
#include <stdio.h>
#include <string.h>

static void cmd(AsmUiDrawCmd *d, uint32_t type, uint32_t tex, uint32_t mat,
                float cx, float cy, float cw, float ch) {
    memset(d, 0, sizeof(*d));
    d->type = type;
    d->resource_id = tex;
    d->material_id = mat;
    d->clip_x = cx; d->clip_y = cy; d->clip_w = cw; d->clip_h = ch;
}

int main(void) {
    AsmUiContext ctx = {0};
    AsmUiDrawCmd draws[7];
    AsmUiBatch batches[7] = {0};
    ctx.draws = draws;
    ctx.draw_count = 7;

    cmd(&draws[0], ASMUI_NODE_IMAGE, 1, ASMUI_MATERIAL_DEFAULT, 0,0,100,100);
    cmd(&draws[1], ASMUI_NODE_IMAGE, 1, ASMUI_MATERIAL_DEFAULT, 0,0,100,100);
    cmd(&draws[2], ASMUI_NODE_IMAGE, 2, ASMUI_MATERIAL_DEFAULT, 0,0,100,100);
    cmd(&draws[3], ASMUI_NODE_IMAGE, 1, ASMUI_MATERIAL_DEFAULT, 0,0,100,100);
    cmd(&draws[4], ASMUI_NODE_IMAGE, 1, ASMUI_MATERIAL_DEFAULT, 0,0,100,100);
    cmd(&draws[5], ASMUI_NODE_IMAGE, 1, ASMUI_MATERIAL_DEFAULT, 0,0,80,100); // clip splits
    cmd(&draws[6], ASMUI_NODE_IMAGE, 1, ASMUI_MATERIAL_ADDITIVE, 0,0,80,100); // material splits

    uint32_t n = asm_ui_build_batches(&ctx, batches, 7);
    if (n != 5) {
        fprintf(stderr, "expected 5 batches, got %u\n", n);
        return 1;
    }
    const uint32_t expected_first[5] = {0,2,3,5,6};
    const uint32_t expected_count[5] = {2,1,2,1,1};
    for (uint32_t i=0;i<n;i++) {
        if (batches[i].first_draw != expected_first[i] ||
            batches[i].draw_count != expected_count[i]) {
            fprintf(stderr, "batch %u mismatch first=%u count=%u\n",
                    i, batches[i].first_draw, batches[i].draw_count);
            return 2;
        }
    }
    puts("painter-order adjacent batching OK: 7 draws -> 5 batches");
    return 0;
}
