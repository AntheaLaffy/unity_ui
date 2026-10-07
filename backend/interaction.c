#include "asmui_interaction.h"
#include <math.h>
#include <string.h>

static float clampf(float x, float lo, float hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}

int asmui_image_place(float source_w, float source_h,
                      float box_w, float box_h,
                      uint32_t mode,
                      AsmUiImagePlacement *out) {
    if (!out || !(source_w > 0.0f) || !(source_h > 0.0f) ||
        !(box_w > 0.0f) || !(box_h > 0.0f)) return 0;

    AsmUiImagePlacement p = {
        .width = box_w, .height = box_h,
        .u0 = 0.0f, .v0 = 0.0f, .u1 = 1.0f, .v1 = 1.0f,
    };

    if (mode == ASMUI_ASPECT_CONTAIN) {
        float scale = fminf(box_w / source_w, box_h / source_h);
        p.width = source_w * scale;
        p.height = source_h * scale;
    } else if (mode == ASMUI_ASPECT_COVER) {
        float src_aspect = source_w / source_h;
        float box_aspect = box_w / box_h;
        if (src_aspect > box_aspect) {
            float visible = box_aspect / src_aspect;
            float pad = 0.5f * (1.0f - visible);
            p.u0 = pad;
            p.u1 = 1.0f - pad;
        } else if (src_aspect < box_aspect) {
            float visible = src_aspect / box_aspect;
            float pad = 0.5f * (1.0f - visible);
            p.v0 = pad;
            p.v1 = 1.0f - pad;
        }
    } else if (mode != ASMUI_ASPECT_STRETCH) {
        return 0;
    }

    *out = p;
    return 1;
}

void asmui_apply_image_placement(AsmUiNode *node,
                                 const AsmUiImagePlacement *p) {
    if (!node || !p) return;
    node->size_delta_x = p->width;
    node->size_delta_y = p->height;
    node->uv_x0 = p->u0;
    node->uv_y0 = p->v0;
    node->uv_x1 = p->u1;
    node->uv_y1 = p->v1;
}

void asmui_scroll_init(AsmUiSnapScroll *s, float initial,
                       float min_target, float max_target,
                       float stiffness) {
    if (!s) return;
    memset(s, 0, sizeof(*s));
    if (min_target > max_target) {
        float t = min_target; min_target = max_target; max_target = t;
    }
    s->min_target = min_target;
    s->max_target = max_target;
    s->stiffness = stiffness > 1.0f ? stiffness : 1.0f;
    s->damping = 2.0f * sqrtf(s->stiffness); /* critical damping */
    s->position = clampf(initial, min_target, max_target);
    s->target = s->position;
}

void asmui_scroll_set_target(AsmUiSnapScroll *s, float target) {
    if (!s) return;
    s->target = clampf(target, s->min_target, s->max_target);
}

void asmui_scroll_nudge(AsmUiSnapScroll *s, float delta) {
    if (!s) return;
    asmui_scroll_set_target(s, s->target + delta);
}

void asmui_scroll_step(AsmUiSnapScroll *s, float dt) {
    if (!s || !(dt > 0.0f)) return;
    if (dt > 0.1f) dt = 0.1f;

    /* Substep so a paused/debugged frame cannot destabilize the spring. */
    const float max_step = 1.0f / 120.0f;
    int steps = (int)ceilf(dt / max_step);
    if (steps < 1) steps = 1;
    float h = dt / (float)steps;
    for (int i = 0; i < steps; ++i) {
        float accel = s->stiffness * (s->target - s->position)
                    - s->damping * s->velocity;
        s->velocity += accel * h;
        s->position += s->velocity * h;
    }

    if (fabsf(s->target - s->position) < 0.0005f && fabsf(s->velocity) < 0.001f) {
        s->position = s->target;
        s->velocity = 0.0f;
    }
}


