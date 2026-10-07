#include "asmui_font_sdf_ft.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MODULE_H
#include <hb-ft.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct RasterGlyph {
    uint32_t glyph_id;
    uint32_t width;
    uint32_t height;
    int left;
    int top;
    uint8_t *pixels;
} RasterGlyph;

static int glyph_seen(const uint32_t *ids, uint32_t n, uint32_t id) {
    for (uint32_t i = 0; i < n; ++i) if (ids[i] == id) return 1;
    return 0;
}

static uint32_t next_pow2(uint32_t x) {
    if (x <= 1u) return 1u;
    --x;
    x |= x >> 1; x |= x >> 2; x |= x >> 4; x |= x >> 8; x |= x >> 16;
    return x + 1u;
}

const char *asmui_font_find_default(void) {
    const char *env = getenv("ASMUI_FONT");
    if (env && *env && access(env, R_OK) == 0) return env;
    static const char *const candidates[] = {
        "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/TTF/NotoSans-Regular.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/fonts/fonts-go/Go-Regular.ttf",
    };
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i)
        if (access(candidates[i], R_OK) == 0) return candidates[i];
    return NULL;
}

const char *asmui_font_find_desktop(void) {
    const char *env=getenv("ASMUI_FONT");
    if(env&&*env&&access(env,R_OK)==0)return env;
    static char desktop_font[4096];
    const char *data = getenv("XDG_DATA_HOME"), *user_home = getenv("HOME");
    if (data && snprintf(desktop_font, sizeof(desktop_font), "%s/fonts/LXGWWenKai-Regular.ttf", data) > 0 &&
        access(desktop_font, R_OK) == 0) return desktop_font;
    if (user_home && snprintf(desktop_font, sizeof(desktop_font), "%s/.local/share/fonts/LXGWWenKai-Regular.ttf", user_home) > 0 &&
        access(desktop_font, R_OK) == 0) return desktop_font;
    return asmui_font_find_default();
}

static int collect_glyphs(hb_font_t *font,
                          const char *const *texts,
                          size_t text_count,
                          uint32_t **out_ids,
                          uint32_t *out_count,
                          uint32_t *out_missing) {
    uint32_t cap = 64u, count = 0u, missing = 0u;
    uint32_t *ids = malloc((size_t)cap * sizeof(*ids));
    if (!ids) return 0;

    for (size_t t = 0; t < text_count; ++t) {
        if (!texts[t] || !*texts[t]) continue;
        hb_buffer_t *buf = hb_buffer_create();
        if (!buf) { free(ids); return 0; }
        hb_buffer_add_utf8(buf, texts[t], -1, 0, -1);
        hb_buffer_guess_segment_properties(buf);
        hb_shape(font, buf, NULL, 0);
        unsigned int n = 0;
        hb_glyph_info_t *info = hb_buffer_get_glyph_infos(buf, &n);
        for (unsigned int i = 0; i < n; ++i) {
            uint32_t gid = info[i].codepoint;
            if (!gid) ++missing;
            if (glyph_seen(ids, count, gid)) continue;
            if (count == cap) {
                uint32_t new_cap = cap * 2u;
                uint32_t *p = realloc(ids, (size_t)new_cap * sizeof(*p));
                if (!p) { hb_buffer_destroy(buf); free(ids); return 0; }
                ids = p; cap = new_cap;
            }
            ids[count++] = gid;
        }
        hb_buffer_destroy(buf);
    }
    *out_ids = ids;
    *out_count = count;
    *out_missing = missing;
    return count != 0u;
}

static void free_rasters(RasterGlyph *r, uint32_t n) {
    if (!r) return;
    for (uint32_t i = 0; i < n; ++i) free(r[i].pixels);
    free(r);
}

