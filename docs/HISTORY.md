旧版音游演示及渲染核心的开发记录。当前编辑器入口见 [README](../README.md)。

# ASMUI v0.20

**v0.20 focus: non-blocking Vulkan texture upload + pending asset state.**
The v0.19 fixed-size jacket cache no longer calls `vkQueueWaitIdle()` for each
streamed image. PNG decode is still CPU-side and frame-budgeted, but GPU copy
submission is asynchronous: cards stay on their fallback artwork while a cache
entry is `PENDING`, then publish the descriptor only after the upload fence
signals.

New in this version:

- `AsmUiVkTextureUpload`: per-upload staging buffer, command buffer and fence;
- `asmui_vk_texture_upload_rgba8_async_begin()` submits without queue-idle;
- `asmui_vk_texture_upload_async_poll()` retires staging resources and hands
  ownership of the completed `VkImage` to the cache;
- `ASMUI_ASSET_PENDING` plus explicit `asmui_asset_cache_complete()`;
- pending cache entries cannot be evicted or rendered prematurely;
- live jacket descriptors are updated only after their upload fence completes;
- startup font/single-jacket uploads keep the blocking convenience wrapper;
- `vulkan-texture-async-smoke` submits four uploads before polling completion;
- `make live-stream-bench` generates 16 deterministic PNG jackets and drives a
  100,000-song catalog aggressively enough to exercise LRU eviction.

Quick verification:

```bash
make clean && make verify
make live-stream-bench
```

The live streaming path deliberately retains synchronous **PNG decoding** for
this version; what is removed is the render-thread `vkQueueWaitIdle()` stall.
If real jacket data shows PNG I/O/decode dominating `CPU encode` p95, the next
step is a worker-thread decode queue rather than more Vulkan changes.

See `docs/ASSET_STREAMING.md`.

---

# ASMUI v0.19

**v0.19 focus: frame-budgeted jacket streaming + fixed-size LRU texture residency.**
The virtual 100k-song catalog no longer assumes all artwork is resident. Seven
retained cards can request logical jacket ids through a 12-slot cache whose
physical Vulkan resource ids stay fixed while PNG/VkImage payloads are recycled.

New in this version:

- backend-agnostic `AsmUiAssetCache` with LRU epochs, negative caching, fixed
  residency, per-frame load budget and statistics;
- GPU-safe recycle age tied to `frames_in_flight`;
- `asmui_vk_update_texture_set()` lets a physical cache slot reuse its existing
  descriptor set after the previous texture is no longer in flight;
- seven hidden mini-jacket retained nodes are populated only when assets become
  resident;
- selected-song large jacket can reuse the same streamed cache texture;
- `ASMUI_JACKET_DIR=/path` loads numbered `N.png` jackets on demand;
- `ASMUI_JACKET_REPEAT=N` optionally maps logical ids modulo N for stress tests;
- `ASMUI_ASSET_LOADS_PER_FRAME=N` caps synchronous loads per rendered frame;
- deterministic LRU correctness test plus a 100,000-key performance regression.

Quick verification:

```bash
make clean && make verify
ASMUI_VIRTUAL_SONGS=100000 \
ASMUI_PRESENT_MODE=mailbox \
ASMUI_FRAMES_IN_FLIGHT=3 \
make live-bench
```

Stream a small seven-jacket demo across a huge logical catalog:

```bash
ASMUI_JACKET_DIR=~/Music/jackets \
ASMUI_JACKET_REPEAT=7 \
ASMUI_VIRTUAL_SONGS=100000 \
ASMUI_PRESENT_MODE=mailbox \
ASMUI_FRAMES_IN_FLIGHT=3 \
make live-demo
```

Files are expected as `0.png` ... `6.png` for that example.

See `docs/ASSET_STREAMING.md`.

---

# ASMUI v0.18

**v0.18 focus: real ScrollRect viewport + virtualized large song catalogs.**
The live text carousel now owns a clipped retained viewport and recycles seven
physical card slots across an arbitrary logical song count. The default live
demo exposes 256 logical songs; `ASMUI_VIRTUAL_SONGS` can raise that up to one
million without allocating more UI cards.

New in this version:

