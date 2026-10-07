#include "asmui_application.h"
#include "asmui_builder.h"
#include <dlfcn.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* Internal flag: payload points into the borrowed application frame. */
#define APP_QUAD_NODE (1u << 31)

int asmui_app_open(AsmUiAppHost *a, const char *project, const char *data) {
    memset(a, 0, sizeof(*a));
    char path[4096];
    int n = snprintf(path, sizeof(path), "%s/target/release/libopensekai.so", project);
    if (n < 0 || (size_t)n >= sizeof(path)) return 0;
    a->library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!a->library) { fprintf(stderr, "Application: %s\n", dlerror()); return 0; }
    AsmUiGetApplication get = (AsmUiGetApplication)dlsym(a->library, "asmui_application_get_api");
    if (!get || !(a->api = get(ASMUI_APPLICATION_ABI)) ||
        a->api->abi != ASMUI_APPLICATION_ABI || a->api->size < sizeof(*a->api) ||
        !a->api->create || !a->api->destroy || !a->api->font_path ||
        !a->api->font_samples || !a->api->frame || !a->api->event || !a->api->take_request) {
        fprintf(stderr, "Application ABI mismatch: %s\n", path);
        asmui_app_close(a); return 0;
    }
    a->instance = a->api->create(project, data);
    if (!a->instance || !a->api->frame(a->instance, 0, 0, &a->frame)) {
        asmui_app_close(a); return 0;
    }
    printf("Application: %s, ABI %u\n", a->api->name, a->api->abi);
    return 1;
}
void asmui_app_close(AsmUiAppHost *a) {
    if (a->instance) a->api->destroy(a->instance);
    if (a->library) dlclose(a->library);
    memset(a, 0, sizeof(*a));
}
uint32_t asmui_app_build(AsmUiAppHost *a, AsmUiNode *nodes, uint32_t start,
                         uint32_t capacity, int32_t parent, const float viewport[4]) {
    if (!a->api->frame(a->instance, a->dt, a->locale, &a->frame) ||
        !isfinite(a->frame.width) || !isfinite(a->frame.height) ||
        a->frame.width <= 0 || a->frame.height <= 0 || !a->frame.draws)
        return start;
    memcpy(a->viewport, viewport, sizeof(a->viewport));
    a->scale = fminf(viewport[2] / a->frame.width, viewport[3] / a->frame.height);
    float w = a->frame.width * a->scale, h = a->frame.height * a->scale;
    float x = viewport[0] + (viewport[2] - w) * .5f;
    float y = viewport[1] + (viewport[3] - h) * .5f;
    float canvas[] = {x, y, w, h};
    memcpy(a->canvas, canvas, sizeof(canvas));
    float cx = viewport[0] + viewport[2] * .5f, cy = viewport[1] + viewport[3] * .5f;
    for (uint32_t i = 0; i < a->frame.count; ++i) {
        const AsmUiAppDraw *d = &a->frame.draws[i];
        if (start + 2 > capacity) return capacity;
        /* Explicit clip containers retain application scroll masks inside the
           editor viewport. No application node can paint over a dock or menu. */
        float bounds[4];
        memcpy(bounds, d->clip, sizeof(bounds));
        if (d->kind == APP_TEXT) {
            float right = fminf(bounds[0] + bounds[2], d->rect[0] + d->rect[2]);
            float bottom = fminf(bounds[1] + bounds[3], d->rect[1] + d->rect[3]);
            bounds[0] = fmaxf(bounds[0], d->rect[0]);
            bounds[1] = fmaxf(bounds[1], d->rect[1]);
            bounds[2] = fmaxf(0, right - bounds[0]);
            bounds[3] = fmaxf(0, bottom - bounds[1]);
        }
        AsmUiNode clip = asmui_node(ASMUI_NODE_CONTAINER, parent, 0xffffffff);
        clip.flags |= ASMUI_CLIP;
        clip.size_delta_x = bounds[2] * a->scale;
        clip.size_delta_y = bounds[3] * a->scale;
        clip.anchored_x = x + (bounds[0] + bounds[2] * .5f) * a->scale - cx;
        clip.anchored_y = cy - y - (bounds[1] + bounds[3] * .5f) * a->scale;
        nodes[start] = clip;
        AsmUiNode n = asmui_node(d->kind == APP_TEXT ? ASMUI_NODE_TEXT : ASMUI_NODE_IMAGE,
                                 (int32_t)start++, d->rgba);
        n.size_delta_x = d->rect[2] * a->scale;
        n.size_delta_y = d->rect[3] * a->scale;
        n.anchored_x = (d->rect[0] + d->rect[2] * .5f - bounds[0] - bounds[2] * .5f) * a->scale;
        n.anchored_y = (bounds[1] + bounds[3] * .5f - d->rect[1] - d->rect[3] * .5f) * a->scale;
        n.rotation = d->rotation;
        if (d->kind == APP_TEXT) {
            n.flags |= ASMUI_CLIP;
            n.resource_id = d->font == 1 ? 66 : 131;
            n.payload = d->text;
            n.font_size = d->font_size * a->scale;
            n.text_align = d->align;
            asmui_text_msdf(&n, 16, 0, 0);
            if (d->reserved & 1u) n.material3 = d->min_font_size * a->scale;
        } else if (d->kind == APP_SPRITE || d->kind == APP_QUAD) {
            if (!a->resolve_image || !d->text) continue;
            n.resource_id = a->resolve_image(a->image_user, d->text);
            if (!n.resource_id) continue;
            n.flags |= ASMUI_UV_EXPLICIT;
            n.uv_x0 = d->uv[0]; n.uv_y0 = d->uv[1];
            n.uv_x1 = d->uv[2]; n.uv_y1 = d->uv[3];
            if (d->reserved & 2u) asmui_material_hsv(&n, 0, 1, 1.2f);
            if (d->reserved & 4u) n.material_id = ASMUI_MATERIAL_ADDITIVE;
            if (d->reserved & 8u)
                asmui_set_material4(&n, ASMUI_MATERIAL_ATLAS_RGB_MIX, d->font_size, 0,
                                    d->min_font_size, 0);
            if (d->kind == APP_QUAD) {
                n.flags |= APP_QUAD_NODE;
                n.payload = (const char *)d;
                if (d->reserved & 16u) n.material_id = ASMUI_MATERIAL_PROJECTIVE;
            }
        }
        nodes[start++] = n;
    }
    return start;
}
void asmui_app_patch_quads(const AsmUiAppHost *a, AsmUiContext *ctx) {
    for (uint32_t i = 0; i < ctx->draw_count; ++i) {
        AsmUiDrawCmd *draw = &ctx->draws[i];
        if (!(draw->flags & APP_QUAD_NODE) || !draw->payload) continue;
        const AsmUiAppDraw *quad = (const AsmUiAppDraw *)draw->payload;
        float *xy = &draw->x0;
        for (uint32_t p = 0; p < 4; ++p) {
            xy[p * 2] = a->canvas[0] + quad->xy[p * 2] * a->scale;
            xy[p * 2 + 1] = a->canvas[1] + quad->xy[p * 2 + 1] * a->scale;
        }
        if (quad->reserved & 16u) {
            /* A planar rectangle's world diagonals share their midpoint.
               Their screen intersection recovers relative camera depths,
               so the application ABI needs no additional vertex fields. */
            const float *p = quad->xy;
            double ax = (double)p[4]-p[0], ay = (double)p[5]-p[1];
            double bx = (double)p[6]-p[2], by = (double)p[7]-p[3];
            double dx = (double)p[2]-p[0], dy = (double)p[3]-p[1];
            double det = ax*by-ay*bx;
            double t = (dx*by-dy*bx)/det, s = (dx*ay-dy*ax)/det;
            float weights[4] = {1,1,1,1};
            if (isfinite(t) && isfinite(s) && t > 0 && t < 1 && s > 0 && s < 1) {
                weights[0] = (float)(2*(1-t)); weights[1] = (float)(2*(1-s));
                weights[2] = (float)(2*t); weights[3] = (float)(2*s);
            }
            memcpy(&draw->material0, weights, sizeof(weights));
        }
        draw->flags &= ~APP_QUAD_NODE;
        draw->payload = NULL;
    }
}
int asmui_app_event(AsmUiAppHost *a, AsmUiAppEvent *event) {
    if (!a->instance || a->scale <= 0) return 0;
    if (event->kind >= APP_POINTER_DOWN && event->kind <= APP_WHEEL) {
        if (event->x < a->canvas[0] || event->y < a->canvas[1] ||
            event->x >= a->canvas[0] + a->canvas[2] || event->y >= a->canvas[1] + a->canvas[3]) return 0;
        event->x = (event->x - a->canvas[0]) / a->scale;
        event->y = (event->y - a->canvas[1]) / a->scale;
    }
    return a->api->event(a->instance, event);
}