static int build_atlas(AsmUiFontSdfAtlas *a,
                         const char *font_path,
                         uint32_t texture_id,
                         uint32_t em_pixels,
                         const char *const *texts,
                         size_t text_count,
                         AsmUiHbGlyphLookup existing,
                         void *existing_user) {
    if (!a || !font_path || !texts || !text_count) return 0;
    memset(a, 0, sizeof(*a));
    if (em_pixels < 24u) em_pixels = 24u;
    if (em_pixels > 192u) em_pixels = 192u;

    FT_Library lib = NULL;
    FT_Face face = NULL;
    if (FT_Init_FreeType(&lib) != 0) return 0;
    if (FT_New_Face(lib, font_path, 0, &face) != 0) {
        FT_Done_FreeType(lib); return 0;
    }
    if (FT_Set_Pixel_Sizes(face, 0, em_pixels) != 0) {
        FT_Done_Face(face); FT_Done_FreeType(lib); return 0;
    }

    /* FreeType's SDF renderer defaults to an 8-pixel spread. Request it
       explicitly where supported so the shader px_range has a stable meaning. */
    unsigned int spread = 8u;
    (void)FT_Property_Set(lib, "sdf", "spread", &spread);

    hb_font_t *font = hb_ft_font_create_referenced(face);
    if (!font) {
        FT_Done_Face(face); FT_Done_FreeType(lib); return 0;
    }
    hb_font_set_scale(font, (int)em_pixels * 64, (int)em_pixels * 64);

    uint32_t *ids = NULL, id_count = 0, missing = 0;
    if (!collect_glyphs(font, texts, text_count, &ids, &id_count, &missing)) {
        hb_font_destroy(font); FT_Done_Face(face); FT_Done_FreeType(lib); return 0;
    }

    RasterGlyph *rasters = calloc(id_count, sizeof(*rasters));
    if (!rasters) {
        free(ids); hb_font_destroy(font); FT_Done_Face(face); FT_Done_FreeType(lib); return 0;
    }

    uint32_t raster_count = 0;
    uint32_t widest = 0;
    uint64_t area = 0;
    for (uint32_t i = 0; i < id_count; ++i) {
        uint32_t gid = ids[i];
        AsmUiHbAtlasGlyph present;
        if (existing && existing(gid, &present, existing_user)) continue;
        if (FT_Load_Glyph(face, gid, FT_LOAD_DEFAULT | FT_LOAD_NO_BITMAP) != 0) continue;
        if (FT_Render_Glyph(face->glyph, FT_RENDER_MODE_SDF) != 0) continue;
        FT_Bitmap *bm = &face->glyph->bitmap;
        if (!bm->width || !bm->rows || !bm->buffer) continue; /* spaces etc. */
        if (bm->pixel_mode != FT_PIXEL_MODE_GRAY) continue;

        RasterGlyph *r = &rasters[raster_count];
        r->glyph_id = gid;
        r->width = bm->width;
        r->height = bm->rows;
        r->left = face->glyph->bitmap_left;
        r->top = face->glyph->bitmap_top;
        r->pixels = malloc((size_t)r->width * r->height);
        if (!r->pixels) { free(ids); free_rasters(rasters, raster_count); hb_font_destroy(font); FT_Done_Face(face); FT_Done_FreeType(lib); return 0; }
        for (uint32_t y = 0; y < r->height; ++y) {
            const uint8_t *src;
            if (bm->pitch >= 0) src = bm->buffer + (size_t)y * (size_t)bm->pitch;
            else src = bm->buffer + (size_t)(r->height - 1u - y) * (size_t)(-bm->pitch);
            memcpy(r->pixels + (size_t)y * r->width, src, r->width);
        }
        if (r->width > widest) widest = r->width;
        area += (uint64_t)(r->width + 4u) * (uint64_t)(r->height + 4u);
        ++raster_count;
    }
    free(ids);
    if (!raster_count) {
        free_rasters(rasters, raster_count); hb_font_destroy(font); FT_Done_Face(face); FT_Done_FreeType(lib); return 0;
    }

    uint32_t atlas_w = 512u;
    while (atlas_w < widest + 4u) atlas_w <<= 1;
    while ((uint64_t)atlas_w * atlas_w < area * 2u && atlas_w < 2048u) atlas_w <<= 1;
    if (atlas_w > 2048u) atlas_w = 2048u;

    uint32_t x = 2u, y = 2u, row_h = 0u;
    uint32_t *px = calloc(raster_count, sizeof(*px));
    uint32_t *py = calloc(raster_count, sizeof(*py));
    if (!px || !py) {
        free(px); free(py); free_rasters(rasters, raster_count); hb_font_destroy(font); FT_Done_Face(face); FT_Done_FreeType(lib); return 0;
    }
    for (uint32_t i = 0; i < raster_count; ++i) {
        RasterGlyph *r = &rasters[i];
        if (x + r->width + 2u > atlas_w) {
            x = 2u; y += row_h + 4u; row_h = 0u;
        }
        px[i] = x; py[i] = y;
        x += r->width + 4u;
        if (r->height > row_h) row_h = r->height;
    }
    uint32_t atlas_h = next_pow2(y + row_h + 2u);
    if (atlas_h < 64u) atlas_h = 64u;
    if (atlas_h > 4096u) {
        free(px); free(py); free_rasters(rasters, raster_count); hb_font_destroy(font); FT_Done_Face(face); FT_Done_FreeType(lib); return 0;
    }

    uint8_t *rgba = calloc((size_t)atlas_w * atlas_h * 4u, 1u);
    AsmUiFontSdfEntry *entries = calloc(raster_count, sizeof(*entries));
    if (!rgba || !entries) {
        free(rgba); free(entries); free(px); free(py); free_rasters(rasters, raster_count); hb_font_destroy(font); FT_Done_Face(face); FT_Done_FreeType(lib); return 0;
    }

    for (uint32_t i = 0; i < raster_count; ++i) {
        const RasterGlyph *r = &rasters[i];
        for (uint32_t gy = 0; gy < r->height; ++gy) {
            for (uint32_t gx = 0; gx < r->width; ++gx) {
                uint8_t v = r->pixels[(size_t)gy * r->width + gx];
                size_t d = ((size_t)(py[i] + gy) * atlas_w + (px[i] + gx)) * 4u;
                rgba[d+0] = v; rgba[d+1] = v; rgba[d+2] = v; rgba[d+3] = 255u;
            }
        }
        entries[i].glyph_id = r->glyph_id;
        AsmUiHbAtlasGlyph *g = &entries[i].atlas;
        g->texture_id = texture_id;
        g->plane_left = (float)r->left / (float)em_pixels;
        g->plane_right = (float)(r->left + (int)r->width) / (float)em_pixels;
        g->plane_top = (float)r->top / (float)em_pixels;
        g->plane_bottom = (float)(r->top - (int)r->height) / (float)em_pixels;
        g->u0 = (float)px[i] / (float)atlas_w;
        g->v0 = (float)py[i] / (float)atlas_h;
        g->u1 = (float)(px[i] + r->width) / (float)atlas_w;
        g->v1 = (float)(py[i] + r->height) / (float)atlas_h;
    }

    free(px); free(py); free_rasters(rasters, raster_count);
    a->rgba = rgba;
    a->width = atlas_w;
    a->height = atlas_h;
    a->texture_id = texture_id;
    a->glyph_count = raster_count;
    a->glyph_capacity = raster_count;
    a->missing_glyph_count = missing;
    a->em_pixels = (float)em_pixels;
    /* FreeType maps [-spread,+spread] to [0,255]. The shader needs the
       full distance range to recover pixel distances.
       https://freetype.org/freetype2/docs/reference/ft2-properties.html#spread */
    a->px_range = 2.0f * (float)spread;
    a->glyphs = entries;
    a->ft_library = lib;
    a->ft_face = face;
    a->hb_font = font;
    return 1;
}

