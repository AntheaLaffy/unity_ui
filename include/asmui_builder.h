#ifndef ASMUI_BUILDER_H
#define ASMUI_BUILDER_H

#include "asmui.h"
#include <string.h>

typedef struct AsmUiSpriteRegion {
    uint32_t texture_id;
    float u0, v0, u1, v1;
    float source_w, source_h;
    float border_l, border_r, border_b, border_t;
} AsmUiSpriteRegion;

static inline AsmUiNode asmui_node(uint32_t type, int32_t parent, uint32_t rgba) {
    AsmUiNode n;
    memset(&n, 0, sizeof(n));
    n.type = type;
    n.flags = ASMUI_VISIBLE;
    n.parent = parent;
    n.rgba = rgba;
    n.anchor_min_x = n.anchor_max_x = 0.5f;
    n.anchor_min_y = n.anchor_max_y = 0.5f;
    n.pivot_x = n.pivot_y = 0.5f;
    n.scale_x = n.scale_y = 1.0f;
    n.alpha = 1.0f;
    n.uv_x1 = n.uv_y1 = 1.0f;
    n.material_id = ASMUI_MATERIAL_DEFAULT;
    n.text_align = ASMUI_TEXT_LEFT;
    n.fill_amount = 1.0f;
    n.mask_mode = ASMUI_MASK_NONE;
    n.mask_softness = 1.0f;
    return n;
}

static inline void asmui_stretch(AsmUiNode *n) {
    n->anchor_min_x = n->anchor_min_y = 0.0f;
    n->anchor_max_x = n->anchor_max_y = 1.0f;
    n->pivot_x = n->pivot_y = 0.5f;
    n->size_delta_x = n->size_delta_y = 0.0f;
}

static inline void asmui_apply_sprite(AsmUiNode *n, const AsmUiSpriteRegion *s, uint32_t image_mode) {
    n->flags |= ASMUI_UV_EXPLICIT;
    n->resource_id = s->texture_id;
    n->image_mode = image_mode;
    n->uv_x0 = s->u0; n->uv_y0 = s->v0;
    n->uv_x1 = s->u1; n->uv_y1 = s->v1;
    n->source_w = s->source_w; n->source_h = s->source_h;
    n->border_l = s->border_l; n->border_r = s->border_r;
    n->border_b = s->border_b; n->border_t = s->border_t;
}

static inline void asmui_set_material4(AsmUiNode *n, uint32_t material_id,
                                       float p0, float p1, float p2, float p3) {
    n->material_id = material_id;
    n->material0 = p0; n->material1 = p1;
    n->material2 = p2; n->material3 = p3;
}

static inline void asmui_material_rounded(AsmUiNode *n, float radius01) {
    asmui_set_material4(n, ASMUI_MATERIAL_ROUNDED, radius01, 0.0f, 0.0f, 0.0f);
}

static inline void asmui_gradient_y(AsmUiNode *n, uint32_t bottom, uint32_t top) {
    n->flags |= ASMUI_GRADIENT_Y;
    n->rgba = bottom;
    memcpy(&n->material3, &top, sizeof(top));
}

static inline void asmui_material_grayscale(AsmUiNode *n, float amount) {
    asmui_set_material4(n, ASMUI_MATERIAL_GRAYSCALE, amount, 0.0f, 0.0f, 0.0f);
}

static inline void asmui_material_hsv(AsmUiNode *n, float hue_shift,
                                      float saturation_mul, float value_mul) {
    asmui_set_material4(n, ASMUI_MATERIAL_HSV,
                        hue_shift, saturation_mul, value_mul, 0.0f);
}

static inline void asmui_material_outline(AsmUiNode *n, float radius_texels, float intensity) {
    asmui_set_material4(n, ASMUI_MATERIAL_OUTLINE,
                        radius_texels, intensity, 0.0f, 0.0f);
}

static inline void asmui_material_glow(AsmUiNode *n, float radius_texels, float intensity) {
    asmui_set_material4(n, ASMUI_MATERIAL_GLOW,
                        radius_texels, intensity, 0.0f, 0.0f);
}

static inline void asmui_soft_mask(AsmUiNode *n, float radius_px, float softness_px) {
    n->flags |= ASMUI_CLIP;
    n->mask_mode = ASMUI_MASK_SOFT;
    n->mask_radius = radius_px;
    n->mask_softness = softness_px > 0.0f ? softness_px : 1.0f;
}


static inline void asmui_offscreen_layer(AsmUiNode *n) {
    n->layer_flags |= ASMUI_LAYER_OFFSCREEN;
}

static inline void asmui_stencil_mask(AsmUiNode *n) {
    /* Keep the conservative AABB scissor for early rejection, then use the
       real transformed quad in the Vulkan stencil attachment. */
    n->flags |= ASMUI_CLIP;
    n->mask_mode = ASMUI_MASK_STENCIL;
}

static inline void asmui_text_msdf(AsmUiNode *n, float px_range, float outline, float outline_softness) {
    n->material_id = ASMUI_MATERIAL_MSDF_TEXT;
    n->material0 = px_range > 0.0f ? px_range : 4.0f;
    n->material1 = outline;
    n->material2 = outline_softness;
    n->material3 = 0.0f;
}

#endif
