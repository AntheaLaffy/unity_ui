#include "asmui_builder.h"
#include "asmui_canvas.h"
#include <math.h>
#include <stdio.h>

void asmui_canvas_begin(AsmUiCanvas* c, AsmUiNode* nodes, uint32_t capacity, char* strings,
                        size_t bytes, float w, float h) {
    *c = (AsmUiCanvas){.nodes = nodes,
                       .capacity = capacity,
                       .strings = strings,
                       .string_capacity = bytes,
                       .parent = -1,
                       .width = w,
                       .height = h};
}
static AsmUiNode* box(AsmUiCanvas* c, uint32_t type, float x, float y, float w, float h,
                      uint32_t rgba) {
    if (c->count == c->capacity) {
        c->overflow = 1;
        return NULL;
    }
    AsmUiNode* n = &c->nodes[c->count++];
    *n = asmui_node(type, c->parent, rgba);
    n->anchor_min_x = n->anchor_max_x = 0;
    n->anchor_min_y = n->anchor_max_y = 1;
    n->pivot_x = 0;
    n->pivot_y = 1;
    for (int32_t p = c->parent; p >= 0; p = c->nodes[p].parent) {
        x -= c->nodes[p].anchored_x;
        y += c->nodes[p].anchored_y;
    }
    n->anchored_x = x;
    n->anchored_y = -y;
    n->size_delta_x = w;
    n->size_delta_y = h;
    return n;
}
int asmui_canvas_rect(AsmUiCanvas* c, float x, float y, float w, float h, uint32_t rgba,
                      float radius) {
    AsmUiNode* n = box(c, ASMUI_NODE_IMAGE, x, y, w, h, rgba);
    if (!n)
        return -1;
    if (radius > 0)
        asmui_material_rounded(n, radius / fminf(w, h));
    return (int)c->count - 1;
}
int asmui_canvas_text(AsmUiCanvas* c, float x, float y, float w, float size, uint32_t rgba,
                      const char* text) {
    size_t bytes = strlen(text) + 1;
    if (bytes > c->string_capacity - c->used) {
        c->overflow = 1;
        return -1;
    }
    AsmUiNode* n = box(c, ASMUI_NODE_TEXT, x, y, w, size * 1.65f, rgba);
    if (!n)
        return -1;
    memcpy(c->strings + c->used, text, bytes);
    n->payload = c->strings + c->used;
    c->used += bytes;
    n->font_size = size;
    n->resource_id = 1;
    asmui_text_msdf(n, 8, 0, 0);
    /* Keep long labels inside their authored box. */
    n->flags |= ASMUI_CLIP;
    return (int)c->count - 1;
}
int asmui_canvas_printf(AsmUiCanvas* c, float x, float y, float w, float size, uint32_t rgba,
                        const char* fmt, ...) {
    char buffer[4096];
    va_list args;
    va_start(args, fmt);
    int count = vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    if (count < 0 || (size_t)count >= sizeof(buffer)) {
        c->overflow = 1;
        return -1;
    }
    return asmui_canvas_text(c, x, y, w, size, rgba, buffer);
}
int asmui_canvas_clip(AsmUiCanvas* c, float x, float y, float w, float h) {
    AsmUiNode* n = box(c, ASMUI_NODE_CONTAINER, x, y, w, h, 0);
    if (!n)
        return -1;
    n->flags |= ASMUI_CLIP;
    return (int)c->count - 1;
}
void asmui_canvas_line(AsmUiCanvas* c, float x0, float y0, float x1, float y1, float width,
                       uint32_t rgba) {
    float dx = x1 - x0, dy = y1 - y0, length = hypotf(dx, dy);
    AsmUiNode* n = box(c, ASMUI_NODE_IMAGE, x0, y0 - width * .5f, length, width, rgba);
    if (n) {
        n->rotation = -atan2f(dy, dx);
        n->pivot_y = .5f;
        n->anchored_y -= width * .5f;
    }
}
