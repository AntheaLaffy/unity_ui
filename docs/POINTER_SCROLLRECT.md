# Pointer / ScrollRect interaction (v0.17)

ASMUI keeps SDL out of the interaction core. Window coordinates are converted to the
center-origin authoring space with `asmui_pointer_to_canvas()`, then the reusable
carousel helpers provide:

- scaled-card hit testing;
- click-to-select;
- direct drag following;
- release projection + snap;
- mouse-wheel stepping;
- selected-song metadata rebinding without allocation.

The live demo maps SDL3 mouse button/motion/wheel events onto these helpers. The
interaction layer itself has no SDL dependency and can be reused by Wayland-native,
touch, or another window backend.

## Regression

`make perf-pointer` simulates 2400 frames containing repeated drags, release snaps,
wheel input, retained scene update, layout, draw-list generation and batching. It
checks selection/detail consistency and batch growth as well as CPU p95.

Override the broad default guardrail with:

```bash
ASMUI_POINTER_CPU_P95_US=100 make perf-pointer
```
