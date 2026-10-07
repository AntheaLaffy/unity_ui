#include "asmui_asset_cache.h"
#include <stdio.h>
#include <string.h>

typedef struct FakePayload {
    uint64_t key[4];
    uint32_t loads;
    uint32_t recycles;
    uint32_t pending_slot;
} FakePayload;

static int fake_load(void *u, uint32_t slot, uint64_t key, uint32_t resource_id,
                     uint64_t old_key, int had_old) {
    FakePayload *p = (FakePayload *)u;
    (void)resource_id; (void)old_key;
    if (had_old) ++p->recycles;
    ++p->loads;
    p->key[slot] = key;
    if (key == 13u) return ASMUI_ASSET_LOADER_FAILED;
    if (key == 30u) { p->pending_slot = slot; return ASMUI_ASSET_LOADER_PENDING; }
    return ASMUI_ASSET_LOADER_READY;
}

int main(void) {
    AsmUiAssetCacheEntry entries[4];
    AsmUiAssetCache c;
    FakePayload p; memset(&p, 0, sizeof(p));
    asmui_asset_cache_init(&c, entries, 4u, 16u, 3u, 2u);

    asmui_asset_cache_begin_epoch(&c, 0u);
    uint32_t r=0,s=0;
    if (asmui_asset_cache_request(&c, 10u, fake_load, &p, &r, &s) != ASMUI_ASSET_LOADED || r != 16u) return 1;
    if (asmui_asset_cache_request(&c, 11u, fake_load, &p, &r, &s) != ASMUI_ASSET_LOADED) return 2;
    if (asmui_asset_cache_request(&c, 12u, fake_load, &p, &r, &s) != ASMUI_ASSET_DEFERRED) return 3;

    asmui_asset_cache_begin_epoch(&c, 1u);
    if (asmui_asset_cache_request(&c, 10u, fake_load, &p, &r, &s) != ASMUI_ASSET_HIT) return 4;
    if (asmui_asset_cache_request(&c, 12u, fake_load, &p, &r, &s) != ASMUI_ASSET_LOADED) return 5;
    if (asmui_asset_cache_request(&c, 13u, fake_load, &p, &r, &s) != ASMUI_ASSET_LOAD_FAILED) return 6;
    if (asmui_asset_cache_request(&c, 13u, fake_load, &p, &r, &s) != ASMUI_ASSET_NEGATIVE) return 7;
    if (asmui_asset_cache_resident(&c) != 3u) return 8;

    /* All four slots are protected until epoch 4, so a new key must defer. */
    asmui_asset_cache_begin_epoch(&c, 2u);
    if (asmui_asset_cache_request(&c, 20u, fake_load, &p, &r, &s) != ASMUI_ASSET_DEFERRED) return 9;

    asmui_asset_cache_begin_epoch(&c, 4u);
    if (asmui_asset_cache_request(&c, 20u, fake_load, &p, &r, &s) != ASMUI_ASSET_LOADED) return 10;
    if (c.stats.evictions != 1u || p.recycles != 1u) return 11;
    if (asmui_asset_cache_resident(&c) != 3u) return 12;

    /* Pending assets are not resident, cannot be evicted, and become visible
       only after an explicit completion event from the async uploader. */
    asmui_asset_cache_begin_epoch(&c, 8u);
    if (asmui_asset_cache_request(&c, 30u, fake_load, &p, &r, &s) != ASMUI_ASSET_PENDING_RESULT) return 13;
    if (asmui_asset_cache_pending(&c) != 1u) return 14;
    if (asmui_asset_cache_lookup(&c, 30u, &r, &s) != ASMUI_ASSET_PENDING_RESULT) return 15;
    if (!asmui_asset_cache_complete(&c, p.pending_slot, 30u, 1)) return 16;
    if (asmui_asset_cache_pending(&c) != 0u) return 17;
    if (asmui_asset_cache_lookup(&c, 30u, &r, &s) != ASMUI_ASSET_HIT) return 18;

    printf("asset cache OK: cap=%u resident=%u loads=%llu evictions=%llu deferred=%llu failures=%llu pending-submits=%llu pending-completions=%llu\n",
           c.capacity, asmui_asset_cache_resident(&c),
           (unsigned long long)c.stats.loads, (unsigned long long)c.stats.evictions,
           (unsigned long long)c.stats.deferred, (unsigned long long)c.stats.failures,
           (unsigned long long)c.stats.pending_submits,
           (unsigned long long)c.stats.pending_completions);
    return 0;
}
