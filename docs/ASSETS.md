# External visual assets (v0.13)

v0.13 adds the first external-art path to the live Vulkan demo.

## PNG jacket

Set `ASMUI_JACKET` to any ordinary RGBA/RGB PNG:

```bash
ASMUI_JACKET=/path/to/jacket.png \
ASMUI_PRESENT_MODE=mailbox \
ASMUI_FRAMES_IN_FLIGHT=3 \
make live-demo
```

The PNG is decoded once with libpng, converted to RGBA8, uploaded once through
`asmui_vk_texture_upload_rgba8()`, assigned resource id 2, and then rendered by
the normal ASMUI image/batch path. The retained scene keeps the jacket image node
invisible when no external asset is supplied, so regression runs remain deterministic.

The current demo stretches the jacket into a 320x320 square. Aspect-fit/crop modes
and a general asset manifest are intentionally left for the next step; this version
validates the end-to-end resource path first.

## Verification

`make png-check` decodes an embedded 2x2 PNG and verifies all 16 RGBA bytes exactly.
`make verify` includes this check.

## v0.14 jacket aspect modes

The live demo no longer assumes jacket PNGs are square. Set:

```bash
ASMUI_JACKET_MODE=cover   # default: fixed 320x320 slot, center-crop UV
ASMUI_JACKET_MODE=contain # preserve whole image, shrink geometry into slot
ASMUI_JACKET_MODE=stretch # legacy full-UV 320x320 behavior
```

At startup the demo prints the resulting geometry and UV rectangle.
