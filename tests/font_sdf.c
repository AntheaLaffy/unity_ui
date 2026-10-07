#include "asmui_font_sdf_ft.h"
#include "demo/song_select_scene_text.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    const char *font = asmui_font_find_default();
    if (!font) {
        puts("font SDF check SKIP: no default font; set ASMUI_FONT=/path/to/font.ttf");
        return 0;
    }
    size_t sample_count = 0;
    const char *const *samples = asmui_song_text_samples(&sample_count);
    AsmUiFontSdfAtlas atlas = {0};
    if (!asmui_font_sdf_build(&atlas, font, 7u, 64u, samples, sample_count)) {
        fprintf(stderr, "font SDF atlas build failed: %s\n", font);
        return 1;
    }
    if (!atlas.rgba || atlas.width < 64u || atlas.height < 64u || atlas.glyph_count < 10u) {
        fprintf(stderr, "font SDF atlas sanity failed\n");
        asmui_font_sdf_destroy(&atlas);
        return 2;
    }

    AsmUiHbShaper shaper = asmui_font_sdf_shaper(&atlas);
    AsmUiDrawCmd d = {0};
    d.type = ASMUI_NODE_TEXT;
    d.payload = "NEON RHAPSODY";
    d.font_size = 42.0f;
    d.local_w = 500.0f;
    d.local_h = 72.0f;
    d.text_align = ASMUI_TEXT_LEFT;
    AsmUiGlyphQuad g[64] = {0};
    uint32_t n = asmui_hb_shape(&d, g, 64u, &shaper);
    if (n < 8u) {
        fprintf(stderr, "font shaping emitted too few glyphs: %u\n", n);
        asmui_font_sdf_destroy(&atlas);
        return 3;
    }
    for (uint32_t i = 0; i < n; ++i) {
        if (g[i].resource_id != 7u || !(g[i].u1 > g[i].u0) || !(g[i].v1 > g[i].v0)) {
            fprintf(stderr, "font glyph atlas lookup mismatch at %u\n", i);
            asmui_font_sdf_destroy(&atlas);
            return 4;
        }
    }
    (void)asmui_font_sdf_write_ppm(&atlas, "build/font_sdf_atlas.ppm");
    printf("font SDF OK: font=%s atlas=%ux%u glyphs=%u missing=%u shaped=%u px-range=%.1f\n",
           font, atlas.width, atlas.height, atlas.glyph_count,
           atlas.missing_glyph_count, n, atlas.px_range);
    asmui_font_sdf_destroy(&atlas);
    return 0;
}
