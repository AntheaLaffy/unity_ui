#define _POSIX_C_SOURCE 200809L
#include "asmui.h"
#include "demo/song_select_scene.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static uint64_t ns_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

int main(void) {
    static AsmUiNode nodes[ASMUI_SONG_MAX_NODES];
    static AsmUiDrawCmd draws[ASMUI_SONG_MAX_NODES * 2];
    static AsmUiTween tweens[8];
    static AsmUiBatch batches[ASMUI_SONG_MAX_NODES * 2];
    static AsmUiGpuVertex verts[ASMUI_SONG_MAX_NODES * 8];
    AsmUiSongSceneInfo scene = asmui_song_select_build(nodes, ASMUI_SONG_MAX_NODES);
    if (scene.node_count < 100 || scene.node_count >= ASMUI_SONG_MAX_NODES) {
        fprintf(stderr, "song scene node count suspicious: %u\n", scene.node_count);
        return 1;
    }
    AsmUiContext ctx;
    asm_ui_init(&ctx, nodes, ASMUI_SONG_MAX_NODES, draws, ASMUI_SONG_MAX_NODES*2, tweens, 8);
    ctx.node_count = scene.node_count;
    asm_ui_set_viewport(&ctx, ASMUI_SONG_W, ASMUI_SONG_H);

    enum { SAMPLES=9, ITERS=400 };
    double sample_ns[SAMPLES];
    uint32_t last_batches=0, last_draws=0;
    for (int s=0;s<SAMPLES;++s) {
        uint64_t t0=ns_now();
        for (int it=0;it<ITERS;++it) {
            asm_ui_begin_frame(&ctx, 1.0f/60.0f);
            asm_ui_layout(&ctx);
            asm_ui_build_drawlist(&ctx);
            last_batches=asm_ui_build_batches(&ctx,batches,ASMUI_SONG_MAX_NODES*2);
            (void)asm_ui_build_gpu_vertices(&ctx,verts,ASMUI_SONG_MAX_NODES*8);
            last_draws=ctx.draw_count;
        }
        uint64_t t1=ns_now();
        sample_ns[s]=(double)(t1-t0)/(double)(ITERS*last_draws);
    }
    for (int i=0;i<SAMPLES;++i) for(int j=i+1;j<SAMPLES;++j)
        if(sample_ns[j]<sample_ns[i]) { double t=sample_ns[i]; sample_ns[i]=sample_ns[j]; sample_ns[j]=t; }
    double median=sample_ns[SAMPLES/2];
    double max_ns=1000.0;
    const char *env=getenv("ASMUI_SCENE_MAX_NS_PER_DRAW");
    if(env&&*env) max_ns=strtod(env,NULL);
    printf("song scene profile: nodes=%u draws=%u panels=%u glyph-pixels=%u batches=%u ns/draw min/med/max=%.1f/%.1f/%.1f\n",
           scene.node_count,last_draws,scene.panel_count,scene.glyph_pixel_count,last_batches,
           sample_ns[0],median,sample_ns[SAMPLES-1]);
    /* This scene intentionally mixes default/rounded materials, but a batch explosion
       into hundreds of state changes would indicate a renderer regression. */
    if(last_batches>96) {
        fprintf(stderr,"SCENE BATCH REGRESSION: %u batches > 96 guardrail\n",last_batches);
        return 2;
    }
    if(median>max_ns) {
        fprintf(stderr,"SCENE CPU PERF REGRESSION: %.1f ns/draw > %.1f guardrail\n",median,max_ns);
        return 3;
    }
    puts("song scene CPU regression OK");
    return 0;
}
