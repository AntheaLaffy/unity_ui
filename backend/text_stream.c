#include "asmui_text.h"
#include <string.h>

uint32_t asmui_expand_text_stream(const AsmUiContext *src,
                                  AsmUiDrawCmd *out,
                                  uint32_t out_capacity,
                                  AsmUiShapeFn shape,
                                  void *user,
                                  AsmUiGlyphQuad *glyph_scratch,
                                  uint32_t glyph_scratch_capacity) {
    if (!src || !out || !out_capacity) return 0;
    uint32_t written = 0;
    for (uint32_t i = 0; i < src->draw_count && written < out_capacity; ++i) {
        const AsmUiDrawCmd *d = &src->draws[i];
        if (d->type != ASMUI_NODE_TEXT) {
            out[written++] = *d;
            continue;
        }
        if (!shape || !glyph_scratch || !glyph_scratch_capacity) continue;
        uint32_t n = shape(d, glyph_scratch, glyph_scratch_capacity, user);
        if (!n) continue;
        uint32_t left = out_capacity - written;
        uint32_t emitted = asm_ui_expand_text_draw(d, glyph_scratch, n, out + written, left);
        written += emitted;
    }
    return written;
}
