#ifndef ASMUI_SDL3_H
#define ASMUI_SDL3_H

#include "asmui.h"
#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ASMUI_SDL3_MAX_TEXTURES 256u

typedef void (*AsmUiSdl3TextRenderer)(SDL_Renderer *renderer,
                                      const AsmUiDrawCmd *cmd,
                                      float viewport_h,
                                      void *user);

typedef struct AsmUiSdl3Backend {
    SDL_Texture *textures[ASMUI_SDL3_MAX_TEXTURES];
    AsmUiSdl3TextRenderer text_renderer;
    void *text_user;
} AsmUiSdl3Backend;

void asmui_sdl3_init(AsmUiSdl3Backend *backend);
int asmui_sdl3_set_texture(AsmUiSdl3Backend *backend, uint32_t resource_id, SDL_Texture *texture);
void asmui_sdl3_set_text_renderer(AsmUiSdl3Backend *backend, AsmUiSdl3TextRenderer fn, void *user);
void asmui_sdl3_render(SDL_Renderer *renderer, const AsmUiContext *ctx, AsmUiSdl3Backend *backend);

#ifdef __cplusplus
}
#endif
#endif
