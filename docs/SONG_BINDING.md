# Song selection binding

v0.16 connects the reusable `AsmUiCarousel` selection state to retained song-detail nodes.

`AsmUiSongMeta` owns immutable metadata pointers and visual values. `asmui_song_select_bind()` does not allocate and does not rebuild the font atlas; it only swaps retained text payload pointers and updates gauge/accent colors. `asmui_song_text_samples()` includes the complete catalog so every glyph needed by any selection is present before the Vulkan atlas is uploaded.

The live frame loop only calls the binder when `carousel.selected` changes. Spring position continues independently, so the carousel remains smoothly animated while metadata changes once per selection event.

Regression coverage:

- `tests/song_binding.c`: checks all seven entries and clamped out-of-range selection;
- `tests/carousel_profile.c`: 2400-frame carousel benchmark includes real rebinding and confirms the detail title matches the selected song;
- ordinary text/GPU regressions continue to protect glyph expansion and rendering.
