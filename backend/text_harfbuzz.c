#include "asmui_text_hb.h"
#include <math.h>
#include <string.h>

uint32_t asmui_hb_shape(const AsmUiDrawCmd *text_draw,
                        AsmUiGlyphQuad *glyphs,
                        uint32_t glyph_capacity,
                        void *user) {
    AsmUiHbShaper *s = (AsmUiHbShaper *)user;
    if (!text_draw || !glyphs || !glyph_capacity || !s || !s->font || !s->lookup || !text_draw->payload)
        return 0;

    hb_buffer_t *buf = hb_buffer_create();
    if (!buf) return 0;
    hb_buffer_add_utf8(buf, text_draw->payload, -1, 0, -1);
    hb_buffer_guess_segment_properties(buf);
    if (s->language != HB_LANGUAGE_INVALID) hb_buffer_set_language(buf, s->language);
    hb_shape(s->font, buf, NULL, 0);

    unsigned int count = 0;
    hb_glyph_info_t *info = hb_buffer_get_glyph_infos(buf, &count);
    hb_glyph_position_t *pos = hb_buffer_get_glyph_positions(buf, &count);
    if (!info || !pos || !count) { hb_buffer_destroy(buf); return 0; }

    unsigned upem = hb_face_get_upem(hb_font_get_face(s->font));
    if (!upem) upem = 1000;
    float em_px = text_draw->font_size > 0.0f ? text_draw->font_size : 16.0f;

    int x_scale = 0, y_scale = 0;
    hb_font_get_scale(s->font, &x_scale, &y_scale);
    float sx = x_scale ? em_px / fabsf((float)x_scale) : em_px / (float)upem;
    float sy = y_scale ? em_px / fabsf((float)y_scale) : em_px / (float)upem;

    float total_advance = 0.0f;
    for (unsigned i = 0; i < count; ++i) total_advance += pos[i].x_advance * sx;
    float origin_x = 0.0f;
    if (text_draw->text_align == ASMUI_TEXT_CENTER) origin_x = 0.5f * (text_draw->local_w - total_advance);
    else if (text_draw->text_align == ASMUI_TEXT_RIGHT) origin_x = text_draw->local_w - total_advance;

    hb_font_extents_t ext;
    float baseline_y = text_draw->local_h * 0.5f;
    if (hb_font_get_h_extents(s->font, &ext)) {
        float asc = ext.ascender * sy;
        float desc = ext.descender * sy;
        float line_h = asc - desc;
        baseline_y = 0.5f * (text_draw->local_h - line_h) - desc;
    }

    float pen_x = origin_x;
    float pen_y = baseline_y;
    uint32_t out_n = 0;
    for (unsigned i = 0; i < count && out_n < glyph_capacity; ++i) {
        AsmUiHbAtlasGlyph gm;
        int found=s->lookup(info[i].codepoint, &gm, s->lookup_user);
        if(!found){
            hb_glyph_extents_t ext;
            hb_codepoint_t fallback;
            /* Spaces have no atlas bitmap. Missing visible glyphs must not
               silently disappear from editable UTF-8 fields. */
            if(hb_font_get_glyph_extents(s->font,info[i].codepoint,&ext)&&ext.width&&ext.height&&
               hb_font_get_nominal_glyph(s->font,0x25a1,&fallback))
                found=s->lookup(fallback,&gm,s->lookup_user);
        }
        if (found) {
            float ox = pos[i].x_offset * sx;
            float oy = pos[i].y_offset * sy;
            AsmUiGlyphQuad *g = &glyphs[out_n++];
            g->x0 = pen_x + ox + gm.plane_left * em_px;
            g->y0 = pen_y + oy + gm.plane_bottom * em_px;
            g->x1 = pen_x + ox + gm.plane_right * em_px;
            g->y1 = pen_y + oy + gm.plane_top * em_px;
            g->u0 = gm.u0; g->v0 = gm.v0;
            g->u1 = gm.u1; g->v1 = gm.v1;
            g->resource_id = gm.texture_id;
            g->reserved0 = 0;
        }
        pen_x += pos[i].x_advance * sx;
        pen_y += pos[i].y_advance * sy;
    }
    hb_buffer_destroy(buf);
    return out_n;
}
