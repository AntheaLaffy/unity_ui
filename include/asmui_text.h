#ifndef ASMUI_TEXT_H
#define ASMUI_TEXT_H

#include "asmui.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Shape callback: consume one ASMUI_NODE_TEXT DrawCmd and return glyph quads
   in text-node local pixels. */
typedef uint32_t (*AsmUiShapeFn)(const AsmUiDrawCmd *text_draw,
                                 AsmUiGlyphQuad *glyphs,
                                 uint32_t glyph_capacity,
                                 void *user);

/* Preserves painter order while replacing each text placeholder with its shaped
   glyph quads. Non-text commands are copied verbatim. Returns commands written. */
uint32_t asmui_expand_text_stream(const AsmUiContext *src,
                                  AsmUiDrawCmd *out,
                                  uint32_t out_capacity,
                                  AsmUiShapeFn shape,
                                  void *user,
                                  AsmUiGlyphQuad *glyph_scratch,
                                  uint32_t glyph_scratch_capacity);

#ifdef __cplusplus
}
#endif
#endif