int asmui_font_sdf_build(AsmUiFontSdfAtlas *a, const char *path, uint32_t texture_id,
                         uint32_t em_pixels, const char *const *texts, size_t text_count) {
    return build_atlas(a,path,texture_id,em_pixels,texts,text_count,NULL,NULL);
}

int asmui_font_sdf_build_missing(AsmUiFontSdfAtlas *a, const char *path, uint32_t texture_id,
                                 uint32_t em_pixels, const char *const *texts, size_t text_count,
                                 AsmUiHbGlyphLookup existing, void *existing_user) {
    return build_atlas(a,path,texture_id,em_pixels,texts,text_count,existing,existing_user);
}

void asmui_font_sdf_destroy(AsmUiFontSdfAtlas *a) {
    if (!a) return;
    free(a->rgba);
    free(a->glyphs);
    if (a->hb_font) hb_font_destroy(a->hb_font);
    if (a->ft_face) FT_Done_Face((FT_Face)a->ft_face);
    if (a->ft_library) FT_Done_FreeType((FT_Library)a->ft_library);
    memset(a, 0, sizeof(*a));
}

int asmui_font_sdf_lookup(uint32_t glyph_id,
                          AsmUiHbAtlasGlyph *out,
                          void *user) {
    AsmUiFontSdfAtlas *a = (AsmUiFontSdfAtlas *)user;
    if (!a || !out) return 0;
    for (uint32_t i = 0; i < a->glyph_count; ++i) {
        if (a->glyphs[i].glyph_id == glyph_id) {
            *out = a->glyphs[i].atlas;
            return 1;
        }
    }
    return 0;
}

AsmUiHbShaper asmui_font_sdf_shaper(AsmUiFontSdfAtlas *a) {
    AsmUiHbShaper s;
    memset(&s, 0, sizeof(s));
    if (!a) return s;
    s.font = a->hb_font;
    s.lookup = asmui_font_sdf_lookup;
    s.lookup_user = a;
    s.language = HB_LANGUAGE_INVALID;
    return s;
}

int asmui_font_sdf_write_ppm(const AsmUiFontSdfAtlas *a,
                             const char *path) {
    if (!a || !a->rgba || !a->width || !a->height || !path) return 0;
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    fprintf(f, "P6\n%u %u\n255\n", a->width, a->height);
    for (uint32_t y = 0; y < a->height; ++y) {
        for (uint32_t x = 0; x < a->width; ++x) {
            const uint8_t *p = a->rgba + ((size_t)y * a->width + x) * 4u;
            fwrite(p, 1, 3, f);
        }
    }
    fclose(f);
    return 1;
}
