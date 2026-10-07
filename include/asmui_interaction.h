#ifndef ASMUI_INTERACTION_H
#define ASMUI_INTERACTION_H

#include "asmui.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    ASMUI_ASPECT_STRETCH = 0,
    ASMUI_ASPECT_CONTAIN = 1,
    ASMUI_ASPECT_COVER   = 2,
};

typedef struct AsmUiImagePlacement {
    float width, height;
    float u0, v0, u1, v1;
} AsmUiImagePlacement;

/* Computes centered image placement for a source image inside a UI box.
   CONTAIN preserves the full image and shrinks geometry.
   COVER preserves the box and center-crops UVs. */
int asmui_image_place(float source_w, float source_h,
                      float box_w, float box_h,
                      uint32_t mode,
                      AsmUiImagePlacement *out);

void asmui_apply_image_placement(AsmUiNode *node,
                                 const AsmUiImagePlacement *placement);

typedef struct AsmUiSnapScroll {
    float position;
    float velocity;
    float target;
    float min_target;
    float max_target;
    float stiffness;
    float damping;
} AsmUiSnapScroll;

void asmui_scroll_init(AsmUiSnapScroll *s, float initial,
                       float min_target, float max_target,
                       float stiffness);
void asmui_scroll_set_target(AsmUiSnapScroll *s, float target);
void asmui_scroll_nudge(AsmUiSnapScroll *s, float delta);
void asmui_scroll_step(AsmUiSnapScroll *s, float dt);

typedef struct AsmUiCarouselItemVisual {
    float x;
    float y;
    float scale;
    float alpha;
    float emphasis;
} AsmUiCarouselItemVisual;

typedef struct AsmUiCarousel {
    AsmUiSnapScroll scroll;
    uint32_t item_count;
    uint32_t selected;
    float center_x;
    float center_y;
    float spacing;
    float selected_scale;
    float side_scale;
    float min_alpha;

    /* Pointer/ScrollRect state. Coordinates passed to these helpers are
       in the scene's center-origin authoring space (+Y upward). */
    uint32_t pointer_dragging;
    float pointer_last_x;
    double pointer_last_time;
    float pointer_travel;
} AsmUiCarousel;

void asmui_carousel_init(AsmUiCarousel *c, uint32_t item_count, uint32_t selected,
                         float center_x, float center_y, float spacing, float stiffness);
void asmui_carousel_select(AsmUiCarousel *c, uint32_t index);
void asmui_carousel_nudge(AsmUiCarousel *c, int delta);
void asmui_carousel_step(AsmUiCarousel *c, float dt);
AsmUiCarouselItemVisual asmui_carousel_item(const AsmUiCarousel *c, uint32_t index);
void asmui_carousel_apply_node(const AsmUiCarousel *c, uint32_t index, AsmUiNode *node);

/* Pointer helpers used by mouse/touch driven song lists.  The window-to-canvas
   conversion is intentionally kept here so SDL/Wayland stays outside the core. */
void asmui_pointer_to_canvas(float pointer_x, float pointer_y, float viewport_w, float viewport_h,
                             float *canvas_x, float *canvas_y);
int asmui_carousel_hit_test(const AsmUiCarousel *c, float canvas_x, float canvas_y,
                            float item_w, float item_h);
void asmui_carousel_pointer_begin(AsmUiCarousel *c, float canvas_x, double time_seconds);
void asmui_carousel_pointer_move(AsmUiCarousel *c, float canvas_x, double time_seconds);
void asmui_carousel_pointer_end(AsmUiCarousel *c);
void asmui_carousel_wheel(AsmUiCarousel *c, float wheel_y);


typedef struct AsmUiVirtualCarousel {
    AsmUiCarousel motion;
    uint32_t logical_count;
    uint32_t slot_count;
    float viewport_x;
    float viewport_y;
} AsmUiVirtualCarousel;

/* Virtualized carousel: logical_count may be huge while slot_count is the
   number of retained card nodes actually materialized. All operations below
   are O(slot_count), never O(logical_count). viewport_{x,y} are in canvas
   coordinates; motion item coordinates are local to that viewport. */
void asmui_virtual_carousel_init(AsmUiVirtualCarousel *v,
                                 uint32_t logical_count, uint32_t selected,
                                 uint32_t slot_count,
                                 float viewport_x, float viewport_y,
                                 float center_x, float center_y,
                                 float spacing, float stiffness);
void asmui_virtual_carousel_select(AsmUiVirtualCarousel *v, uint32_t logical_index);
void asmui_virtual_carousel_nudge(AsmUiVirtualCarousel *v, int delta);
void asmui_virtual_carousel_step(AsmUiVirtualCarousel *v, float dt);
int asmui_virtual_carousel_slot(const AsmUiVirtualCarousel *v, uint32_t slot,
                                uint32_t *logical_index,
                                AsmUiCarouselItemVisual *visual);
void asmui_virtual_carousel_apply_slot(const AsmUiVirtualCarousel *v, uint32_t slot,
                                       AsmUiNode *node, uint32_t *logical_index);
int asmui_virtual_carousel_hit_test(const AsmUiVirtualCarousel *v,
                                    float canvas_x, float canvas_y,
                                    float item_w, float item_h);
void asmui_virtual_carousel_pointer_begin(AsmUiVirtualCarousel *v,
                                          float canvas_x, double time_seconds);
void asmui_virtual_carousel_pointer_move(AsmUiVirtualCarousel *v,
                                         float canvas_x, double time_seconds);
void asmui_virtual_carousel_pointer_end(AsmUiVirtualCarousel *v);
void asmui_virtual_carousel_wheel(AsmUiVirtualCarousel *v, float wheel_y);

#ifdef __cplusplus
}
#endif
#endif