void asmui_carousel_init(AsmUiCarousel *c, uint32_t item_count, uint32_t selected,
                         float center_x, float center_y, float spacing, float stiffness) {
    if (!c) return;
    memset(c, 0, sizeof(*c));
    c->item_count = item_count;
    c->center_x = center_x;
    c->center_y = center_y;
    c->spacing = spacing > 1.0f ? spacing : 1.0f;
    c->selected_scale = 1.10f;
    c->side_scale = 0.88f;
    c->min_alpha = 0.28f;
    if (!item_count) {
        asmui_scroll_init(&c->scroll, 0.0f, 0.0f, 0.0f, stiffness);
        return;
    }
    if (selected >= item_count) selected = item_count - 1u;
    c->selected = selected;
    asmui_scroll_init(&c->scroll, (float)selected, 0.0f, (float)(item_count - 1u), stiffness);
}

void asmui_carousel_select(AsmUiCarousel *c, uint32_t index) {
    if (!c || !c->item_count) return;
    if (index >= c->item_count) index = c->item_count - 1u;
    c->selected = index;
    asmui_scroll_set_target(&c->scroll, (float)index);
}

void asmui_carousel_nudge(AsmUiCarousel *c, int delta) {
    if (!c || !c->item_count || delta == 0) return;
    int next = (int)c->selected + delta;
    if (next < 0) next = 0;
    if (next >= (int)c->item_count) next = (int)c->item_count - 1;
    asmui_carousel_select(c, (uint32_t)next);
}

void asmui_carousel_step(AsmUiCarousel *c, float dt) {
    if (!c || c->pointer_dragging) return;
    asmui_scroll_step(&c->scroll, dt);
}

AsmUiCarouselItemVisual asmui_carousel_item(const AsmUiCarousel *c, uint32_t index) {
    AsmUiCarouselItemVisual v = {0};
    if (!c || !c->item_count || index >= c->item_count) return v;
    float d = (float)index - c->scroll.position;
    float ad = fabsf(d);
    float t = clampf(ad, 0.0f, 1.0f);
    v.emphasis = 1.0f - t;
    v.x = c->center_x + d * c->spacing;
    v.y = c->center_y - fminf(ad, 2.0f) * 6.0f;
    v.scale = c->selected_scale + (c->side_scale - c->selected_scale) * t;
    v.alpha = clampf(1.0f - 0.22f * ad, c->min_alpha, 1.0f);
    return v;
}

void asmui_carousel_apply_node(const AsmUiCarousel *c, uint32_t index, AsmUiNode *node) {
    if (!node) return;
    AsmUiCarouselItemVisual v = asmui_carousel_item(c, index);
    node->anchored_x = v.x;
    node->anchored_y = v.y;
    node->scale_x = v.scale;
    node->scale_y = v.scale;
    node->alpha = v.alpha;
}


void asmui_pointer_to_canvas(float pointer_x, float pointer_y, float viewport_w, float viewport_h,
                             float *canvas_x, float *canvas_y) {
    if (canvas_x) *canvas_x = pointer_x - viewport_w * 0.5f;
    if (canvas_y) *canvas_y = viewport_h * 0.5f - pointer_y;
}

int asmui_carousel_hit_test(const AsmUiCarousel *c, float canvas_x, float canvas_y,
                            float item_w, float item_h) {
    if (!c || !c->item_count || !(item_w > 0.0f) || !(item_h > 0.0f)) return -1;
    /* Search most-emphasized first so overlapping scaled cards select the visual
       card the player perceives as being on top. */
    int best = -1;
    float best_emphasis = -1.0f;
    for (uint32_t i = 0; i < c->item_count; ++i) {
        AsmUiCarouselItemVisual v = asmui_carousel_item(c, i);
        float hw = item_w * v.scale * 0.5f;
        float hh = item_h * v.scale * 0.5f;
        if (canvas_x >= v.x - hw && canvas_x <= v.x + hw &&
            canvas_y >= v.y - hh && canvas_y <= v.y + hh &&
            v.emphasis > best_emphasis) {
            best = (int)i;
            best_emphasis = v.emphasis;
        }
    }
    return best;
}

