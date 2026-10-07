#include "asmui_image_png.h"
#include <png.h>
#include <stdlib.h>
#include <string.h>

int asmui_image_png_load(const char *path, AsmUiImageRgba8 *out) {
    if (!path || !out) return 0;
    memset(out, 0, sizeof(*out));

    png_image image;
    memset(&image, 0, sizeof(image));
    image.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_file(&image, path)) return 0;

    image.format = PNG_FORMAT_RGBA;
    if (!image.width || !image.height || image.width > 16384u || image.height > 16384u) {
        png_image_free(&image);
        return 0;
    }

    png_alloc_size_t bytes = PNG_IMAGE_SIZE(image);
    uint8_t *pixels = (uint8_t *)malloc((size_t)bytes);
    if (!pixels) {
        png_image_free(&image);
        return 0;
    }

    if (!png_image_finish_read(&image, NULL, pixels, 0, NULL)) {
        free(pixels);
        png_image_free(&image);
        return 0;
    }

    out->pixels = pixels;
    out->width = image.width;
    out->height = image.height;
    png_image_free(&image);
    return 1;
}

void asmui_image_rgba8_destroy(AsmUiImageRgba8 *img) {
    if (!img) return;
    free(img->pixels);
    memset(img, 0, sizeof(*img));
}