- `AsmUiVirtualCarousel`: logical count is independent from retained slot count;
- O(slot-count) slot mapping, hit testing, dragging, wheel input and snapping;
- the Song Carousel is parented to an `ASMUI_CLIP` viewport (`1080x104`);
- recycled slots rebind title/accent metadata without allocating nodes;
- selected-song details bind from the logical index while the demo metadata
  intentionally repeats every seven entries;
- `tests/virtual_list.c` checks head/middle/tail mapping plus inherited viewport clipping;
- `make perf-virtual` runs a **100,000-song** logical catalog while asserting
  constant nodes/draws/batches and CPU p95;
- live demo: `ASMUI_VIRTUAL_SONGS=N` controls the logical catalog size.

Quick verification:

```bash
make clean && make verify
ASMUI_PRESENT_MODE=mailbox ASMUI_FRAMES_IN_FLIGHT=3 ASMUI_VIRTUAL_SONGS=100000 make live-bench
```

Interactive demo:

```bash
ASMUI_PRESENT_MODE=mailbox \
ASMUI_FRAMES_IN_FLIGHT=3 \
ASMUI_VIRTUAL_SONGS=100000 \
make live-demo
```

The bundled demo only has seven metadata records and repeats them across the
logical catalog; the virtualization layer itself is catalog-agnostic.

See `docs/VIRTUAL_SCROLLRECT.md`.

---

# ASMUI v0.17

**v0.17 focus: mouse/touch-style ScrollRect interaction.** The retained seven-song carousel now supports pointer coordinate conversion, scaled-card hit testing, click-to-select, direct drag, release snap/flick, and mouse-wheel selection without adding SDL to the interaction core.

New in this version:

- `asmui_pointer_to_canvas()` converts SDL/window coordinates to ASMUI authoring coordinates;
- `asmui_carousel_hit_test()` respects animated card scale;
- pointer begin/move/end implements direct ScrollRect drag and snap;
- wheel input advances/reverses the song selection, including SDL natural-scroll correction in the live demo;
- click/drag/wheel selection uses the existing allocation-free song-detail binding;
- `make perf-pointer` simulates 2400 interactive frames and guards selection, batches and CPU p95.

Quick verification:

```bash
make clean && make verify
ASMUI_PRESENT_MODE=mailbox ASMUI_FRAMES_IN_FLIGHT=3 make live-bench
```

Interactive demo:

```bash
ASMUI_PRESENT_MODE=mailbox ASMUI_FRAMES_IN_FLIGHT=3 make live-demo
```

Controls:

```text
LEFT / RIGHT        select song
mouse click          select a visible song card
mouse drag           ScrollRect-style carousel drag + release snap
mouse wheel          previous / next song
UP / DOWN            navigation selection
ESC                  quit
```

Target the new path:

```bash
make perf-pointer
ASMUI_POINTER_CPU_P95_US=100 make perf-pointer
```

See `docs/POINTER_SCROLLRECT.md`.

---

# ASMUI v0.16

**v0.16 focus: selected-song data binding.** The Song Carousel is now connected to the detail panel instead of moving independently. Every selection updates the retained title, artist, subtitle, difficulty, level, score, combo, gauge fill, difficulty badge color, and jacket accent color. The live benchmark keeps auto-switching songs, so this binding path is exercised continuously.

New in this version:

- seven-entry `AsmUiSongMeta` catalog;
- retained node handles for all dynamic song-detail fields;
- `asmui_song_select_bind()` performs allocation-free pointer/color/value updates;
- the runtime SDF glyph sample set now contains every dynamic metadata string, so switching songs does not require rebuilding the atlas;
- live LEFT/RIGHT selection immediately updates the detail pane while the carousel continues its spring transition;
- `tests/song_binding.c` exhaustively checks all seven selections plus out-of-range clamping;
- the 2400-frame carousel perf regression now includes real metadata rebinding.

Quick verification:

```bash
make clean && make verify
ASMUI_PRESENT_MODE=mailbox ASMUI_FRAMES_IN_FLIGHT=3 make live-bench
```

Interactive demo:

```bash
ASMUI_PRESENT_MODE=mailbox ASMUI_FRAMES_IN_FLIGHT=3 make live-demo
```

Controls:

```text
LEFT / RIGHT   select song + update details
UP / DOWN      navigation selection
ESC            quit
```

Target just the new correctness/perf paths:

```bash
make test
make perf-carousel
```

---

# ASMUI v0.15

