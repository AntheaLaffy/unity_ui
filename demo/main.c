#include "asmui.h"
#include "asmui_builder.h"
#include "asmui_sdl3.h"
#include <SDL3/SDL.h>
#include <stdint.h>
#include <stdio.h>

int main(void) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "%s\n", SDL_GetError());
        return 1;
    }
    SDL_Window *w = SDL_CreateWindow("ASMUI rhythm-game visual shell", 1280, 720, SDL_WINDOW_RESIZABLE);
    SDL_Renderer *r = w ? SDL_CreateRenderer(w, NULL) : NULL;
    if (!w || !r) {
        fprintf(stderr, "%s\n", SDL_GetError());
        return 1;
    }

    AsmUiContext ui;
    AsmUiNode nodes[64] = {0};
    AsmUiDrawCmd draws[256] = {0};
    AsmUiTween tweens[16] = {0};
    AsmUiSdl3Backend backend;
    asmui_sdl3_init(&backend);
    asm_ui_init(&ui, nodes, 64, draws, 256, tweens, 16);

    nodes[0] = asmui_node(ASMUI_NODE_IMAGE, -1, 0x111827FFu);
    asmui_stretch(&nodes[0]);

    nodes[1] = asmui_node(ASMUI_NODE_IMAGE, 0, 0x202A44FFu); // song jacket
    nodes[1].anchored_x = -260;
    nodes[1].size_delta_x = 420;
    nodes[1].size_delta_y = 420;
    nodes[1].rotation = -0.035f;

    nodes[2] = asmui_node(ASMUI_NODE_IMAGE, 0, 0xE9C46AFFu); // difficulty ribbon
    nodes[2].anchored_x = 250;
    nodes[2].anchored_y = 120;
    nodes[2].size_delta_x = 520;
    nodes[2].size_delta_y = 84;
    nodes[2].rotation = 0.03f;

    nodes[3] = asmui_node(ASMUI_NODE_IMAGE, 0, 0xE76F51FFu); // play button
    nodes[3].anchored_x = 330;
    nodes[3].anchored_y = -190;
    nodes[3].size_delta_x = 260;
    nodes[3].size_delta_y = 92;
    nodes[3].material_id = ASMUI_MATERIAL_ADDITIVE;

    nodes[4] = asmui_node(ASMUI_NODE_IMAGE, 0, 0x2A9D8FFFu); // top status bar
    nodes[4].anchor_min_x = 0;
    nodes[4].anchor_max_x = 1;
    nodes[4].anchor_min_y = nodes[4].anchor_max_y = 1;
    nodes[4].pivot_y = 1;
    nodes[4].anchored_y = -24;
    nodes[4].size_delta_y = 72;

    // Gauge background + filled foreground.
    nodes[5] = asmui_node(ASMUI_NODE_IMAGE, 0, 0x202938FFu);
    nodes[5].anchored_x = 250;
    nodes[5].anchored_y = 25;
    nodes[5].size_delta_x = 520;
    nodes[5].size_delta_y = 22;

    nodes[6] = asmui_node(ASMUI_NODE_IMAGE, 5, 0x4ADE80FFu);
    asmui_stretch(&nodes[6]);
    nodes[6].pivot_x = 0;
    nodes[6].image_mode = ASMUI_IMAGE_FILLED_HORIZONTAL;
    nodes[6].fill_amount = 0.15f;

    ui.node_count = 7;
    tweens[0] = (AsmUiTween){3, ASMUI_PROP_SCALE_X, ASMUI_EASE_IN_OUT_CUBIC,
        ASMUI_TWEEN_ACTIVE|ASMUI_TWEEN_LOOP, 0, 0.8f, 0.96f, 1.05f};
    tweens[1] = (AsmUiTween){3, ASMUI_PROP_SCALE_Y, ASMUI_EASE_IN_OUT_CUBIC,
        ASMUI_TWEEN_ACTIVE|ASMUI_TWEEN_LOOP, 0, 0.8f, 0.96f, 1.05f};
    tweens[2] = (AsmUiTween){6, ASMUI_PROP_FILL, ASMUI_EASE_IN_OUT_CUBIC,
        ASMUI_TWEEN_ACTIVE|ASMUI_TWEEN_LOOP, 0, 2.0f, 0.10f, 1.0f};
    ui.tween_count = 3;

    uint64_t last = SDL_GetTicks();
    int running = 1;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) if (e.type == SDL_EVENT_QUIT) running = 0;

        int ww, hh;
        SDL_GetWindowSizeInPixels(w, &ww, &hh);
        uint64_t now = SDL_GetTicks();
        float dt = (now-last) / 1000.0f;
        last = now;

        asm_ui_set_viewport(&ui, (float)ww, (float)hh);
        asm_ui_begin_frame(&ui, dt);
        asm_ui_tick_tweens(&ui);
        asm_ui_layout(&ui);
        asm_ui_build_drawlist(&ui);

        SDL_SetRenderDrawColor(r, 8, 12, 20, 255);
        SDL_RenderClear(r);
        asmui_sdl3_render(r, &ui, &backend);
        SDL_RenderPresent(r);
    }

    SDL_DestroyRenderer(r);
    SDL_DestroyWindow(w);
    SDL_Quit();
    return 0;
}
