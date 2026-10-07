# v0.9 Song Select integration scene

v0.9 changes priority from adding isolated rendering features to validating the
existing renderer as one rhythm-game-style frame.

The scene is deliberately asset-free and deterministic. A 1x1 white texture is
tinted by ASMUI vertex colors; rounded cards, a filled progress bar and a small
5x7 procedural display font build a 1280x720 song-select composition. This
keeps the regression independent of external PNG/font files while exercising a
realistic number of retained nodes and draw calls.

## CPU regression

```bash
make perf-scene
```

Checks:

- scene builds at a stable non-trivial size;
- complete `layout -> drawlist -> batch -> GPU vertex pack` hot path;
- batch count must remain <= 96;
- default catastrophic threshold is 1000 ns/draw;
- override with `ASMUI_SCENE_MAX_NS_PER_DRAW`.

## GPU / visual integration

```bash
make song-select-demo
```

The headless Vulkan demo renders:

```text
retained scene
 -> full-resolution offscreen source
 -> half-resolution bright extraction
 -> horizontal blur
 -> vertical blur
 -> additive upsample/composite
 -> readback
 -> build/song_select.ppm
```

It reports retained node/draw/batch counts and GPU timestamps for the base UI
pass plus every post-process stage. Readback anchors protect against a fully
black frame, lost PLAY CTA, missing glow and gross background contamination.

The default integrated GPU guardrail is 5000 us and can be overridden with:

```bash
ASMUI_GPU_SONG_MAX_US=2500 make song-select-demo
```

The existing v0.7 full-resolution blur and v0.8 half-resolution glow tests stay
unchanged, so historical microbenchmarks remain comparable.


### v0.9.1 visual coordinate fix

The initial v0.9 integration test exposed a vertically mirrored Vulkan UI.
The root cause was an OpenGL-style Y inversion in `ui.vert` even though ASMUI
uses top-left/+Y-down coordinates and the Vulkan viewport uses positive height.
The shader is fixed and the basic runtime smoke now contains asymmetric
top/bottom markers to protect the convention independently of this demo.

## v0.10 realtime window

`make live-demo` opens an SDL3 Vulkan window while keeping Vulkan responsible for
swapchain/present. The retained scene is animated every frame, rebuilt through the
ASM layout/draw/batch/vertex path, rendered to an offscreen base target, then sent
through the existing half-resolution glow chain before presentation.

Controls:

- `Esc`: quit
- `Left/Right`: nudge the highlighted navigation tile
- `ASMUI_PRESENT_MODE=mailbox make live-demo`: request mailbox when the surface supports it
- `make live-bench`: run exactly 600 presented frames and print CPU p50/p95/p99 plus average GPU pass times

v0.10 intentionally used one frame in flight to establish a simple ownership baseline.
Real Raptor Lake-P measurements showed that MAILBOX could exceed 300 FPS while FIFO
was much slower, so v0.11 replaces the serialized stream with a configurable 1..3-frame
ring. Each frame slot owns its command buffer, fence, acquire semaphore, timestamp pool
and offscreen targets; render-finished semaphores are owned per swapchain image. Use
`make live-bench-compare` to compare one and three frames in flight under the same
present mode.
