#ifndef ASMUI_CANVAS_H
#define ASMUI_CANVAS_H
#include "asmui.h"
#include <stdarg.h>

/* Top-left pixel coordinates for desktop applications. Storage belongs to the
 * caller; a frame can never overrun the node or UTF-8 arenas. */
typedef struct AsmUiCanvas {
    AsmUiNode* nodes;
    uint32_t count, capacity;
    char* strings;
    size_t used, string_capacity;
    int32_t parent;
    float width, height;
    int overflow;
} AsmUiCanvas;
void asmui_canvas_begin(AsmUiCanvas*, AsmUiNode*, uint32_t, char*, size_t, float, float);
int asmui_canvas_rect(AsmUiCanvas*, float, float, float, float, uint32_t, float);
int asmui_canvas_text(AsmUiCanvas*, float, float, float, float, uint32_t, const char*);
int asmui_canvas_printf(AsmUiCanvas*, float, float, float, float, uint32_t, const char*, ...);
int asmui_canvas_clip(AsmUiCanvas*, float, float, float, float);
void asmui_canvas_line(AsmUiCanvas*, float, float, float, float, float, uint32_t);
#endif
