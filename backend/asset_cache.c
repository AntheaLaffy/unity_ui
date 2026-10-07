#include "asmui_asset_cache.h"
#include <limits.h>
#include <string.h>

void asmui_asset_cache_init(AsmUiAssetCache *c,
                            AsmUiAssetCacheEntry *entries,
                            uint32_t capacity,
                            uint32_t resource_base,
                            uint32_t protect_epochs,
                            uint32_t load_budget) {
    if (!c) return;
    memset(c, 0, sizeof(*c));
    c->entries = entries;
    c->capacity = entries ? capacity : 0u;
    c->resource_base = resource_base;
    c->protect_epochs = protect_epochs;
    c->load_budget = load_budget ? load_budget : 1u;
    if (entries && capacity) memset(entries, 0, sizeof(*entries) * capacity);
    for (uint32_t i = 0; entries && i < capacity; ++i)
        entries[i].resource_id = resource_base + i;
}

void asmui_asset_cache_begin_epoch(AsmUiAssetCache *c, uint64_t epoch) {
    if (!c) return;
    c->epoch = epoch;
    c->loads_this_epoch = 0u;
}

static int find_key(const AsmUiAssetCache *c, uint64_t key) {
    if (!c || !c->entries) return -1;
    for (uint32_t i = 0; i < c->capacity; ++i) {
        if (c->entries[i].state != ASMUI_ASSET_EMPTY && c->entries[i].key == key)
            return (int)i;
    }
    return -1;
}

int asmui_asset_cache_lookup(AsmUiAssetCache *c, uint64_t key,
                             uint32_t *resource_id, uint32_t *slot) {
    int idx = find_key(c, key);
    if (idx < 0) return 0;
    AsmUiAssetCacheEntry *e = &c->entries[idx];
    e->last_touch_epoch = c->epoch;
    if (resource_id) *resource_id = e->resource_id;
    if (slot) *slot = (uint32_t)idx;
    ++c->stats.hits;
    if (e->state == ASMUI_ASSET_READY) return ASMUI_ASSET_HIT;
    if (e->state == ASMUI_ASSET_PENDING) return ASMUI_ASSET_PENDING_RESULT;
    return ASMUI_ASSET_NEGATIVE;
}

static int choose_slot(const AsmUiAssetCache *c) {
    int empty = -1;
    int lru = -1;
    uint64_t lru_epoch = UINT64_MAX;
    for (uint32_t i = 0; i < c->capacity; ++i) {
        const AsmUiAssetCacheEntry *e = &c->entries[i];
        if (e->state == ASMUI_ASSET_EMPTY) {
            empty = (int)i;
            break;
        }
        if (e->state == ASMUI_ASSET_PENDING) continue;
        uint64_t age = c->epoch >= e->last_touch_epoch
                     ? c->epoch - e->last_touch_epoch : 0u;
        if (age >= c->protect_epochs && e->last_touch_epoch < lru_epoch) {
            lru_epoch = e->last_touch_epoch;
            lru = (int)i;
        }
    }
    return empty >= 0 ? empty : lru;
}

int asmui_asset_cache_request(AsmUiAssetCache *c, uint64_t key,
                              AsmUiAssetLoadFn loader, void *user,
                              uint32_t *resource_id, uint32_t *slot) {
    if (!c || !c->entries || !c->capacity || !loader) return ASMUI_ASSET_LOAD_FAILED;
    int existing = find_key(c, key);
    if (existing >= 0) {
        AsmUiAssetCacheEntry *e = &c->entries[existing];
        e->last_touch_epoch = c->epoch;
        if (resource_id) *resource_id = e->resource_id;
        if (slot) *slot = (uint32_t)existing;
        ++c->stats.hits;
        if (e->state == ASMUI_ASSET_READY) return ASMUI_ASSET_HIT;
        if (e->state == ASMUI_ASSET_PENDING) return ASMUI_ASSET_PENDING_RESULT;
        return ASMUI_ASSET_NEGATIVE;
    }

    ++c->stats.misses;
    if (c->loads_this_epoch >= c->load_budget) {
        ++c->stats.deferred;
        return ASMUI_ASSET_DEFERRED;
    }
    int idx = choose_slot(c);
    if (idx < 0) {
        ++c->stats.deferred;
        return ASMUI_ASSET_DEFERRED;
    }

    AsmUiAssetCacheEntry *e = &c->entries[idx];
    int had_old = e->state != ASMUI_ASSET_EMPTY;
    uint64_t old_key = e->key;
    if (had_old) ++c->stats.evictions;
    ++c->loads_this_epoch;
    int load_result = loader(user, (uint32_t)idx, key, e->resource_id, old_key, had_old);
    e->key = key;
    e->last_touch_epoch = c->epoch;
    if (resource_id) *resource_id = e->resource_id;
    if (slot) *slot = (uint32_t)idx;

    if (load_result == ASMUI_ASSET_LOADER_READY) {
        e->state = ASMUI_ASSET_READY;
        ++c->stats.loads;
        return ASMUI_ASSET_LOADED;
    }
    if (load_result == ASMUI_ASSET_LOADER_PENDING) {
        e->state = ASMUI_ASSET_PENDING;
        ++c->stats.pending_submits;
        return ASMUI_ASSET_PENDING_RESULT;
    }
    e->state = ASMUI_ASSET_FAILED;
    ++c->stats.failures;
    return ASMUI_ASSET_LOAD_FAILED;
}

int asmui_asset_cache_complete(AsmUiAssetCache *c,
                               uint32_t slot,
                               uint64_t key,
                               int ok) {
    if (!c || !c->entries || slot >= c->capacity) return 0;
    AsmUiAssetCacheEntry *e = &c->entries[slot];
    if (e->state != ASMUI_ASSET_PENDING || e->key != key) return 0;
    e->last_touch_epoch = c->epoch;
    e->state = ok ? ASMUI_ASSET_READY : ASMUI_ASSET_FAILED;
    ++c->stats.pending_completions;
    if (ok) ++c->stats.loads;
    else ++c->stats.failures;
    return 1;
}

uint32_t asmui_asset_cache_resident(const AsmUiAssetCache *c) {
    if (!c || !c->entries) return 0u;
    uint32_t n = 0;
    for (uint32_t i = 0; i < c->capacity; ++i)
        if (c->entries[i].state == ASMUI_ASSET_READY) ++n;
    return n;
}

uint32_t asmui_asset_cache_pending(const AsmUiAssetCache *c) {
    if (!c || !c->entries) return 0u;
    uint32_t n = 0;
    for (uint32_t i = 0; i < c->capacity; ++i)
        if (c->entries[i].state == ASMUI_ASSET_PENDING) ++n;
    return n;
}