**v0.15 focus: a real retained Song Carousel.** The live Song Select now contains seven retained song cards driven by a reusable `AsmUiCarousel` state. Selection moves with critically damped snap motion; the selected card scales/fades forward while neighbors recede. LEFT/RIGHT selects songs, UP/DOWN keeps the five-entry navigation selector.

New in this version:

- generic `AsmUiCarousel` built on top of `AsmUiSnapScroll`;
- per-item x/y/scale/alpha/emphasis computation;
- seven retained song cards with real SDF/HarfBuzz titles (`AIR`, `夏影`, `NEON`, `鳥の詩`, `ORBIT`, `LUMINA`, `ASTER`);
- card children inherit the root transform/alpha, so an entire card transitions as one object;
- live benchmark intentionally nudges the carousel every 120 frames, so perf numbers exercise movement instead of 600 static frames;
- `make perf-carousel`: 2400-frame interaction/performance regression with batch and p95 guardrails;
- v0.14 image contain/cover, v0.13 PNG assets, v0.12 real text, and the 3-frame MAILBOX renderer remain unchanged.

Quick verification:

```bash
make clean && make verify
ASMUI_PRESENT_MODE=mailbox ASMUI_FRAMES_IN_FLIGHT=3 make live-bench
```

Interactive demo:

```bash
ASMUI_PRESENT_MODE=mailbox ASMUI_FRAMES_IN_FLIGHT=3 make live-demo
```

Controls:

```text
LEFT / RIGHT   select song
UP / DOWN      navigation selection
ESC            quit
```

Target only the new regression:

```bash
make perf-carousel
ASMUI_CAROUSEL_CPU_P95_US=250 make perf-carousel
```

---

# ASMUI v0.14

**v0.14 focus: asset placement + reusable interaction primitives.** The v0.13 PNG jacket path now supports aspect-preserving contain/cover instead of assuming square artwork, and the live demo uses a critically damped snap-scroll state for navigation selection. These are framework primitives rather than one-off demo math.

New in this version:

- `asmui_image_place()` with `STRETCH`, `CONTAIN`, and centered `COVER`;
- `asmui_apply_image_placement()` updates geometry and UV crop together;
- `ASMUI_JACKET_MODE=cover|contain|stretch` (default: `cover`);
- generic `AsmUiSnapScroll` with clamped target, critical damping and dt substepping;
- live LEFT/RIGHT navigation now snaps smoothly across five entries;
- deterministic regression checks 16:9 -> square contain/cover math and spring convergence;
- v0.13 real SDF/HarfBuzz text, PNG upload, v0.11 3-frame MAILBOX and GPU post path remain intact.

Quick verification:

```bash
make clean && make verify
ASMUI_PRESENT_MODE=mailbox ASMUI_FRAMES_IN_FLIGHT=3 make live-bench
```

Use a non-square jacket to exercise the new path:

```bash
ASMUI_JACKET=~/Pictures/wide-jacket.png \
ASMUI_JACKET_MODE=cover \
ASMUI_PRESENT_MODE=mailbox \
ASMUI_FRAMES_IN_FLIGHT=3 \
make live-demo
```

Switch `cover` to `contain` to preserve the complete image. The live startup log prints the final draw size and UV crop, making image-placement regressions visible without a GPU debugger.

See `docs/INTERACTION_AND_ASPECT.md`.

---

# ASMUI v0.13

**v0.13 focus: real visual assets.** The live Song Select can now load an external PNG jacket through libpng, upload it once as a Vulkan RGBA8 texture, and feed it into the same retained image/batch path used by the rest of ASMUI. The external image node remains invisible when no asset is supplied, so all deterministic regressions still work without files.

New in this version:

- `ASMUI_JACKET=/path/to/jacket.png` live asset override;
- libpng RGBA8 decoder (`backend/image_png.c`);
- Vulkan resource id 2 reserved by the demo for jacket art;
- exact 2x2 PNG decode regression (`make png-check`);
- real-text scene now has one retained optional jacket-image node;
- v0.11 3-frame MAILBOX path and v0.12 FreeType/HarfBuzz text stay unchanged.

Arch dependencies:

```bash
sudo pacman -S libpng freetype2 harfbuzz
```

Example:

```bash
ASMUI_JACKET=~/Pictures/jacket.png \
ASMUI_PRESENT_MODE=mailbox \
ASMUI_FRAMES_IN_FLIGHT=3 \
make live-demo
```

