#ifndef ASMUI_TEXT_HB_H
#define ASMUI_TEXT_HB_H

#include "asmui_text.h"
#include <hb.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Atlas metric convention intentionally matches common MSDF atlas metadata:
   plane bounds are em-relative to the text baseline; UVs are normalized. */
typedef struct AsmUiHbAtlasGlyph {
    uint32_t texture_id;
    float plane_left, plane_bottom, plane_right, plane_top;
    float u0, v0, u1, v1;
} AsmUiHbAtlasGlyph;

typedef int (*AsmUiHbGlyphLookup)(uint32_t glyph_id,
                                  AsmUiHbAtlasGlyph *out,
                                  void *user);

typedef struct AsmUiHbShaper {
    hb_font_t *font;
    AsmUiHbGlyphLookup lookup;
    void *lookup_user;
    hb_language_t language; /* HB_LANGUAGE_INVALID => guessed/default */
} AsmUiHbShaper;

/* AsmUiShapeFn-compatible callback. Horizontal shaping, UTF-8 input. */
uint32_t asmui_hb_shape(const AsmUiDrawCmd *text_draw,
                        AsmUiGlyphQuad *glyphs,
                        uint32_t glyph_capacity,
                        void *user);

#ifdef __cplusplus
}
#endif
#endif
