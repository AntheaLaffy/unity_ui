#ifndef ASMUI_DESKTOP_H
#define ASMUI_DESKTOP_H
#include "asmui_canvas.h"
#include <SDL3/SDL.h>

typedef struct AsmUiDesktop AsmUiDesktop;
typedef struct AsmUiDesktopConfig {
    const char *title, *shader_dir, *font;
    uint32_t width, height, max_nodes, max_draws;
    uint32_t min_width, min_height;
    int headless;
    const char* const* texts;
    size_t text_count;
} AsmUiDesktopConfig;
AsmUiDesktop* asmui_desktop_open(const AsmUiDesktopConfig*);
SDL_Window* asmui_desktop_window(AsmUiDesktop*);
void asmui_desktop_size(AsmUiDesktop*, float*, float*);
void asmui_desktop_resize(AsmUiDesktop*);
/* Runs ASM layout, HarfBuzz shaping, ASM glyph/vertex emission, Vulkan drawing.
 * capture_path optionally reads back the exact rendered framebuffer as PNG. */
/* Returns 1 if a frame was submitted (and an optional capture completed).
 * Returns 0 when a stale swapchain was rebuilt before any frame submission. */
int asmui_desktop_present(AsmUiDesktop*, AsmUiCanvas*, float, const char* capture_path);
void asmui_desktop_close(AsmUiDesktop*);
#endif