See `docs/ASSETS.md`.

---

# ASMUI v0.12

**v0.12 focus: real text instead of pixel-font quads.** The live Song Select now
builds a small runtime SDF atlas with FreeType, shapes UTF-8 with HarfBuzz, expands
glyph geometry through the existing ASM text path, uploads the atlas once to Vulkan,
and keeps the v0.11 3-frame ring/present pipeline. If no usable font is found it
falls back to the deterministic 5x7 scene.

Key additions:

- runtime FreeType SDF atlas containing only glyphs used by the scene;
- HarfBuzz shaping for UTF-8, including a Japanese `鳥の詩` integration label;
- generic one-time RGBA8 Vulkan texture upload helper;
- live demo uses retained `ASMUI_NODE_TEXT` nodes, then ASM glyph expansion/batching;
- `font-sdf-check` writes `build/font_sdf_atlas.ppm`;
- `perf-text` guards node/draw/batch counts and text hot-path performance;
- `ASMUI_FONT=/path/to/font.ttf` (or `.otf/.ttc`) overrides font discovery.

On Arch:

```bash
sudo pacman -S freetype2 harfbuzz
make clean && make verify
ASMUI_PRESENT_MODE=mailbox ASMUI_FRAMES_IN_FLIGHT=3 make live-demo
```

Targeted checks:

```bash
make font-sdf-check
make perf-text
xdg-open build/font_sdf_atlas.ppm
```

The old 5x7 scene stays in the regression suite on purpose: it remains a deterministic
worst-ish draw-count baseline while the live path now measures the architecture we
actually want for a Unity-style rhythm-game UI.

---

# ASMUI v0.11

**v0.11 focus:** correctness + WSI pipelining. It fixes top-left scissor handling,
adds a 1..3 frame persistent vertex ring, per-frame offscreen targets, per-swapchain-image
render-finished semaphores, and split live CPU timing.

Quick validation:

```bash
make clean && make verify
make live-bench-compare
ASMUI_PRESENT_MODE=mailbox make live-bench-compare
```

See `docs/LIVE_PIPELINE.md`.

# ASMUI v0.9

v0.9 shifts from isolated renderer primitives to an integrated rhythm-game UI regression.

## New integration workload

- deterministic 1280x720 asset-free Song Select scene;
- 5x7 procedural display font built from ordinary ASMUI image nodes;
- rounded cards, jacket composition, difficulty badge and filled progress gauge;
- realistic CPU scene profile with batch-count guardrail;
- headless Vulkan Song Select renderer;
- existing half-resolution Glow applied to the complete scene;
- full-frame GPU timestamps: base / extract / blur-H / blur-V / composite;
- writes `build/song_select.ppm` for visual inspection;
- readback anchors catch black-frame, missing-CTA and missing-glow regressions.

Run the normal suite first:

```bash
make clean && make verify
```

Then render/profile the integrated visual workload:

```bash
make song-select-demo
```

Optional tighter local guards:

```bash
ASMUI_SCENE_MAX_NS_PER_DRAW=500 make perf-scene
ASMUI_GPU_SONG_MAX_US=2500 make song-select-demo
```

See `docs/SONG_SELECT_DEMO.md`.

---

# ASMUI v0.8

ASM-first retained-mode UI runtime aimed at reproducing Unity-style rhythm-game UI.

## v0.8 additions

- bright-pass extraction (`ASMUI_POST_EXTRACT`);
- dedicated additive post pipeline for glow compositing;
- 2x downsampled glow path (`256x256 -> 128x128 blur -> 256x256 additive upsample` in the smoke test);
- threshold regression: a bright source must glow while a dim source below threshold must not;
- mixed-size render-target-pool reuse regression;
- GPU timestamps split into extract / blur-H / blur-V / composite;
- paired GPU performance targets: `make perf-gpu` runs the v0.7 full-res blur baseline and the v0.8 half-res glow path;
- guardrails tightened around the measured Raptor Lake-P baseline while remaining environment-overridable.

The full-resolution blur test is intentionally retained unchanged as a stable historical baseline. The new glow path does not replace it.

## Verification

```bash
make clean && make verify
```

Target only the new path:

```bash
make vulkan-glow-smoke
```

Compare both GPU post paths:

