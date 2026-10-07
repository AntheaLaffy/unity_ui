# Retained song carousel (v0.15)

`AsmUiCarousel` is a UI interaction primitive, not Song-Select-specific math. It owns a clamped selected index and a critically damped continuous position. `asmui_carousel_item()` maps the continuous position to each retained card's position, scale, alpha and emphasis.

The live demo creates seven card roots. Accent and title nodes are children of each root, so one root transform updates the whole card. Painter order is not globally resorted; cards use enough spacing to avoid relying on depth sorting.

The benchmark path automatically nudges the selection every 120 frames. `tests/carousel_profile.c` runs 2400 frames and guards both batch count and CPU p95. The default p95 threshold is deliberately loose (`1000 us`) for portable CI and can be tightened using `ASMUI_CAROUSEL_CPU_P95_US`.
