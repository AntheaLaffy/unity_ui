# v0.12 real text path

The old 5x7 font was useful because it had no external assets, but a single label
could become hundreds of image nodes. That is intentionally no longer the live path.

The v0.12 path is:

```text
UTF-8 label node
    -> HarfBuzz shaping
    -> glyph ids + advances/offsets
    -> FreeType runtime SDF atlas lookup
    -> AsmUiGlyphQuad[]
    -> asm_ui_expand_text_draw()        [x86-64 ASM]
    -> painter-order draw stream
    -> ASM batch + GPU vertex packing
    -> Vulkan atlas sampling
```

## Atlas policy

Only glyphs reached by shaping the scene's known strings are rasterized. The demo
uses a 64 px EM raster and requests an 8 px FreeType SDF spread. The grayscale SDF
value is replicated into RGB so the existing median-based MSDF shader can render it;
this is a single-channel SDF, not a claim that FreeType generated true multi-channel
MSDF data.

For a production asset pipeline we can later replace this startup atlas with an
offline MSDF atlas without changing the ASM text expansion ABI.

## Font selection

`ASMUI_FONT` wins. Otherwise common Noto/DejaVu/Go font locations are probed. If none
is found or the atlas/upload fails, the live demo falls back to the deterministic 5x7
scene instead of failing the renderer.

For Chinese/Japanese coverage, point `ASMUI_FONT` at a CJK font when needed, e.g. a
Noto Sans CJK `.ttc` installed on the machine.

## Regression strategy

`make font-sdf-check` verifies shaping, atlas UVs and emits an atlas preview.

`make perf-text` verifies the architectural win, not just elapsed time: the retained
scene must stay under 96 nodes, expanded glyph/image draws under 180, and batches
under 64. `ASMUI_TEXT_SCENE_MAX_NS_PER_DRAW` can tighten the portable timing guard.

The original 915-node pixel scene remains in `perf-scene` so historical hot-path
comparisons remain meaningful.
