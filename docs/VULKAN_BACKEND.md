# Vulkan backend contract (v0.5)

ASMUI does not own the window, surface, swapchain or frame scheduler. The host begins Vulkan 1.3 dynamic rendering and supplies a command buffer.

## Stream layout

Before upload, text placeholders should already be expanded to glyph commands. The persistent vertex buffer then contains visible draw quads followed by one stable quad per retained node:

```text
DrawCmd[] --ASM--> visible vertices
Node[]    --ASM--> stable node-mask vertices
```

This is designed for the Intel UMA path: the CPU writes the persistently mapped stream directly and the iGPU consumes it without a per-frame staging copy.

## Batch key

Adjacent commands merge only when these match bit-for-bit:

- draw type;
- texture/atlas `resource_id`;
- material id;
- scissor rectangle;
- soft-mask state;
- stencil nesting depth;
- nearest stencil-mask identity.

The last item is required because equal-depth sibling masks are not equivalent.

## Pipeline packs

`asmui_vk_create_pipeline_pack()` creates the non-stencil alpha/additive/multiply pipelines.

`asmui_vk_create_pipeline_pack_ex()` additionally takes a stencil attachment format and creates:

```text
pipeline_alpha
pipeline_additive
pipeline_multiply
pipeline_stencil_push
pipeline_stencil_pop
```

All pipelines declare the same dynamic-rendering color format. Stencil-enabled pipelines also declare `VkPipelineRenderingCreateInfo::stencilAttachmentFormat`.

## Dynamic stencil reference

Stencil reference is dynamic state. Before mask/content draws, the backend calls `vkCmdSetStencilReference()` for front and back faces. Push/pop use increment/decrement; content compares against the active depth.

## Host rendering requirement

When stencil is enabled, the host's `VkRenderingInfo` must contain a compatible `pStencilAttachment`, and that attachment must begin the UI pass cleared to zero.

The v0.5 runtime smoke uses `VK_FORMAT_S8_UINT` and `VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL`.

## Soft mask + stencil

The two systems are orthogonal:

```text
scissor -> stencil test -> fragment material/soft-mask -> blend
```

Stencil handles exact nested transformed geometry. Soft mask remains useful for feathered rounded edges.

## Capacity

The streaming buffer must hold:

```text
ctx->draw_count + ctx->node_count
```

quads. `asmui_vk_upload()` rejects the frame when this exceeds `backend->max_quads` rather than silently truncating mask geometry.
