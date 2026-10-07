#ifndef ASMUI_FONT_SDF_FT_H
#define ASMUI_FONT_SDF_FT_H

#include "asmui_text_hb.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A compact runtime SDF atlas built from only the glyphs needed by a scene.
 * FreeType is used only at atlas-build time; HarfBuzz remains the shaper.
 * The atlas stores one-channel SDF replicated to RGBA so it can use the
 * existing ASMUI_MATERIAL_MSDF_TEXT shader path (median(R,G,B) == SDF). */
typedef struct AsmUiFontSdfEntry {
    uint32_t glyph_id;
    AsmUiHbAtlasGlyph atlas;
} AsmUiFontSdfEntry;

typedef struct AsmUiFontSdfAtlas {
    uint8_t *rgba;
    uint32_t width;
    uint32_t height;
    uint32_t texture_id;
    uint32_t glyph_count;
    uint32_t glyph_capacity;
    uint32_t missing_glyph_count;
    float em_pixels;
    float px_range;

    AsmUiFontSdfEntry *glyphs;
    void *ft_library;
    void *ft_face;
    hb_font_t *hb_font;
} AsmUiFontSdfAtlas;

/* Checks ASMUI_FONT first, then common Arch/Linux font locations. The returned
 * pointer is process-lifetime storage and must not be freed. */
const char *asmui_font_find_default(void);

/* Desktop preference: user-installed LXGW WenKai, then the shared fallback. */
const char *asmui_font_find_desktop(void);

/* Build an RGBA8 SDF atlas for exactly the glyphs reached by shaping `texts`.
 * em_pixels=64 is a good default. texture_id becomes each glyph resource id.
 * Returns non-zero on success. */
int asmui_font_sdf_build(AsmUiFontSdfAtlas *atlas,
                         const char *font_path,
                         uint32_t texture_id,
                         uint32_t em_pixels,
                         const char *const *texts,
                         size_t text_count);

void asmui_font_sdf_destroy(AsmUiFontSdfAtlas *atlas);

/* Rasterize only glyphs absent from an existing same-font lookup. The font
 * path and em_pixels must match that lookup. Useful for immutable input pages
 * without rebuilding all interface glyphs. Returns 0 if no bitmap is added. */
int asmui_font_sdf_build_missing(AsmUiFontSdfAtlas *atlas, const char *font_path,
                                 uint32_t texture_id, uint32_t em_pixels,
                                 const char *const *texts, size_t text_count,
                                 AsmUiHbGlyphLookup existing, void *existing_user);

/* AsmUiHbGlyphLookup-compatible callback. */
int asmui_font_sdf_lookup(uint32_t glyph_id,
                          AsmUiHbAtlasGlyph *out,
                          void *user);

/* Convenience wrapper for asmui_hb_shape(). */
AsmUiHbShaper asmui_font_sdf_shaper(AsmUiFontSdfAtlas *atlas);

/* Writes the raw atlas as PPM for quick inspection. */
int asmui_font_sdf_write_ppm(const AsmUiFontSdfAtlas *atlas,
                             const char *path);

#ifdef __cplusplus
}
#endif
#endif
