# Offscreen layers (v0.6)

v0.6 adds the first deliberately small offscreen foundation.

## Authored state

`AsmUiNode.layer_flags` reuses the old reserved ABI word at offset 76. Mark a
node with `asmui_offscreen_layer(&node)` to make it the owner of descendant
visuals in the render-op planner.

The ASM draw emitter now writes `source_node` (node index + 1) into every draw
command. Expanded MSDF glyphs preserve this field, so text stays inside the
same layer as its source text node.

## Painter stream

`asmui_build_render_ops()` emits:

- `ASMUI_RENDER_DRAW_RANGE`
- `ASMUI_RENDER_BEGIN_LAYER`
- `ASMUI_RENDER_END_LAYER`
- `ASMUI_RENDER_COMPOSITE_LAYER`

Normal draws are kept in painter order. A layer subtree must be contiguous in
that order. If the same layer would need to be entered twice, planning fails
instead of silently producing an incorrect composite.

Nested offscreen layers are intentionally not promoted to a full render graph
in v0.6. That is the next architectural step.

## Render-target pool

`AsmUiVkTargetPool` owns up to 8 reusable device-local images. Exact
(width,height,format) matches are reused before a new target is allocated.
Targets use:

- COLOR_ATTACHMENT
- SAMPLED
- TRANSFER_SRC
- TRANSFER_DST

The caller still owns image-layout transitions and synchronization. This keeps
the pool a lifetime/resource mechanism rather than a hidden scheduler.

## Tests

- `make test`: CPU/ASM correctness including layer-op planning.
- `make perf-core`: 4096-node core throughput benchmark with a broad
  catastrophic-regression guardrail.
- `make vulkan-offscreen-smoke`: real GPU offscreen render -> sampled
  composite -> readback, plus target-pool reuse.
- `make verify`: all CPU, HarfBuzz, Vulkan, stencil, offscreen and performance
  checks.
