# Nested stencil compositor (v0.5)

## Problem

A single `stencil_depth` is insufficient for retained UI. Two sibling masks can overlap while both live at depth 1. If their identity is discarded, content from one sibling can be accepted by the other sibling's stencil region.

ASMUI therefore carries both:

```text
stencil_depth
nearest stencil-mask node index + 1
```

The renderer reconstructs the target ancestor mask chain from the retained nodes and compares it with the currently active chain.

## Painter-order transition

For a transition:

```text
current = [A, B]
target  = [A, C, D]
```

ASMUI records:

```text
POP  B at reference 2
PUSH C at reference 1
PUSH D at reference 2
DRAW at reference 3
```

A push pipeline uses:

```text
compareOp = EQUAL
passOp    = INCREMENT_AND_CLAMP
reference = current depth
```

A pop pipeline uses:

```text
compareOp = EQUAL
passOp    = DECREMENT_AND_CLAMP
reference = current depth
```

Content uses:

```text
compareOp = EQUAL
passOp    = KEEP
reference = target depth
```

This makes each nested region equal to its nesting depth and allows the renderer to unwind one sibling before entering another.

## Vertex stream

The persistent vertex buffer is laid out as:

```text
[ visual draw quad 0 ]
[ visual draw quad 1 ]
...
[ visual draw quad N-1 ]
[ node quad 0 ]
[ node quad 1 ]
...
[ node quad M-1 ]
```

The first section is produced by `asm_ui_build_gpu_vertices()`.
The second section is produced by `asm_ui_build_node_vertices()`.

A mask node's index therefore maps to:

```text
quad_index = ctx->draw_count + node_index
```

No mask geometry is reconstructed by the Vulkan backend.

## Conservative scissor

`asmui_stencil_mask()` also sets `ASMUI_CLIP`. Layout computes the transformed node AABB and intersects it with the inherited clip. This scissor is not the mask itself; it is only an early-rejection bound. The actual rotated quad is written to stencil.

That distinction is exactly what `make vulkan-stencil-smoke` tests.

## Current shape scope

v0.5 uses the node's transformed rectangle as stencil geometry. It supports arbitrary affine transforms and nesting, but it does not yet sample an image alpha channel when writing stencil. Alpha silhouettes/path masks can be layered on later without changing the ancestry model.