```bash
make perf-gpu
```

Default catastrophic-regression guardrails are now intentionally tighter for the target machine:

```text
CPU core median       <= 1000 ns/draw
full-res blur total   <= 2000 us
half-res glow total   <= 2000 us
```

They can still be overridden locally:

```bash
ASMUI_PERF_MAX_NS_PER_DRAW=500 make perf-core
ASMUI_GPU_BLUR_MAX_US=1000 make vulkan-blur-smoke
ASMUI_GPU_GLOW_MAX_US=1000 make vulkan-glow-smoke
```

See `docs/POST_PROCESS.md`.

---

# ASMUI v0.7

ASM-first retained-mode UI runtime aimed at reproducing Unity-style rhythm-game UI.

## v0.7 additions

- fullscreen-triangle Vulkan post-process pipeline (no extra post vertex stream);
- separable 9-tap Gaussian blur with RT ping-pong;
- final sampled composite path;
- GPU timestamp queries for blur-H / blur-V / composite;
- blur correctness readback (center + halo + far background);
- GPU catastrophic-regression guardrail via `ASMUI_GPU_BLUR_MAX_US`;
- CPU hot-path benchmark upgraded to 9-sample median/min/max reporting.

v0.7 deliberately stops at **blur**. Glow/bloom will build on this path only after the target Intel Raptor Lake-P machine validates the new synchronization, ping-pong and timestamp-query path.

## Full verification

```bash
make clean && make verify
```

Targeted post-process validation:

```bash
make vulkan-blur-smoke
```

Local performance thresholds can be tightened without changing portable defaults:

```bash
ASMUI_PERF_MAX_NS_PER_DRAW=500 make perf-core
ASMUI_GPU_BLUR_MAX_US=3000 make vulkan-blur-smoke
```

See `docs/POST_PROCESS.md`.

---

# ASMUI v0.6

ASM-first retained-mode UI core aimed at reproducing Unity-style rhythm-game UI.

## v0.6 additions

- offscreen-layer authoring via `asmui_offscreen_layer()`
- ASM draw commands retain their `source_node` identity
- painter-order `AsmUiRenderOp` planning (`BEGIN/END/COMPOSITE_LAYER`)
- reusable Vulkan render-target pool
- real Vulkan offscreen -> sampled composite smoke test
- CPU hot-path performance regression guardrail
- one-command full verification: `make verify`

v0.6 deliberately supports contiguous sibling offscreen layers first. Nested
offscreen layers will be enabled with the later render-graph step rather than
being implemented with ambiguous implicit behavior.

## Quick verification

```bash
make clean
make verify
```

For local performance tracking:

```bash
make perf-core
ASMUI_PERF_MAX_NS_PER_DRAW=500 make perf-core
```

See `docs/OFFSCREEN.md` for the v0.6 layer/target-pool design.

---

# ASMUI v0.5 — Unity-style rhythm-game UI runtime in x86-64 assembly

ASMUI is a retained-mode UI runtime for reproducing the visual structure and rendering behavior of Unity-style rhythm-game interfaces without depending on Unity.

The core is **x86-64 System V assembly** (GNU assembler, Intel syntax). C remains a thin boundary around Vulkan/SDL3 and Unicode shaping.

## v0.5 milestone: true nested stencil masks

v0.5 keeps every v0.4 feature and adds a real stencil path instead of approximating arbitrary clipping with a rounded soft-mask rectangle.

The core now provides:

- Unity-like `RectTransform` anchors / pivot / anchored position / size delta;
- parent-child affine transforms, rotation, scale and inherited alpha;
- hierarchical scissor clipping;
- simple / 9-sliced / horizontal-filled / vertical-filled images;
- tween tracks for position, scale, rotation, alpha, size and fill;
- painter-order-safe adjacent batching;
- GPU vertex packing in x86-64 ASM;
- HarfBuzz/MSDF text expansion path;
- rounded soft masks;
- **stencil ancestry resolved per visual draw in ASM;**
- **stencil depth + nearest-mask identity included in the batch key;**
- **one stable ASM-generated GPU quad per node for mask replay;**
- **Vulkan content / stencil-push / stencil-pop pipelines;**
- **nested stencil transitions that preserve painter order without global sorting.**

A stencil mask affects descendants, not the mask node's own visual, matching the retained hierarchy model used by the soft-mask path.

