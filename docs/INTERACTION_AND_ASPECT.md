# v0.14 — Image aspect placement and snap interaction

## Image placement

`asmui_image_place()` separates source-image aspect handling from the renderer.
The Vulkan backend still sees an ordinary retained image node.

- `ASMUI_ASPECT_STRETCH`: full UV, full box; legacy behavior.
- `ASMUI_ASPECT_CONTAIN`: full UV, geometry shrinks to fit inside the box.
- `ASMUI_ASPECT_COVER`: geometry keeps the box, UVs are center-cropped.

For a 1600x900 image in a 320x320 box:

- contain -> 320x180, UV `[0,0]-[1,1]`;
- cover -> 320x320, U approximately `[0.21875,0.78125]`.

The live Song Select selects the mode with `ASMUI_JACKET_MODE`; cover is the
default because rhythm-game jacket slots are normally fixed-size.

## Snap scroll

`AsmUiSnapScroll` is a small renderer-independent interaction state. It stores
position, velocity and a clamped target and integrates a critically damped spring.
Large frame deltas are internally substepped to avoid an unstable jump after a
pause/debugger stop.

The v0.14 live demo uses the same state to move the selected navigation tile.
The primitive is intentionally generic so the next song-carousel implementation
can use exactly the same state for list snapping/inertial transitions.

## Regression coverage

`tests/interaction.c` verifies:

1. contain geometry for a non-square image;
2. cover UV cropping;
3. placement application to a retained node;
4. spring convergence;
5. target clamping.

The test is part of `make test`, and therefore `make verify`.
