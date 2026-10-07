#include "asmui.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static int nearf(float a, float b) { return fabsf(a-b) < 0.0001f; }

int main(void) {
    AsmUiContext ctx = {0};
    AsmUiDrawCmd draw = {0};
    AsmUiGpuVertex v[4] = {0};
    ctx.draws = &draw;
    ctx.draw_count = 1;

    draw.x0=10; draw.y0=20;
    draw.x1=110; draw.y1=20;
    draw.x2=110; draw.y2=70;
    draw.x3=10; draw.y3=70;
    draw.u0=.25f; draw.v0=.125f; draw.u1=.75f; draw.v1=.625f;
    draw.rgba=0xAABBCCDDu; draw.alpha=.5f;
    draw.material0=1; draw.material1=2; draw.material2=3; draw.material3=4;

    uint32_t n = asm_ui_build_gpu_vertices(&ctx, v, 4);
    if (n != 4) return 1;
    if (!nearf(v[0].x,10) || !nearf(v[0].y,20) || !nearf(v[0].u,.25f) || !nearf(v[0].v,.625f) ||
        !nearf(v[2].x,110) || !nearf(v[2].y,70) || !nearf(v[2].u,.75f) || !nearf(v[2].v,.125f) ||
        !nearf(v[3].local_x,0) || !nearf(v[3].local_y,1) || v[1].rgba != 0xAABBCCDDu ||
        !nearf(v[2].material3,4)) {
        fprintf(stderr, "GPU vertex packing mismatch\n");
        return 2;
    }
    puts("ASM GPU vertex packing OK: 1 quad -> 4 vertices");
    return 0;
}