## Why v0.5 stops before blur

Stencil changes the render-state model and the persistent vertex-stream layout. It is intentionally validated before offscreen render targets are introduced. This gives a clean failure boundary on the target Intel Raptor Lake-P UHD machine.

The next render-graph step starts only after the real stencil runtime smoke passes.

## Target machine

```text
Intel Corporation Raptor Lake-P [UHD Graphics]
Kernel driver in use: i915
Kernel modules: i915, xe
```

Runtime path:

```text
ASMUI -> Vulkan -> Mesa ANV / vulkan-intel -> i915 -> Intel UHD
```

Dynamic geometry stays in a persistently mapped host-visible stream buffer. v0.5 appends one node quad per retained node after the visible draw quads, so a stencil mask can be replayed by stable node index without reconstructing geometry in C.

## Build

CPU/ASM regression suite:

```bash
make test
```

Arch Vulkan toolchain:

```bash
sudo pacman -S vulkan-intel vulkan-tools vulkan-headers shaderc
make vulkan-check
```

HarfBuzz adapter:

```bash
sudo pacman -S harfbuzz freetype2
make text-hb-check
```

Baseline GPU smoke from v0.4.1:

```bash
make vulkan-runtime-smoke
```

New v0.5 stencil smoke:

```bash
make vulkan-stencil-smoke
```

The stencil test is headless/offscreen. It renders a red image under a 120×120 square rotated 45 degrees. The conservative scissor is the square's larger world-space AABB, while the true stencil shape is the diamond. The test verifies:

```text
center pixel                  -> red
pixel inside AABB/outside diamond -> black
```

If both conditions hold, the result cannot be explained by scissor clipping alone.

## Frame flow

```c
asm_ui_begin_frame(&ui, dt);
asm_ui_tick_tweens(&ui);
asm_ui_layout(&ui);
asm_ui_build_drawlist(&ui);
```

Text placeholders can then be expanded to glyph draw commands exactly as in v0.4. Batching now also carries stencil ancestry:

```c
AsmUiBatch batches[4096];
uint32_t batch_count = asm_ui_build_batches(&render_ui, batches, 4096);
asmui_vk_upload(&render_ui, &vkui);
asmui_vk_record_images(cmd, &render_ui, batches, batch_count,
                       &vkui, framebuffer_w, framebuffer_h);
```

When a stencil-enabled pipeline pack is used, `asmui_vk_record_images()` compares the current and target stencil-mask chains and emits only the necessary transitions:

```text
current: RootMask -> CardMask
new:     RootMask -> PopupMask

POP  CardMask   (stencil 2 -> 1)
PUSH PopupMask  (stencil 1 -> 2)
DRAW content    (compare == 2)
```

No UI draw is reordered.

## Creating a stencil mask

```c
nodes[mask] = asmui_node(ASMUI_NODE_CONTAINER, parent, 0xffffffffu);
nodes[mask].size_delta_x = 420.0f;
nodes[mask].size_delta_y = 180.0f;
nodes[mask].rotation = 0.12f;
asmui_stencil_mask(&nodes[mask]);

nodes[jacket].parent = mask;
nodes[title].parent = mask;
```

`asmui_stencil_mask()` also keeps a conservative AABB scissor for early rejection. Vulkan stencil performs the exact transformed-quad clipping inside that AABB.

## Vulkan setup

Old non-stencil callers continue to use:

```c
asmui_vk_create_pipeline_pack(&vkui, color_format,
                              vs, vs_bytes, fs, fs_bytes);
```

Stencil-enabled callers use:

```c
asmui_vk_create_pipeline_pack_ex(&vkui,
                                 color_format,
                                 VK_FORMAT_S8_UINT,
                                 vs, vs_bytes,
                                 fs, fs_bytes);
```

The host must begin dynamic rendering with a compatible stencil attachment and clear stencil to zero. v0.5 uses dynamic `vkCmdSetStencilReference()` with three pipeline behaviors:

```text
content : compare EQUAL, keep
push    : compare EQUAL, increment-and-clamp
pop     : compare EQUAL, decrement-and-clamp
```

## What v0.5 stencil does and does not do

Implemented now:

- rotated/scaled rectangular stencil geometry;
- nested stencil masks;
- sibling mask switching without stencil leakage;
- soft mask + stencil mask on the same content;
- stable node geometry generated by ASM;
- painter-order-preserving transitions.

