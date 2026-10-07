# Asset streaming / asynchronous GPU residency (v0.20)

v0.20 keeps the v0.19 fixed-size LRU model but separates **request**, **GPU
upload**, and **publication**. A cache miss may now enter `ASMUI_ASSET_PENDING`:
that resource id is not used by retained cards until the upload fence signals.

The live path is:

```text
logical jacket id
  -> fixed LRU slot
  -> PNG decode (CPU, frame-budgeted)
  -> staging buffer + VkImage
  -> vkQueueSubmit(..., uploadFence)
  -> render loop continues (no vkQueueWaitIdle)
  -> poll fence on later frame
  -> update fixed descriptor set
  -> asmui_asset_cache_complete(... READY)
  -> visible card may use resource id
```

Pending entries are not eviction candidates. Existing READY/FAILED entries are
still protected for `frames_in_flight` epochs before recycling, so descriptor
sets and old VkImages are not mutated while older frames can reference them.

`asmui_vk_texture_upload_rgba8()` remains as a blocking wrapper for startup
resources such as the SDF atlas. Streaming jackets use the new async API.

## Stress path

`make live-stream-bench` generates 16 deterministic 256x256 PNG files and runs
a 1200-frame MAILBOX/3-frame test over 100,000 logical songs. Bench mode moves
forward every 30 frames so it forces misses, async completions and LRU eviction
instead of bouncing between the same two items.

The end-of-run stats report resident/pending entries plus async submit/complete
counts. `vulkan-texture-async-smoke` separately submits four GPU uploads without
calling queue-idle, then polls their fences.

## Intentional remaining boundary

PNG file I/O and libpng decode are still synchronous in v0.20. They are capped
by `ASMUI_ASSET_LOADS_PER_FRAME`; the expensive global GPU queue wait is gone.
If measured `CPU encode` latency is dominated by decoding, move decode to worker
threads next. Do not add a transfer-thread/queue until measurements justify it.

---

## v0.19 design notes
v0.19 separates the logical song catalog from jacket texture residency. A
100,000-song virtual list can keep only a small fixed cache of decoded/uploaded
artwork while the seven retained card slots continue to recycle normally.

## Generic cache

`AsmUiAssetCache` is backend-agnostic. Each physical cache slot owns a stable
`resource_id`; the application owns the payload attached to that slot. The
cache tracks:

- logical key -> physical slot mapping;
- LRU touch epoch;
- a protection age so resources referenced by in-flight GPU frames are not
  recycled too early;
- per-frame load budget;
- negative-cache entries for missing/broken assets;
- hit/miss/load/eviction/deferred/failure counters.

The lookup path is O(cache capacity), not O(logical catalog size). v0.19 uses a
12-entry demo cache and a seven-card visible window.

## Live Vulkan jacket stream

Set:

```bash
ASMUI_JACKET_DIR=/path/to/jackets
```

The live demo looks for numbered PNGs:

```text
/path/to/jackets/0.png
/path/to/jackets/1.png
/path/to/jackets/2.png
...
```

Optional knobs:

```text
ASMUI_ASSET_LOADS_PER_FRAME=1   # default 1, range 1..8
ASMUI_JACKET_REPEAT=7           # map logical id N to N % 7 on disk
```

The repeat mode is useful for stress-testing a 100k logical catalog with only a
small demo asset set.

Physical cache resource ids are currently 16..27. When a cache slot is safely
old enough to recycle, its Vulkan image is replaced and the already allocated
descriptor set is updated in place.

The selected song also reuses the resident cache texture for the large jacket,
while each of the seven retained carousel cards has a hidden mini-jacket node
that becomes visible only when its asset is resident.

## Safety with frames in flight

The cache protection age defaults to the live `frames_in_flight` count. A slot
is not recycled until it has gone untouched for that many frame epochs. The
current PNG upload helper is intentionally synchronous (`vkQueueWaitIdle`) and
therefore conservative; future work can replace it with an asynchronous upload
queue without changing the cache API.

## Tests

```bash
make asset-cache-check
make perf-assets
```

The performance regression drives a 100,000-key logical catalog through a
12-slot cache and asserts bounded residency, real evictions, and a CPU p95
threshold. Override the guardrail with:

```bash
ASMUI_ASSET_CPU_P95_US=50 make perf-assets
```