void asmui_carousel_pointer_begin(AsmUiCarousel *c, float canvas_x, double time_seconds) {
    if (!c || !c->item_count) return;
    c->pointer_dragging = 1u;
    c->pointer_last_x = canvas_x;
    c->pointer_last_time = time_seconds;
    c->pointer_travel = 0.0f;
    c->scroll.velocity = 0.0f;
}

void asmui_carousel_pointer_move(AsmUiCarousel *c, float canvas_x, double time_seconds) {
    if (!c || !c->pointer_dragging || !(c->spacing > 0.0f)) return;
    float dx = canvas_x - c->pointer_last_x;
    float dt = time_seconds - c->pointer_last_time;
    if (!(dt > 0.0001f) || dt > 0.1f) dt = 1.0f / 120.0f;
    float delta_items = -dx / c->spacing;
    float lo = c->scroll.min_target, hi = c->scroll.max_target;
    c->scroll.position = clampf(c->scroll.position + delta_items, lo, hi);
    c->scroll.target = c->scroll.position;
    /* Low-pass event velocity so the release projection behaves like a small
       ScrollRect flick instead of depending on one noisy motion event. */
    float instant_v = clampf(delta_items / dt, -8.0f, 8.0f);
    c->scroll.velocity = c->scroll.velocity * 0.65f + instant_v * 0.35f;
    c->pointer_travel += fabsf(dx);
    c->pointer_last_x = canvas_x;
    c->pointer_last_time = time_seconds;

    int nearest = (int)lroundf(c->scroll.position);
    if (nearest < 0) nearest = 0;
    if (nearest >= (int)c->item_count) nearest = (int)c->item_count - 1;
    c->selected = (uint32_t)nearest;
}

void asmui_carousel_pointer_end(AsmUiCarousel *c) {
    if (!c || !c->pointer_dragging) return;
    c->pointer_dragging = 0u;
    if (c->pointer_travel < 4.0f) {
        asmui_carousel_select(c, c->selected);
        return;
    }
    float projected = c->scroll.position + c->scroll.velocity * 0.035f;
    int nearest = (int)lroundf(projected);
    if (nearest < 0) nearest = 0;
    if (nearest >= (int)c->item_count) nearest = (int)c->item_count - 1;
    asmui_carousel_select(c, (uint32_t)nearest);
}

void asmui_carousel_wheel(AsmUiCarousel *c, float wheel_y) {
    if (!c || fabsf(wheel_y) < 0.01f) return;
    /* Conventional vertical wheel: down advances to the next song. */
    asmui_carousel_nudge(c, wheel_y < 0.0f ? +1 : -1);
}


void asmui_virtual_carousel_init(AsmUiVirtualCarousel *v,
                                 uint32_t logical_count, uint32_t selected,
                                 uint32_t slot_count,
                                 float viewport_x, float viewport_y,
                                 float center_x, float center_y,
                                 float spacing, float stiffness) {
    if (!v) return;
    memset(v, 0, sizeof(*v));
    v->logical_count = logical_count;
    v->slot_count = slot_count ? slot_count : 1u;
    if (v->slot_count > logical_count && logical_count) v->slot_count = logical_count;
    v->viewport_x = viewport_x;
    v->viewport_y = viewport_y;
    asmui_carousel_init(&v->motion, logical_count, selected,
                        center_x, center_y, spacing, stiffness);
}

void asmui_virtual_carousel_select(AsmUiVirtualCarousel *v, uint32_t logical_index) {
    if (!v) return;
    asmui_carousel_select(&v->motion, logical_index);
}

void asmui_virtual_carousel_nudge(AsmUiVirtualCarousel *v, int delta) {
    if (!v) return;
    asmui_carousel_nudge(&v->motion, delta);
}

