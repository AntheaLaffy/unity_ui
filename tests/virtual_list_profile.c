#define _POSIX_C_SOURCE 200809L
#include "asmui.h"
#include "asmui_interaction.h"
#include "demo/song_select_scene_text.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static uint64_t now_ns(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}
static int cmp_u64(const void *a, const void *b) {
    uint64_t x=*(const uint64_t*)a,y=*(const uint64_t*)b; return (x>y)-(x<y);
}
static int materialize(AsmUiNode *nodes, const AsmUiSongTextSceneInfo *s,
                       AsmUiVirtualCarousel *v) {
    for (uint32_t slot=0; slot<s->carousel_count; ++slot) {
        uint32_t logical=0;
        asmui_virtual_carousel_apply_slot(v,slot,&nodes[s->carousel_root[slot]],&logical);
        if (!asmui_song_carousel_bind_slot(nodes,s,slot,logical)) return 0;
    }
    return asmui_song_select_bind_logical(nodes,s,v->motion.selected);
}
int main(void) {
    enum { FRAMES=2400, LOGICAL=100000 };
    static AsmUiNode nodes[ASMUI_SONG_MAX_NODES];
    static AsmUiDrawCmd draws[ASMUI_SONG_MAX_NODES*2u];
    static AsmUiTween tweens[8];
    static AsmUiBatch batches[ASMUI_SONG_MAX_NODES*2u];
    uint64_t *samples=calloc(FRAMES,sizeof(*samples)); if(!samples) return 1;
    AsmUiSongTextSceneInfo s=asmui_song_select_build_text(nodes,ASMUI_SONG_MAX_NODES,8.0f);
    AsmUiContext ctx; asm_ui_init(&ctx,nodes,ASMUI_SONG_MAX_NODES,draws,ASMUI_SONG_MAX_NODES*2u,tweens,8u);
    asm_ui_set_viewport(&ctx,1280,720); ctx.node_count=s.base.node_count;
    AsmUiVirtualCarousel v; asmui_virtual_carousel_init(&v,LOGICAL,50000u,s.carousel_count,90,-318,0,0,156,125);
    uint32_t worst_draws=0,worst_batches=0;
    for(uint32_t f=0;f<FRAMES;++f){
        if((f%300u)==0u){ uint32_t target=(uint32_t)(((uint64_t)f*7919u)%LOGICAL); asmui_virtual_carousel_select(&v,target); }
        if((f%240u)==40u) asmui_virtual_carousel_wheel(&v,-1.0f);
        uint64_t t0=now_ns();
        asm_ui_begin_frame(&ctx,1.0f/120.0f); asmui_virtual_carousel_step(&v,1.0f/120.0f);
        if(!materialize(nodes,&s,&v)) return 2;
        asm_ui_layout(&ctx); asm_ui_build_drawlist(&ctx);
        uint32_t bc=asm_ui_build_batches(&ctx,batches,ASMUI_SONG_MAX_NODES*2u);
        if (ctx.draw_count > worst_draws) worst_draws = ctx.draw_count;
        if (bc > worst_batches) worst_batches = bc;
        samples[f]=now_ns()-t0;
    }
    qsort(samples,FRAMES,sizeof(*samples),cmp_u64);
    double p50=samples[FRAMES/2]/1000.0,p95=samples[(FRAMES*95u)/100u]/1000.0,p99=samples[(FRAMES*99u)/100u]/1000.0; free(samples);
    if(ctx.node_count>80u || worst_draws>80u || worst_batches>64u || v.slot_count!=7u){
        fprintf(stderr,"virtualization regression: logical=%u nodes=%u slots=%u draws=%u batches=%u\n",LOGICAL,ctx.node_count,v.slot_count,worst_draws,worst_batches); return 3;
    }
    const char *e=getenv("ASMUI_VIRTUAL_CPU_P95_US"); double lim=e?strtod(e,NULL):1000.0;
    if(p95>lim){fprintf(stderr,"virtual CPU regression %.2f > %.2f us\n",p95,lim);return 4;}
    printf("virtual list perf: logical=%u retained-slots=%u nodes=%u worst-draws=%u worst-batches=%u p50/p95/p99=%.2f/%.2f/%.2f us\n",LOGICAL,v.slot_count,ctx.node_count,worst_draws,worst_batches,p50,p95,p99);
    puts("100k-song virtualization regression OK: O(retained slots), not O(logical songs)");
    return 0;
}
