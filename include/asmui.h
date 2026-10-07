#ifndef ASMUI_H
#define ASMUI_H

#include <stdint.h>
#include <stddef.h>

#define ASMUI_VERSION_MAJOR 0
#define ASMUI_VERSION_MINOR 21
#define ASMUI_VERSION_PATCH 0

#ifdef __cplusplus
extern "C" {
#endif

enum {
    ASMUI_NODE_CONTAINER = 0,
    ASMUI_NODE_IMAGE     = 1,
    ASMUI_NODE_TEXT      = 2,
    ASMUI_NODE_GLYPH     = 3,
};

enum {
    ASMUI_VISIBLE = 1u << 0,
    ASMUI_CLIP    = 1u << 1,
    /* Two-color vertical gradient; material3 contains the top RGBA bits.
       Valid on image nodes with DEFAULT/ROUNDED materials. */
    ASMUI_GRADIENT_Y = 1u << 2,
    /* Zero-area UVs deliberately sample one texel in atlas slice centers. */
    ASMUI_UV_EXPLICIT = 1u << 3,
};

enum {
    ASMUI_LAYER_NONE      = 0,
    ASMUI_LAYER_OFFSCREEN = 1u << 0,
};

enum {
    ASMUI_MASK_NONE    = 0,
    ASMUI_MASK_SOFT    = 1,
    ASMUI_MASK_STENCIL = 2, /* true nested Vulkan stencil mask */
};

enum {
    ASMUI_PROP_ANCHORED_X = 1,
    ASMUI_PROP_ANCHORED_Y = 2,
    ASMUI_PROP_SCALE_X    = 3,
    ASMUI_PROP_SCALE_Y    = 4,
    ASMUI_PROP_ROTATION   = 5,
    ASMUI_PROP_ALPHA      = 6,
    ASMUI_PROP_SIZE_X     = 7,
    ASMUI_PROP_SIZE_Y     = 8,
    ASMUI_PROP_FILL       = 9,
};

enum {
    ASMUI_EASE_LINEAR       = 0,
    ASMUI_EASE_OUT_CUBIC    = 1,
    ASMUI_EASE_IN_OUT_CUBIC = 2,
};

enum {
    ASMUI_TWEEN_ACTIVE = 1u << 0,
    ASMUI_TWEEN_LOOP   = 1u << 1,
};

enum {
    ASMUI_IMAGE_SIMPLE = 0,
    ASMUI_IMAGE_SLICED = 1,
    ASMUI_IMAGE_FILLED_HORIZONTAL = 2,
    ASMUI_IMAGE_FILLED_VERTICAL   = 3,
};

enum {
    ASMUI_MATERIAL_DEFAULT   = 0,
    ASMUI_MATERIAL_ADDITIVE  = 1,
    ASMUI_MATERIAL_MULTIPLY  = 2,
    ASMUI_MATERIAL_CUSTOM    = 0x100,
    ASMUI_MATERIAL_ROUNDED   = 0x101,
    ASMUI_MATERIAL_OUTLINE   = 0x102,
    ASMUI_MATERIAL_GRAYSCALE = 0x103,
    ASMUI_MATERIAL_HSV       = 0x104,
    ASMUI_MATERIAL_GLOW      = 0x105,
    ASMUI_MATERIAL_MSDF_TEXT = 0x106,
    ASMUI_MATERIAL_LINE      = 0x107,
    ASMUI_MATERIAL_GRID      = 0x108,
    ASMUI_MATERIAL_GROUND    = 0x109,
    ASMUI_MATERIAL_GRID_2D   = 0x10a,
    ASMUI_MATERIAL_ATLAS_RGB_MIX = 0x10b,
    ASMUI_MATERIAL_PROJECTIVE = 0x10c, /* draw material0..3 = four positive clip W values */
};

enum {
    ASMUI_TEXT_LEFT   = 0,
    ASMUI_TEXT_CENTER = 1,
    ASMUI_TEXT_RIGHT  = 2,
};

enum {
    ASMUI_DRAW_HAS_SOFT_MASK = 1u << 0,
};

typedef struct AsmUiNode {
    uint32_t type;          // 0
    uint32_t flags;         // 4
    int32_t  parent;        // 8; -1 => viewport
    uint32_t resource_id;   // 12; texture id for image, font id for text

    /* Authored anchors, pivots, positions and local geometry use +Y up.
       Resolved matrices, draw vertices, clips and masks use screen +Y down. */
    float anchor_min_x;     // 16
    float anchor_min_y;     // 20
    float anchor_max_x;     // 24
    float anchor_max_y;     // 28
    float pivot_x;          // 32
    float pivot_y;          // 36
    float anchored_x;       // 40
    float anchored_y;       // 44
    float size_delta_x;     // 48
    float size_delta_y;     // 52
    float rotation;         // 56, radians
    float scale_x;          // 60
    float scale_y;          // 64
    float alpha;            // 68
    uint32_t rgba;          // 72, 0xRRGGBBAA
    uint32_t layer_flags;   // 76, ASMUI_LAYER_*
    const char *payload;    // 80, UTF-8 for text nodes

    float m00;              // 88
    float m01;              // 92
    float m02;              // 96
    float m10;              // 100
    float m11;              // 104
    float m12;              // 108
    float width;            // 112
    float height;           // 116
    float world_alpha;      // 120
    float clip_x;           // 124
    float clip_y;           // 128
    float clip_w;           // 132
    float clip_h;           // 136
    float font_size;        // 140

    uint32_t image_mode;    // 144
    uint32_t material_id;   // 148
    float uv_x0;            // 152
    float uv_y0;            // 156
    float uv_x1;            // 160
    float uv_y1;            // 164
    float source_w;         // 168
    float source_h;         // 172
    float border_l;         // 176
    float border_r;         // 180
    float border_b;         // 184
    float border_t;         // 188
    float material0;        // 192
    float material1;        // 196
    float material2;        // 200
    float material3;        // 204
    uint32_t text_align;    // 208
    float fill_amount;      // 212

    /* v0.4 authored mask + resolved inherited soft-mask state. */
    uint32_t mask_mode;     // 216, ASMUI_MASK_*
    float mask_radius;      // 220, authored radius in screen-ish pixels
    float mask_softness;    // 224, edge feather in pixels
    float mask_x;           // 228, resolved inherited mask AABB
    float mask_y;           // 232
    float mask_w;           // 236
    float mask_h;           // 240
    float mask_world_radius;// 244
    float mask_world_softness;// 248
    uint32_t mask_flags;    // 252, ASMUI_DRAW_HAS_SOFT_MASK
} AsmUiNode;

typedef struct AsmUiDrawCmd {
    uint32_t type;          // 0
    uint32_t resource_id;   // 4
    uint32_t rgba;          // 8
    uint32_t flags;         // 12
    float x0, y0;           // 16,20 bottom-left
    float x1, y1;           // 24,28 bottom-right
    float x2, y2;           // 32,36 top-right
    float x3, y3;           // 40,44 top-left
    float alpha;            // 48
    float u0, v0;           // 52,56 atlas left/top
    float u1, v1;           // 60,64 atlas right/bottom
    float clip_x;           // 68
    float clip_y;           // 72
    float clip_w;           // 76
    float clip_h;           // 80
    float font_size;        // 84
    const char *payload;    // 88
    uint32_t material_id;   // 96
    uint32_t source_node;   // 100, source node index + 1; 0 => synthetic
    float material0;        // 104
    float material1;        // 108
    float material2;        // 112
    float material3;        // 116
    uint32_t text_align;    // 120
    uint32_t stencil_depth; // 124, number of stencil-mask ancestors

    /* v0.4: original local box + inherited soft-mask state. */
    float local_w;          // 128
    float local_h;          // 132
    float mask_x;           // 136
    float mask_y;           // 140
    float mask_w;           // 144
    float mask_h;           // 148
    float mask_radius;      // 152
    float mask_softness;    // 156
    uint32_t mask_flags;    // 160
    uint32_t stencil_parent;// 164, nearest stencil-mask node index + 1; 0 => none
} AsmUiDrawCmd;

typedef struct AsmUiBatch {
    uint32_t first_draw;     // 0
    uint32_t draw_count;     // 4
    uint32_t type;           // 8
    uint32_t resource_id;    // 12
    uint32_t material_id;    // 16
    uint32_t mask_flags;     // 20
    float clip_x;            // 24
    float clip_y;            // 28
    float clip_w;            // 32
    float clip_h;            // 36
    float mask_x;            // 40
    float mask_y;            // 44
    float mask_w;            // 48
    float mask_h;            // 52
    float mask_radius;       // 56
    float mask_softness;     // 60
    uint32_t stencil_depth;   // 64
    uint32_t stencil_parent;  // 68, nearest stencil-mask node index + 1
} AsmUiBatch;

typedef struct AsmUiGpuVertex {
    float x, y;
    float u, v;
    float local_x, local_y;
    uint32_t rgba;
    float alpha;
    float material0;
    float material1;
    float material2;
    float material3;
} AsmUiGpuVertex;

/* Glyph quads are produced by a shaper/atlas adapter in text-node local pixels.
   Coordinates use the same local system as UI nodes: origin bottom-left, +Y up. */
typedef struct AsmUiGlyphQuad {
    float x0, y0, x1, y1;
    float u0, v0, u1, v1;
    uint32_t resource_id;   // MSDF atlas texture page
    uint32_t reserved0;
} AsmUiGlyphQuad;



enum {
    ASMUI_RENDER_DRAW_RANGE = 0,
    ASMUI_RENDER_BEGIN_LAYER = 1,
    ASMUI_RENDER_END_LAYER = 2,
    ASMUI_RENDER_COMPOSITE_LAYER = 3,
};

typedef struct AsmUiRenderOp {
    uint32_t type;
    uint32_t node_index;
    uint32_t first_draw;
    uint32_t draw_count;
} AsmUiRenderOp;

_Static_assert(sizeof(AsmUiRenderOp) == 16, "AsmUiRenderOp ABI mismatch");

typedef struct AsmUiTween {
    uint32_t node_index;
    uint32_t property;
    uint32_t easing;
    uint32_t flags;
    float start_time;
    float duration;
    float from;
    float to;
} AsmUiTween;

typedef struct AsmUiContext {
    AsmUiNode *nodes;
    uint32_t node_count;
    uint32_t node_capacity;
    AsmUiDrawCmd *draws;
    uint32_t draw_count;
    uint32_t draw_capacity;
    AsmUiTween *tweens;
    uint32_t tween_count;
    uint32_t tween_capacity;
    float viewport_w;
    float viewport_h;
    float time;
    float dt;
} AsmUiContext;

_Static_assert(sizeof(AsmUiNode) == 256, "AsmUiNode ABI mismatch");
_Static_assert(sizeof(AsmUiDrawCmd) == 168, "AsmUiDrawCmd ABI mismatch");
_Static_assert(sizeof(AsmUiBatch) == 72, "AsmUiBatch ABI mismatch");
_Static_assert(sizeof(AsmUiGpuVertex) == 48, "AsmUiGpuVertex ABI mismatch");
_Static_assert(sizeof(AsmUiGlyphQuad) == 40, "AsmUiGlyphQuad ABI mismatch");
_Static_assert(sizeof(AsmUiTween) == 32, "AsmUiTween ABI mismatch");
_Static_assert(sizeof(AsmUiContext) == 64, "AsmUiContext ABI mismatch");
_Static_assert(offsetof(AsmUiNode, mask_mode) == 216, "AsmUiNode mask offset mismatch");
_Static_assert(offsetof(AsmUiDrawCmd, local_w) == 128, "AsmUiDrawCmd v0.4 offset mismatch");
_Static_assert(offsetof(AsmUiDrawCmd, mask_flags) == 160, "AsmUiDrawCmd mask offset mismatch");
_Static_assert(offsetof(AsmUiBatch, mask_x) == 40, "AsmUiBatch mask offset mismatch");
_Static_assert(offsetof(AsmUiBatch, stencil_depth) == 64, "AsmUiBatch stencil offset mismatch");

void asm_ui_init(AsmUiContext *ctx,
                 AsmUiNode *nodes, uint32_t node_capacity,
                 AsmUiDrawCmd *draws, uint32_t draw_capacity,
                 AsmUiTween *tweens, uint32_t tween_capacity);
void asm_ui_set_viewport(AsmUiContext *ctx, float width, float height);
void asm_ui_begin_frame(AsmUiContext *ctx, float dt);
void asm_ui_tick_tweens(AsmUiContext *ctx);
void asm_ui_layout(AsmUiContext *ctx);
void asm_ui_resolve_soft_masks(AsmUiContext *ctx);
void asm_ui_build_drawlist(AsmUiContext *ctx);

uint32_t asm_ui_build_batches(const AsmUiContext *ctx,
                              AsmUiBatch *batches,
                              uint32_t batch_capacity);
uint32_t asmui_build_render_ops(const AsmUiContext *ctx,
                                AsmUiRenderOp *ops,
                                uint32_t op_capacity);

uint32_t asm_ui_build_gpu_vertices(const AsmUiContext *ctx,
                                   AsmUiGpuVertex *vertices,
                                   uint32_t vertex_capacity);

/* Packs one quad per node, indexed by node index. Vulkan uses these quads to
   replay stencil-mask geometry without rebuilding CPU-side triangles. */
uint32_t asm_ui_build_node_vertices(const AsmUiContext *ctx,
                                    AsmUiGpuVertex *vertices,
                                    uint32_t vertex_capacity);

/* Expands one shaped text draw into MSDF glyph draw commands. The text shaper
   stays outside the ASM core; geometry transform and final command emission are ASM. */
uint32_t asm_ui_expand_text_draw(const AsmUiDrawCmd *text_draw,
                                 const AsmUiGlyphQuad *glyphs,
                                 uint32_t glyph_count,
                                 AsmUiDrawCmd *out,
                                 uint32_t out_capacity);

#ifdef __cplusplus
}
#endif
#endif
