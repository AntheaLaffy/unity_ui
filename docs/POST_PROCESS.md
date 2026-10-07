# ASMUI v0.8 post-process path

v0.8 extends the validated v0.7 full-resolution blur path into a practical rhythm-game UI glow path without introducing a full render graph yet.

## Stable baseline retained

`make vulkan-blur-smoke` remains the same conceptual baseline:

```text
full-resolution source
 -> blur H
 -> blur V
 -> copy composite
```

This keeps historical GPU timings comparable across future changes.

## Glow path

`make vulkan-glow-smoke` adds:

```text
full-resolution UI source
 -> bright-pass extraction + 2x downsample
 -> half-resolution blur H
 -> half-resolution blur V
 -> base source copied to final target
 -> blurred glow linearly upsampled + additive composite
```

The post vertex stage remains a fullscreen triangle generated from `gl_VertexIndex`; it does not allocate or upload extra UI vertices.

### Extraction

`ASMUI_POST_EXTRACT` uses the maximum RGB component as the bright-pass signal. `radius` is used as the extraction threshold and `strength` as extraction gain for this mode. The smoke test uses a bright red square that must glow and a dim green square that is deliberately below threshold and must not produce a halo.

### Additive pipeline

`AsmUiVkPostBackend` owns two compatible post pipelines:

- `pipeline`: blending disabled, used for copy/extract/blur;
- `additive_pipeline`: ONE + ONE additive blending, used to accumulate glow over the already copied base image.

The shader and descriptor layout are shared between the two pipelines.

## Why half resolution

For a separable 9-tap kernel, reducing both dimensions by two means the blur passes shade roughly one quarter as many fragments. Extraction also performs the downsample using the existing linear sampler. The final additive pass upsamples back to the full UI target.

This is intentionally the first optimization because Intel Raptor Lake-P is UMA: reducing sampled/shaded pixels is more valuable than adding another CPU-side geometry path.

## Correctness regression

The glow smoke verifies all of the following:

```text
bright center remains bright
bright source produces a visible halo
dim source below extraction threshold does not produce a halo
far background remains dark
exact half-resolution RT can be released and reused
```

A shader that merely copies the source, a broken threshold, a broken additive pipeline, or an RT-pool size mismatch should therefore fail independently.

## GPU timing regression

If the selected graphics queue exposes timestamps, the test prints:

```text
GPU glow timings (half-res blur):
extract=... us
blurH=... us
blurV=... us
composite=... us
total=... us
```

The v0.8 target-machine defaults are deliberately loose relative to the measured Raptor Lake-P baseline but no longer effectively infinite:

```text
ASMUI_GPU_BLUR_MAX_US default = 2000
ASMUI_GPU_GLOW_MAX_US default = 2000
ASMUI_PERF_MAX_NS_PER_DRAW default = 1000
```

Every value can be overridden through its environment variable.

Run both GPU baselines together with:

```bash
make perf-gpu
```

## Still deferred

Not implemented in v0.8:

- multi-level bloom pyramid;
- nested offscreen render graph;
- automatic per-layer effect scheduling;
- compute-shader blur;
- frame-overlapped target ownership/fences;
- temporal/post effects.

Those should only be added after the target GPU validates the half-resolution glow path and its timing behavior.
