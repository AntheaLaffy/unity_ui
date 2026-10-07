#ifndef ASMUI_ASSET_CACHE_H
#define ASMUI_ASSET_CACHE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    ASMUI_ASSET_EMPTY = 0,
    ASMUI_ASSET_READY = 1,
    ASMUI_ASSET_FAILED = 2,
    ASMUI_ASSET_PENDING = 3,
};

enum {
    ASMUI_ASSET_DEFERRED = 0,
    ASMUI_ASSET_HIT = 1,
    ASMUI_ASSET_LOADED = 2,
    ASMUI_ASSET_PENDING_RESULT = 3,
    ASMUI_ASSET_NEGATIVE = -1,
    ASMUI_ASSET_LOAD_FAILED = -2,
};

enum {
    ASMUI_ASSET_LOADER_FAILED = 0,
    ASMUI_ASSET_LOADER_READY = 1,
    ASMUI_ASSET_LOADER_PENDING = 2,
};

typedef struct AsmUiAssetCacheEntry {
    uint64_t key;
    uint64_t last_touch_epoch;
    uint32_t resource_id;
    uint32_t state;
} AsmUiAssetCacheEntry;

typedef struct AsmUiAssetCacheStats {
    uint64_t hits;
    uint64_t misses;
    uint64_t loads;
    uint64_t evictions;
    uint64_t deferred;
    uint64_t failures;
    uint64_t pending_submits;
    uint64_t pending_completions;
} AsmUiAssetCacheStats;

typedef struct AsmUiAssetCache {
    AsmUiAssetCacheEntry *entries;
    uint32_t capacity;
    uint32_t resource_base;
    uint32_t protect_epochs;
    uint32_t load_budget;
    uint32_t loads_this_epoch;
    uint64_t epoch;
    AsmUiAssetCacheStats stats;
} AsmUiAssetCache;

/* The loader owns the physical payload associated with the chosen slot.
 * Return ASMUI_ASSET_LOADER_READY for synchronous completion,
 * ASMUI_ASSET_LOADER_PENDING when an asynchronous upload was submitted, or
 * ASMUI_ASSET_LOADER_FAILED on failure. Pending entries are never selected for
 * eviction and become visible only after asmui_asset_cache_complete(). */
typedef int (*AsmUiAssetLoadFn)(void *user,
                                uint32_t slot,
                                uint64_t key,
                                uint32_t resource_id,
                                uint64_t old_key,
                                int had_old);

void asmui_asset_cache_init(AsmUiAssetCache *c,
                            AsmUiAssetCacheEntry *entries,
                            uint32_t capacity,
                            uint32_t resource_base,
                            uint32_t protect_epochs,
                            uint32_t load_budget);
void asmui_asset_cache_begin_epoch(AsmUiAssetCache *c, uint64_t epoch);
int asmui_asset_cache_lookup(AsmUiAssetCache *c, uint64_t key,
                             uint32_t *resource_id, uint32_t *slot);
int asmui_asset_cache_request(AsmUiAssetCache *c, uint64_t key,
                              AsmUiAssetLoadFn loader, void *user,
                              uint32_t *resource_id, uint32_t *slot);
int asmui_asset_cache_complete(AsmUiAssetCache *c,
                               uint32_t slot,
                               uint64_t key,
                               int ok);
uint32_t asmui_asset_cache_resident(const AsmUiAssetCache *c);
uint32_t asmui_asset_cache_pending(const AsmUiAssetCache *c);

#ifdef __cplusplus
}
#endif
#endif
