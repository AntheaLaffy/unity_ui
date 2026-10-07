# Text and masks in ASMUI v0.5

## Why shaping is not handwritten ASM

UTF-8 bytes do not map one-to-one to visible glyphs. OpenType shaping can reorder, substitute, combine and position glyphs. ASMUI therefore treats HarfBuzz as a narrow preprocessing library rather than attempting to duplicate Unicode/OpenType logic.

The assembly boundary starts after shaping: `AsmUiGlyphQuad` contains a local rectangle, atlas UVs and atlas texture page. `asm_ui_expand_text_draw()` maps every local glyph rectangle through the already-computed text quad basis and emits ordinary world-space draw commands.

This preserves rotation, scale, alpha, clipping, soft-mask state and painter order.

## Atlas metric convention

`AsmUiHbAtlasGlyph` uses em-relative plane bounds:

```text
plane_left / plane_bottom / plane_right / plane_top
```

These are multiplied by the text node's `font_size`. This convention maps naturally to pre-generated MSDF atlas metadata. UVs are normalized `[0,1]`; `texture_id` selects the atlas page.

## Painter-order expansion

Given:

```text
background image
song title text
foreground shine image
```

`asmui_expand_text_stream()` produces:

```text
background image
  glyph 0
  glyph 1
  glyph 2
  ...
foreground shine image
```

No global text pass is introduced.

## Soft mask semantics

`asmui_soft_mask(parent, radius, softness)` also enables rectangular clipping on the parent. Descendants inherit:

```text
mask rect
radius
softness
```

The Vulkan batch key includes all of these values, so commands with different masks can never be accidentally merged into one draw call.

The fragment shader computes a rounded-rectangle signed distance in screen coordinates and feathers the edge by `softness` pixels.

### Current nesting rule

v0.5 carries the nearest inherited rounded soft mask plus the normal hierarchical scissor intersection. Multiple arbitrary soft shapes are not multiplied together. This is sufficient for the common single card/viewport mask but is intentionally not advertised as general stencil composition.

`ASMUI_MASK_STENCIL` is reserved for v0.5, where nested stencil push/pop commands will represent arbitrary masks.
