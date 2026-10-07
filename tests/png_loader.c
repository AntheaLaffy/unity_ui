#include "asmui_image_png.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>

static const unsigned char k_png[] = {
0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0x00,0x00,0x00,0x0d,0x49,0x48,0x44,0x52,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x02,0x08,0x06,0x00,0x00,0x00,0x72,0xb6,0x0d,0x24,0x00,0x00,0x00,0x12,0x49,0x44,0x41,0x54,0x78,0x9c,0x63,0xf8,0xcf,0xc0,0xf0,0x1f,0x0c,0x81,0x34,0x18,0x00,0x00,0x49,0xc8,0x09,0xf7,0xf9,0xab,0xb6,0x0d,0x00,0x00,0x00,0x00,0x49,0x45,0x4e,0x44,0xae,0x42,0x60,0x82
};

int main(void) {
    const char *path = "build/png_loader_test.png";
    FILE *f = fopen(path, "wb");
    if (!f) return 1;
    if (fwrite(k_png, 1, sizeof(k_png), f) != sizeof(k_png)) { fclose(f); return 2; }
    fclose(f);

    AsmUiImageRgba8 img = {0};
    if (!asmui_image_png_load(path, &img)) {
        fprintf(stderr, "PNG loader failed\n");
        return 3;
    }
    if (img.width != 2u || img.height != 2u || !img.pixels) {
        fprintf(stderr, "PNG dimensions mismatch: %ux%u\n", img.width, img.height);
        asmui_image_rgba8_destroy(&img);
        return 4;
    }
    /* Top-left red, top-right green, bottom-left blue, bottom-right white. */
    const uint8_t expected[16] = {
        255,0,0,255, 0,255,0,255,
        0,0,255,255, 255,255,255,255
    };
    if (memcmp(img.pixels, expected, sizeof(expected)) != 0) {
        fprintf(stderr, "PNG RGBA decode mismatch\n");
        asmui_image_rgba8_destroy(&img);
        return 5;
    }
    asmui_image_rgba8_destroy(&img);
    remove(path);
    puts("PNG asset loader OK: 2x2 RGBA decode exact");
    return 0;
}