Not yet implemented:

- alpha-texture / arbitrary silhouette mask;
- MSDF/path-defined mask geometry;
- offscreen layers;
- Gaussian blur / bloom;
- render-target pooling.

Those belong to the next render-graph increment rather than being hidden behind a fake "complete v0.5" label.

## Tests

`make test` verifies:

- RectTransform hierarchy / rotation / tween smoke path;
- 9-slice atlas math;
- filled-image geometry + UV crop;
- fill tweening;
- painter-order batch boundaries;
- visible GPU vertex packing;
- soft-mask inheritance;
- shaped glyph -> ASM world quad expansion;
- **nested stencil ancestry;**
- **stencil-aware batch splitting;**
- **stable ASM node-quad packing.**

## Tree

```text
include/asmui.h
include/asmui_builder.h
include/asmui_text.h
include/asmui_text_hb.h
include/asmui_sdl3.h
include/asmui_vulkan.h
src/asmui.S
backend/text_stream.c
backend/text_harfbuzz.c
backend/sdl3_backend.c
backend/vulkan_backend.c
shaders/ui.vert
shaders/ui.frag
tests/stencil_core.c
tests/vulkan_stencil.c
tests/
docs/TEXT_AND_MASKS.md
docs/VULKAN_BACKEND.md
docs/STENCIL.md
docs/INTEL_RAPTOR_LAKE.md
```

## Next increment

After `make vulkan-stencil-smoke` passes on the Raptor Lake-P target, the next practical step is an **offscreen layer + render-target pool**. Blur/glow comes after that, because those effects need a reliable layer lifetime and image-layout transition model first.

## Asset/legal note

ASMUI reproduces layout/rendering behavior. Use artwork, fonts, music and other assets you created or have permission to use rather than redistributing extracted proprietary assets.

## v0.9.1 coordinate regression fix

Vulkan UI geometry now follows ASMUI's documented top-left/+Y-down screen
coordinates correctly.  The vertex shader previously used an OpenGL-style Y
inversion on top of Vulkan's positive-height viewport transform, mirroring the
whole frame vertically.  Centered smoke scenes hid the bug.

`vulkan-runtime-smoke` now renders asymmetric green-top / blue-bottom markers
and readbacks both anchors so this class of regression cannot pass unnoticed.
The Song Select visual anchors therefore test the intended on-screen positions.

## v0.10 realtime milestone

The static Song Select integration test now has a realtime counterpart. `make live-demo`
uses SDL3 only for the native window/events/Vulkan surface; swapchain and rendering are
owned by Vulkan. The scene animates a filled gauge, PLAY pulse, jacket accent rotation,
and navigation highlight while the same ASM hot paths rebuild layout, painter batches,
and GPU vertices every frame.

`make perf-realtime` is part of `make verify` and runs 1200 simulated 120 Hz frames
without opening a window. It guards p95 and catastrophic p99 CPU cost (`ASMUI_REALTIME_CPU_P95_US`, default 1000 us; `ASMUI_REALTIME_CPU_P99_US`, default 5000 us). `make live-bench` opens the real Vulkan window for 600 frames and reports
CPU submit-path percentiles plus average GPU pass timings.

## v0.11 live synchronization notes

Real Raptor Lake-P measurements from v0.10 showed that rendering itself was not the
limit: the full UI + glow was only a few milliseconds, while the single-frame FIFO
loop lost substantial wall throughput. v0.11 therefore focuses on synchronization
rather than adding another visual effect.

- `ASMUI_FRAMES_IN_FLIGHT=1..3` selects the stream depth (default 3).
- the persistent vertex buffer is segmented per frame;
- offscreen source/extract/ping/pong targets are also per frame;
- render-finished semaphores are per swapchain image;
- scissor rectangles now use the same top-left/+Y-down convention as geometry;
- `vulkan-runtime-smoke` includes an asymmetric clip sample and uploads from ring slot 2;
- live compilation treats misleading indentation and unused functions as errors.

Benchmark the WSI pipeline with:

```bash
make live-bench-compare
ASMUI_PRESENT_MODE=mailbox make live-bench-compare
```

The live output separates fence wait, image acquire, CPU encode, submit/present and
total-loop percentiles so the next optimization can be based on the actual stall.