void asmui_virtual_carousel_step(AsmUiVirtualCarousel *v, float dt) {
    if (!v) return;
    asmui_carousel_step(&v->motion, dt);
}

static int virtual_first(const AsmUiVirtualCarousel *v) {
    if (!v || !v->logical_count || !v->slot_count) return 0;
    int center = (int)lroundf(v->motion.scroll.position);
    int first = center - (int)(v->slot_count / 2u);
    int max_first = (int)(v->logical_count > v->slot_count ? v->logical_count - v->slot_count : 0u);
    if (first < 0) first = 0;
    if (first > max_first) first = max_first;
    return first;
}

int asmui_virtual_carousel_slot(const AsmUiVirtualCarousel *v, uint32_t slot,
                                uint32_t *logical_index,
                                AsmUiCarouselItemVisual *visual) {
    if (!v || !v->logical_count || slot >= v->slot_count) return 0;
    uint32_t logical = (uint32_t)(virtual_first(v) + (int)slot);
    if (logical >= v->logical_count) return 0;
    if (logical_index) *logical_index = logical;
    if (visual) *visual = asmui_carousel_item(&v->motion, logical);
    return 1;
}

void asmui_virtual_carousel_apply_slot(const AsmUiVirtualCarousel *v, uint32_t slot,
                                       AsmUiNode *node, uint32_t *logical_index) {
    if (!node) return;
    AsmUiCarouselItemVisual vis;
    uint32_t logical = 0;
    if (!asmui_virtual_carousel_slot(v, slot, &logical, &vis)) {
        node->flags &= ~ASMUI_VISIBLE;
        return;
    }
    node->flags |= ASMUI_VISIBLE;
    node->anchored_x = vis.x;
    node->anchored_y = vis.y;
    node->scale_x = vis.scale;
    node->scale_y = vis.scale;
    node->alpha = vis.alpha;
    if (logical_index) *logical_index = logical;
}

int asmui_virtual_carousel_hit_test(const AsmUiVirtualCarousel *v,
                                    float canvas_x, float canvas_y,
                                    float item_w, float item_h) {
    if (!v || !(item_w > 0.0f) || !(item_h > 0.0f)) return -1;
    float lx = canvas_x - v->viewport_x;
    float ly = canvas_y - v->viewport_y;
    int best = -1;
    float best_emphasis = -1.0f;
    for (uint32_t slot = 0; slot < v->slot_count; ++slot) {
        uint32_t logical = 0;
        AsmUiCarouselItemVisual vis;
        if (!asmui_virtual_carousel_slot(v, slot, &logical, &vis)) continue;
        float hw = item_w * vis.scale * 0.5f;
        float hh = item_h * vis.scale * 0.5f;
        if (lx >= vis.x - hw && lx <= vis.x + hw &&
            ly >= vis.y - hh && ly <= vis.y + hh &&
            vis.emphasis > best_emphasis) {
            best = (int)logical;
            best_emphasis = vis.emphasis;
        }
    }
    return best;
}

void asmui_virtual_carousel_pointer_begin(AsmUiVirtualCarousel *v,
                                          float canvas_x, double time_seconds) {
    if (!v) return;
    asmui_carousel_pointer_begin(&v->motion, canvas_x - v->viewport_x, time_seconds);
}
void asmui_virtual_carousel_pointer_move(AsmUiVirtualCarousel *v,
                                         float canvas_x, double time_seconds) {
    if (!v) return;
    asmui_carousel_pointer_move(&v->motion, canvas_x - v->viewport_x, time_seconds);
}
void asmui_virtual_carousel_pointer_end(AsmUiVirtualCarousel *v) {
    if (!v) return;
    asmui_carousel_pointer_end(&v->motion);
}
void asmui_virtual_carousel_wheel(AsmUiVirtualCarousel *v, float wheel_y) {
    if (!v) return;
    asmui_carousel_wheel(&v->motion, wheel_y);
}
