#ifndef ASMUI_IMAGE_PNG_H
#define ASMUI_IMAGE_PNG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AsmUiImageRgba8 {
    uint8_t *pixels;
    uint32_t width;
    uint32_t height;
} AsmUiImageRgba8;

int asmui_image_png_load(const char *path, AsmUiImageRgba8 *out);
void asmui_image_rgba8_destroy(AsmUiImageRgba8 *img);

#ifdef __cplusplus
}
#endif

#endif
