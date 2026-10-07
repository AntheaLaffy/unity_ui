/* Thin SDL3 backend. The assembly core owns UI semantics and command generation. */
#include "asmui_sdl3.h"
#include <math.h>
#include <stdint.h>
#include <string.h>

static void unpack_rgba(uint32_t c, float alpha_mul, SDL_FColor *out) {
    out->r = ((c >> 24) & 255) / 255.0f;
    out->g = ((c >> 16) & 255) / 255.0f;
    out->b = ((c >>  8) & 255) / 255.0f;
    out->a = ((c      ) & 255) / 255.0f * alpha_mul;
}

static SDL_BlendMode blend_for(uint32_t material_id) {
    switch (material_id) {
        case ASMUI_MATERIAL_ADDITIVE: return SDL_BLENDMODE_ADD;
        case ASMUI_MATERIAL_MULTIPLY: return SDL_BLENDMODE_MUL;
        default: return SDL_BLENDMODE_BLEND;
    }
}

void asmui_sdl3_init(AsmUiSdl3Backend *backend) {
    memset(backend, 0, sizeof(*backend));
}

int asmui_sdl3_set_texture(AsmUiSdl3Backend *backend, uint32_t resource_id, SDL_Texture *texture) {
    if (resource_id >= ASMUI_SDL3_MAX_TEXTURES) return 0;
    backend->textures[resource_id] = texture;
    return 1;
}

void asmui_sdl3_set_text_renderer(AsmUiSdl3Backend *backend, AsmUiSdl3TextRenderer fn, void *user) {
    backend->text_renderer = fn;
    backend->text_user = user;
}

static void render_quad(SDL_Renderer *r, const AsmUiDrawCmd *d,
                        SDL_Texture *texture) {
    SDL_FColor c;
    unpack_rgba(d->rgba, d->alpha, &c);
    SDL_FColor top = c;
    if (d->flags & ASMUI_GRADIENT_Y) {
        uint32_t rgba;
        memcpy(&rgba, &d->material3, sizeof(rgba));
        unpack_rgba(rgba, d->alpha, &top);
    }
    SDL_Vertex v[4] = {
        {{d->x0, d->y0}, c, {d->u0,d->v1}},
        {{d->x1, d->y1}, c, {d->u1,d->v1}},
        {{d->x2, d->y2}, top, {d->u1,d->v0}},
        {{d->x3, d->y3}, top, {d->u0,d->v0}},
    };
    const int idx[6] = {0,1,2, 0,2,3};
    SDL_BlendMode blend = blend_for(d->material_id);
    SDL_SetRenderDrawBlendMode(r, blend);
    if (texture) SDL_SetTextureBlendMode(texture, blend);
    SDL_RenderGeometry(r, texture, v, 4, idx, 6);
}

void asmui_sdl3_render(SDL_Renderer *r, const AsmUiContext *ctx, AsmUiSdl3Backend *backend) {
    for (uint32_t i = 0; i < ctx->draw_count; ++i) {
        const AsmUiDrawCmd *d = &ctx->draws[i];
        SDL_Rect clip = {
            (int)floorf(d->clip_x),
            (int)floorf(d->clip_y),
            (int)ceilf(d->clip_w),
            (int)ceilf(d->clip_h),
        };
        SDL_SetRenderClipRect(r, &clip);

        if (d->type == ASMUI_NODE_IMAGE) {
            SDL_Texture *texture = NULL;
            if (backend && d->resource_id < ASMUI_SDL3_MAX_TEXTURES)
                texture = backend->textures[d->resource_id];
            render_quad(r, d, texture);
        } else if (d->type == ASMUI_NODE_TEXT && backend && backend->text_renderer) {
            backend->text_renderer(r, d, ctx->viewport_h, backend->text_user);
        }
    }
    SDL_SetRenderClipRect(r, NULL);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
}
