#include "asmui.h"
#include "asmui_text.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static int nearf(float a, float b) { return fabsf(a-b) < 0.001f; }

static uint32_t fake_shape(const AsmUiDrawCmd *text, AsmUiGlyphQuad *g,
                           uint32_t cap, void *user) {
    (void)text; (void)user;
    if (cap < 2) return 0;
    g[0] = (AsmUiGlyphQuad){10,20,30,40, .10f,.20f,.20f,.40f, 7,0};
    g[1] = (AsmUiGlyphQuad){35,20,55,40, .20f,.20f,.30f,.40f, 7,0};
    return 2;
}

int main(void) {
    AsmUiDrawCmd text = {0}, glyph_draws[4] = {0};
    AsmUiGlyphQuad glyphs[2];
    text.type = ASMUI_NODE_TEXT;
    text.rgba = 0xffffffffu;
    text.alpha = 0.75f;
    text.x0=100; text.y0=200; text.x1=300; text.y1=200;
    text.x2=300; text.y2=300; text.x3=100; text.y3=300;
    text.local_w=200; text.local_h=100;
    text.clip_x=0; text.clip_y=0; text.clip_w=800; text.clip_h=600;
    text.material0=4.0f;
    fake_shape(&text, glyphs, 2, NULL);

    uint32_t n = asm_ui_expand_text_draw(&text, glyphs, 2, glyph_draws, 4);
    if (n != 2) return 1;
    if (glyph_draws[0].type != ASMUI_NODE_GLYPH ||
        glyph_draws[0].resource_id != 7 ||
        glyph_draws[0].material_id != ASMUI_MATERIAL_MSDF_TEXT ||
        !nearf(glyph_draws[0].x0,110) || !nearf(glyph_draws[0].y0,220) ||
        !nearf(glyph_draws[0].x2,130) || !nearf(glyph_draws[0].y2,240) ||
        !nearf(glyph_draws[0].u0,.10f) || !nearf(glyph_draws[0].v1,.40f)) {
        fprintf(stderr, "ASM glyph expansion mismatch\n");
        return 2;
    }

    AsmUiDrawCmd src_draws[3] = {0}, out[8] = {0};
    AsmUiGlyphQuad scratch[4] = {0};
    src_draws[0].type = ASMUI_NODE_IMAGE;
    src_draws[1] = text;
    src_draws[2].type = ASMUI_NODE_IMAGE;
    AsmUiContext src = {0};
    src.draws = src_draws; src.draw_count = 3;
    uint32_t m = asmui_expand_text_stream(&src, out, 8, fake_shape, NULL, scratch, 4);
    if (m != 4 || out[0].type != ASMUI_NODE_IMAGE ||
        out[1].type != ASMUI_NODE_GLYPH || out[2].type != ASMUI_NODE_GLYPH ||
        out[3].type != ASMUI_NODE_IMAGE) {
        fprintf(stderr, "painter-order text stream expansion mismatch\n");
        return 3;
    }

    AsmUiContext expanded = {0};
    expanded.draws = out; expanded.draw_count = m;
    AsmUiBatch batches[8] = {0};
    uint32_t bn = asm_ui_build_batches(&expanded, batches, 8);
    if (bn != 3 || batches[1].type != ASMUI_NODE_GLYPH || batches[1].draw_count != 2) {
        fprintf(stderr, "glyph batching mismatch: %u batches\n", bn);
        return 4;
    }
    puts("MSDF text path OK: shaped glyphs -> ASM world quads -> painter-order batch");
    return 0;
}
