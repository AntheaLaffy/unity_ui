#define _POSIX_C_SOURCE 200809L
#include "asmui_asset_cache.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

typedef struct Fake { uint64_t keys[12]; } Fake;
static int load(void *u,uint32_t slot,uint64_t key,uint32_t r,uint64_t old,int had){
    (void)r;(void)old;(void)had; ((Fake*)u)->keys[slot]=key; return 1;
}
static uint64_t ns(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return(uint64_t)t.tv_sec*1000000000ull+t.tv_nsec;}
static int cmp(const void*a,const void*b){uint64_t x=*(const uint64_t*)a,y=*(const uint64_t*)b;return(x>y)-(x<y);}
int main(void){
    enum{FRAMES=2400,CAP=12}; AsmUiAssetCacheEntry e[CAP]; AsmUiAssetCache c; Fake f={{0}};
    asmui_asset_cache_init(&c,e,CAP,16u,3u,2u); uint64_t *samples=calloc(FRAMES,sizeof(*samples));if(!samples)return 1;
    uint32_t selected=50000u,max_resident=0;
    for(uint32_t frame=0;frame<FRAMES;++frame){
        if((frame%120u)==0u) selected=(selected+7919u)%100000u;
        uint64_t t0=ns(); asmui_asset_cache_begin_epoch(&c,frame);
        /* center-first prefetch order */
        static const int d[7]={0,-1,1,-2,2,-3,3};
        for(int i=0;i<7;++i){int64_t k=(int64_t)selected+d[i]; if(k<0||k>=100000)continue; uint32_t r,s; (void)asmui_asset_cache_request(&c,(uint64_t)k,load,&f,&r,&s);}
        uint32_t resident=asmui_asset_cache_resident(&c);if(resident>max_resident)max_resident=resident;
        samples[frame]=ns()-t0;
    }
    qsort(samples,FRAMES,sizeof(*samples),cmp); double p50=samples[FRAMES/2]/1000.0,p95=samples[(FRAMES*95u)/100u]/1000.0,p99=samples[(FRAMES*99u)/100u]/1000.0;free(samples);
    if(max_resident>CAP||c.stats.loads<20u||c.stats.evictions==0u)return 2;
    const char *x=getenv("ASMUI_ASSET_CPU_P95_US");double lim=x?strtod(x,NULL):100.0;if(p95>lim){fprintf(stderr,"asset cache CPU regression %.2f > %.2f us\n",p95,lim);return 3;}
    printf("asset stream perf: logical=100000 cache=%u max-resident=%u loads=%llu evictions=%llu hits=%llu deferred=%llu p50/p95/p99=%.2f/%.2f/%.2f us\n",CAP,max_resident,(unsigned long long)c.stats.loads,(unsigned long long)c.stats.evictions,(unsigned long long)c.stats.hits,(unsigned long long)c.stats.deferred,p50,p95,p99);
    puts("asset streaming/LRU regression OK: fixed residency independent of catalog size"); return 0;
}
