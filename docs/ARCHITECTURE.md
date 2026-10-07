# ASMUI architecture (v0.21)

The primary demo is the Unity-style editor. See [editor boundaries](EDITOR_UI.md)
and [localization](LOCALIZATION.md) for its state, projection and font pipeline.

```text
AsmUiNode tree
      |
      | ASM: tween / layout / soft-mask resolve / draw-list
      v
AsmUiDrawCmd painter stream
      |
      +-- image commands ------------------------------------+
      |                                                     |
      +-- text placeholder -> HarfBuzz -> GlyphQuad[]       |
                               |                             |
                               +-> ASM text expansion -------+
                                                             v
                                                 expanded DrawCmd stream
                                                             |
                                  +--------------------------+------------------+
                                  |                                             |
                          ASM adjacent batching                         ASM vertex packing
                                  |                                             |
                                  +----------------------+----------------------+
                                                         v
                                                 Vulkan/other backend
```

## ABI ownership

The public structs in `include/asmui.h` are the assembly ABI. Field offsets are mirrored in `src/asmui.S` and guarded by C `_Static_assert`s.

## Rendering order

Painter order is semantic. ASMUI does not globally sort by material or texture. Only adjacent commands with identical state are merged. Soft-mask state is part of the batch key.

Node coordinates are authored with +Y upwards. The root layout matrix maps them
to +Y-down framebuffer coordinates; children, text expansion and clipping inherit
that mapping. Backends consume screen coordinates directly, without another flip.

## Text boundary

HarfBuzz performs Unicode/OpenType shaping. An atlas lookup maps glyph IDs to MSDF plane bounds and UV pages. ASM performs the final glyph geometry transform, preserving the text node's world affine transform and UI state.

## Mask boundary

Scissor clipping remains a layout/core feature. Rounded soft masks are resolved through the node hierarchy and passed to the fragment shader. Nested stencil, offscreen layers, blur and glow remain covered by the rendering regression targets.
