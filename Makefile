CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
ASFLAGS ?= -O2
GLSLC ?= glslc
INCLUDES = -Iinclude -I.
BUILD = build
SPVDIR = $(BUILD)/shaders
VULKAN_CFLAGS := $(shell pkg-config --cflags vulkan 2>/dev/null)
FONT_CFLAGS := $(shell pkg-config --cflags harfbuzz freetype2 2>/dev/null)
FONT_LIBS := $(shell pkg-config --libs harfbuzz freetype2 2>/dev/null)
PNG_CFLAGS := $(shell pkg-config --cflags libpng 2>/dev/null)
PNG_LIBS := $(shell pkg-config --libs libpng 2>/dev/null)

.PHONY: all test perf perf-core perf-scene perf-realtime perf-carousel perf-pointer perf-virtual perf-assets perf-text perf-gpu demo song-select-demo live-demo live-bench live-stream-bench stream-demo-assets live-bench-1f live-bench-compare shaders vulkan-check vulkan-runtime-smoke vulkan-stencil-smoke vulkan-offscreen-smoke vulkan-blur-smoke vulkan-glow-smoke vulkan-texture-async-smoke verify text-hb-check font-sdf-check png-check asset-cache-check clean
all: test $(BUILD)/editor

.PHONY: editor editor-demo editor-smoke editor-check editor-visual-check song-select-sdl-demo
.NOTPARALLEL: verify
EDITOR_OBJS = $(BUILD)/editor_i18n.o $(BUILD)/editor_scene.o $(BUILD)/application.o $(BUILD)/sound.o $(BUILD)/application_sound.o $(BUILD)/image_png.o $(BUILD)/asmui_editor.o $(BUILD)/asmui.o $(BUILD)/text_stream.o $(BUILD)/font_sdf_freetype.o $(BUILD)/text_harfbuzz.o $(BUILD)/vulkan_backend.o $(BUILD)/vulkan_target_pool.o $(BUILD)/vulkan_texture.o

$(BUILD)/sound.o: backend/sound.c include/asmui_sound.h include/asmui_application.h | $(BUILD)
	$(CC) $(CFLAGS) -Werror $(INCLUDES) -c $< -o $@

$(BUILD)/application_sound.o: demo/application_sound.c demo/application_sound.h include/asmui_sound.h include/asmui_application.h | $(BUILD)
	$(CC) $(CFLAGS) -Werror $(INCLUDES) $$(pkg-config --cflags sdl3) -c $< -o $@

$(BUILD)/application.o: backend/application.c include/asmui_application.h include/asmui_builder.h | $(BUILD)
	$(CC) $(CFLAGS) -Werror $(INCLUDES) -c $< -o $@

$(BUILD)/editor_i18n.o: demo/editor_i18n.c include/asmui_editor_i18n.h | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/asmui_editor.o: src/asmui_editor.S | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/editor_scene.o: demo/editor_scene.c include/asmui_editor.h include/asmui_editor_i18n.h include/asmui_builder.h include/asmui.h | $(BUILD)
	$(CC) $(CFLAGS) -Werror $(INCLUDES) $(FONT_CFLAGS) -c $< -o $@

$(BUILD)/editor: demo/editor_main.c demo/application_sound.h include/asmui_editor.h $(EDITOR_OBJS) shaders
	$(CC) $(CFLAGS) -Werror $(INCLUDES) $(FONT_CFLAGS) $(PNG_CFLAGS) demo/editor_main.c $(EDITOR_OBJS) $$(pkg-config --cflags --libs sdl3) $(FONT_LIBS) $(PNG_LIBS) -lvulkan -lm -ldl -o $@

OPENSEKAI = application/OpenSekai_Community_rs
OPENSEKAI_SCENE = $(OPENSEKAI)/native/generated/editor-scene.json
.PHONY: opensekai opensekai-check opensekai-editor opensekai-assets opensekai-window-check opensekai-live-check opensekai-song-check opensekai-result-check opensekai-visual-check opensekai-gameplay-check opensekai-scoring-check
opensekai: $(OPENSEKAI_SCENE)
	cargo build --release --locked --manifest-path $(OPENSEKAI)/Cargo.toml

opensekai-check: opensekai $(BUILD)/application_core $(BUILD)/sound_core $(BUILD)/sound_stream
	./$(BUILD)/application_core
	./$(BUILD)/sound_core
	./$(BUILD)/sound_stream
	python3 $(OPENSEKAI)/tools/capture_baseline.py
	cargo test --locked --manifest-path $(OPENSEKAI)/Cargo.toml
	python3 $(OPENSEKAI)/tools/check_native_application.py
	python3 $(OPENSEKAI)/tools/check_live_application.py
	python3 $(OPENSEKAI)/tools/check_scoring_application.py
	python3 $(OPENSEKAI)/tools/check_sound_application.py

opensekai-assets:
	python3 $(OPENSEKAI)/tools/import_unity_assets.py
	python3 $(OPENSEKAI)/tools/compile_native_scene.py
	python3 $(OPENSEKAI)/tools/compile_soundbank.py
	python3 $(OPENSEKAI)/tools/compile_result_rig.py

$(BUILD)/sound_core: tests/sound_core.c $(BUILD)/sound.o
	$(CC) $(CFLAGS) -Werror $(INCLUDES) $^ -lm -o $@

$(BUILD)/sound_stream: tests/sound_stream.c $(BUILD)/application_sound.o $(BUILD)/sound.o
	$(CC) $(CFLAGS) -Werror $(INCLUDES) $^ $$(pkg-config --cflags --libs sdl3) -lm -o $@

.PHONY: sound-check
sound-check: $(BUILD)/sound_core $(BUILD)/sound_stream
	./$(BUILD)/sound_core
	./$(BUILD)/sound_stream

$(OPENSEKAI_SCENE): $(OPENSEKAI)/tools/import_unity_assets.py $(OPENSEKAI)/tools/compile_native_scene.py $(OPENSEKAI)/tools/compile_live_presentations.py $(OPENSEKAI)/tools/compile_particles.py $(OPENSEKAI)/tools/compile_live_background.py $(OPENSEKAI)/tools/compile_live_hud.py $(OPENSEKAI)/tools/compile_live_hit.py $(OPENSEKAI)/Assets/Resources/live/view/Background2DView.prefab $(OPENSEKAI)/Assets/Resources/live/view/FrontUIView.prefab $(OPENSEKAI)/Assets/Resources/screen/prefabs/ScreenLayerMusicScoreMaker.prefab $(OPENSEKAI)/Assets/GameObject/NotePreviewPrefab.prefab $(OPENSEKAI)/Assets/Resources/wording/wording_zh.txt $(OPENSEKAI)/Assets/Resources/wording/master_wording_zh.txt
	python3 $(OPENSEKAI)/tools/import_unity_assets.py
	python3 $(OPENSEKAI)/tools/compile_native_scene.py

opensekai-window-check: $(BUILD)/editor opensekai
	python3 tools/check_opensekai_window.py $(OPENSEKAI_WINDOW_ARGS)

opensekai-live-check: $(BUILD)/editor opensekai
	python3 tools/check_opensekai_live.py $(OPENSEKAI_LIVE_ARGS)

opensekai-song-check: $(BUILD)/editor opensekai
	python3 tools/check_opensekai_song.py $(OPENSEKAI_SONG_ARGS)

opensekai-result-check: $(BUILD)/editor opensekai
	python3 tools/check_opensekai_results.py $(OPENSEKAI_RESULT_ARGS)

opensekai-visual-check: $(BUILD)/editor opensekai
	python3 tools/check_opensekai_visual_play.py

opensekai-gameplay-check: $(BUILD)/editor opensekai
	python3 tools/check_opensekai_gameplay_reference.py $(OPENSEKAI_GAMEPLAY_ARGS)

opensekai-scoring-check: $(BUILD)/editor opensekai
	python3 tools/check_opensekai_scoring.py $(OPENSEKAI_SCORING_ARGS)

opensekai-editor: $(BUILD)/editor opensekai
	./$(BUILD)/editor --project $(OPENSEKAI)

editor: $(BUILD)/editor
	./$(BUILD)/editor

editor-demo: $(BUILD)/editor
	./$(BUILD)/editor --headless --output $(BUILD)/editor.png

editor-smoke: $(BUILD)/editor
	./$(BUILD)/editor --frames 90 --smoke --output $(BUILD)/editor-live.png

$(BUILD)/editor_core: tests/editor_core.c $(BUILD)/editor_scene.o $(BUILD)/editor_i18n.o $(BUILD)/asmui_editor.o $(BUILD)/asmui.o $(BUILD)/font_sdf_freetype.o $(BUILD)/text_harfbuzz.o
	$(CC) $(CFLAGS) -Werror $(INCLUDES) $(FONT_CFLAGS) $^ $(FONT_LIBS) -lm -o $@

editor-check: $(BUILD)/editor_core
	./$(BUILD)/editor_core

editor-visual-check: $(BUILD)/editor
	python3 tools/check_editor_visual.py

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/asmui.o: src/asmui.S | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/smoke: tests/smoke.c $(BUILD)/asmui.o
	$(CC) $(CFLAGS) $(INCLUDES) $^ -lm -o $@

$(BUILD)/sprite: tests/sprite.c $(BUILD)/asmui.o
	$(CC) $(CFLAGS) $(INCLUDES) $^ -lm -o $@

$(BUILD)/application_core: tests/application_core.c $(BUILD)/application.o $(BUILD)/asmui.o include/asmui_application.h
	$(CC) $(CFLAGS) -Werror $(INCLUDES) tests/application_core.c $(BUILD)/application.o $(BUILD)/asmui.o -ldl -lm -o $@

$(BUILD)/filled: tests/filled.c $(BUILD)/asmui.o
	$(CC) $(CFLAGS) $(INCLUDES) $^ -lm -o $@

$(BUILD)/tween_fill: tests/tween_fill.c $(BUILD)/asmui.o
	$(CC) $(CFLAGS) $(INCLUDES) $^ -lm -o $@

$(BUILD)/batch: tests/batch.c $(BUILD)/asmui.o
	$(CC) $(CFLAGS) $(INCLUDES) $^ -lm -o $@

$(BUILD)/gpu_vertices: tests/gpu_vertices.c $(BUILD)/asmui.o
	$(CC) $(CFLAGS) $(INCLUDES) $^ -lm -o $@

$(BUILD)/text_stream.o: backend/text_stream.c include/asmui_text.h include/asmui.h | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/soft_mask: tests/soft_mask.c $(BUILD)/asmui.o
	$(CC) $(CFLAGS) $(INCLUDES) $^ -lm -o $@

$(BUILD)/text_expand: tests/text_expand.c $(BUILD)/asmui.o $(BUILD)/text_stream.o
	$(CC) $(CFLAGS) $(INCLUDES) $^ -lm -o $@

$(BUILD)/stencil_core: tests/stencil_core.c $(BUILD)/asmui.o
	$(CC) $(CFLAGS) $(INCLUDES) $^ -lm -o $@

$(BUILD)/render_ops.o: backend/render_ops.c include/asmui.h | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/layer_ops: tests/layer_ops.c $(BUILD)/asmui.o $(BUILD)/render_ops.o
	$(CC) $(CFLAGS) $(INCLUDES) $^ -lm -o $@

$(BUILD)/text_scene_struct: tests/text_scene_struct.c demo/song_select_scene_text.h $(BUILD)/asmui.o
	$(CC) $(CFLAGS) $(INCLUDES) tests/text_scene_struct.c $(BUILD)/asmui.o -lm -o $@


$(BUILD)/interaction.o: backend/interaction.c include/asmui_interaction.h include/asmui.h | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/interaction: tests/interaction.c $(BUILD)/interaction.o
	$(CC) $(CFLAGS) $(INCLUDES) tests/interaction.c $(BUILD)/interaction.o -lm -o $@

$(BUILD)/song_binding: tests/song_binding.c demo/song_select_scene_text.h $(BUILD)/asmui.o
	$(CC) $(CFLAGS) $(INCLUDES) tests/song_binding.c $(BUILD)/asmui.o -lm -o $@

$(BUILD)/virtual_list: tests/virtual_list.c demo/song_select_scene_text.h $(BUILD)/asmui.o $(BUILD)/interaction.o
	$(CC) $(CFLAGS) $(INCLUDES) tests/virtual_list.c $(BUILD)/asmui.o $(BUILD)/interaction.o -lm -o $@


$(BUILD)/asset_cache.o: backend/asset_cache.c include/asmui_asset_cache.h | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/asset_cache: tests/asset_cache.c $(BUILD)/asset_cache.o
	$(CC) $(CFLAGS) $(INCLUDES) tests/asset_cache.c $(BUILD)/asset_cache.o -o $@

asset-cache-check: $(BUILD)/asset_cache
	./$(BUILD)/asset_cache

$(BUILD)/asset_cache_profile: tests/asset_cache_profile.c $(BUILD)/asset_cache.o
	$(CC) $(CFLAGS) $(INCLUDES) tests/asset_cache_profile.c $(BUILD)/asset_cache.o -o $@

perf-assets: $(BUILD)/asset_cache_profile
	./$(BUILD)/asset_cache_profile

$(BUILD)/perf_core: tests/perf_core.c $(BUILD)/asmui.o
	$(CC) $(CFLAGS) $(INCLUDES) $^ -lm -o $@

perf-core: $(BUILD)/perf_core
	./$(BUILD)/perf_core

perf: perf-core perf-scene perf-realtime perf-carousel perf-pointer perf-virtual perf-assets perf-text

$(BUILD)/scene_profile: tests/scene_profile.c demo/song_select_scene.h $(BUILD)/asmui.o
	$(CC) $(CFLAGS) $(INCLUDES) tests/scene_profile.c $(BUILD)/asmui.o -lm -o $@

perf-scene: $(BUILD)/scene_profile
	./$(BUILD)/scene_profile


$(BUILD)/realtime_loop: tests/realtime_loop.c demo/song_select_scene.h $(BUILD)/asmui.o
	$(CC) $(CFLAGS) $(INCLUDES) tests/realtime_loop.c $(BUILD)/asmui.o -lm -o $@

perf-realtime: $(BUILD)/realtime_loop
	./$(BUILD)/realtime_loop

$(BUILD)/carousel_profile: tests/carousel_profile.c demo/song_select_scene_text.h $(BUILD)/asmui.o $(BUILD)/interaction.o
	$(CC) $(CFLAGS) $(INCLUDES) tests/carousel_profile.c $(BUILD)/asmui.o $(BUILD)/interaction.o -lm -o $@

perf-carousel: $(BUILD)/carousel_profile
	./$(BUILD)/carousel_profile

$(BUILD)/pointer_profile: tests/pointer_profile.c demo/song_select_scene_text.h $(BUILD)/asmui.o $(BUILD)/interaction.o
	$(CC) $(CFLAGS) $(INCLUDES) tests/pointer_profile.c $(BUILD)/asmui.o $(BUILD)/interaction.o -lm -o $@

perf-pointer: $(BUILD)/pointer_profile
	./$(BUILD)/pointer_profile

$(BUILD)/virtual_list_profile: tests/virtual_list_profile.c demo/song_select_scene_text.h $(BUILD)/asmui.o $(BUILD)/interaction.o
	$(CC) $(CFLAGS) $(INCLUDES) tests/virtual_list_profile.c $(BUILD)/asmui.o $(BUILD)/interaction.o -lm -o $@

perf-virtual: $(BUILD)/virtual_list_profile
	./$(BUILD)/virtual_list_profile

$(BUILD)/font_sdf_freetype.o: backend/font_sdf_freetype.c include/asmui_font_sdf_ft.h include/asmui_text_hb.h | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) $(FONT_CFLAGS) -c $< -o $@

$(BUILD)/text_harfbuzz.o: backend/text_harfbuzz.c include/asmui_text_hb.h include/asmui_text.h | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) $(FONT_CFLAGS) -c $< -o $@

$(BUILD)/font_sdf: tests/font_sdf.c demo/song_select_scene_text.h $(BUILD)/font_sdf_freetype.o $(BUILD)/text_harfbuzz.o
	$(CC) $(CFLAGS) $(INCLUDES) $(FONT_CFLAGS) tests/font_sdf.c $(BUILD)/font_sdf_freetype.o $(BUILD)/text_harfbuzz.o $(FONT_LIBS) -lm -o $@

font-sdf-check: $(BUILD)/font_sdf
	./$(BUILD)/font_sdf

$(BUILD)/text_scene_profile: tests/text_scene_profile.c demo/song_select_scene_text.h $(BUILD)/asmui.o $(BUILD)/text_stream.o $(BUILD)/font_sdf_freetype.o $(BUILD)/text_harfbuzz.o
	$(CC) $(CFLAGS) $(INCLUDES) $(FONT_CFLAGS) tests/text_scene_profile.c $(BUILD)/asmui.o $(BUILD)/text_stream.o $(BUILD)/font_sdf_freetype.o $(BUILD)/text_harfbuzz.o $(FONT_LIBS) -lm -o $@

perf-text: $(BUILD)/text_scene_profile
	./$(BUILD)/text_scene_profile


test: $(BUILD)/smoke $(BUILD)/sprite $(BUILD)/filled $(BUILD)/tween_fill $(BUILD)/batch $(BUILD)/gpu_vertices $(BUILD)/soft_mask $(BUILD)/text_expand $(BUILD)/stencil_core $(BUILD)/layer_ops $(BUILD)/text_scene_struct $(BUILD)/interaction $(BUILD)/song_binding $(BUILD)/virtual_list $(BUILD)/asset_cache
	./$(BUILD)/smoke
	./$(BUILD)/sprite
	./$(BUILD)/filled
	./$(BUILD)/tween_fill
	./$(BUILD)/batch
	./$(BUILD)/gpu_vertices
	./$(BUILD)/soft_mask
	./$(BUILD)/text_expand
	./$(BUILD)/stencil_core
	./$(BUILD)/layer_ops
	./$(BUILD)/text_scene_struct
	./$(BUILD)/interaction
	./$(BUILD)/song_binding
	./$(BUILD)/virtual_list
	./$(BUILD)/asset_cache

$(BUILD)/demo: demo/main.c backend/sdl3_backend.c $(BUILD)/asmui.o
	$(CC) $(CFLAGS) $(INCLUDES) $^ $$(pkg-config --cflags --libs sdl3) -lm -o $@

demo: editor

song-select-sdl-demo: $(BUILD)/demo
	./$(BUILD)/demo

$(BUILD)/image_png.o: backend/image_png.c include/asmui_image_png.h | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) $(PNG_CFLAGS) -c $< -o $@

$(BUILD)/png_loader: tests/png_loader.c $(BUILD)/image_png.o
	$(CC) $(CFLAGS) $(INCLUDES) $(PNG_CFLAGS) tests/png_loader.c $(BUILD)/image_png.o $(PNG_LIBS) -o $@

png-check: $(BUILD)/png_loader
	./$(BUILD)/png_loader

$(SPVDIR):
	mkdir -p $(SPVDIR)

$(SPVDIR)/ui.vert.spv: shaders/ui.vert | $(SPVDIR)
	$(GLSLC) --target-env=vulkan1.3 $< -o $@

$(SPVDIR)/ui.frag.spv: shaders/ui.frag | $(SPVDIR)
	$(GLSLC) --target-env=vulkan1.3 $< -o $@

$(SPVDIR)/post.vert.spv: shaders/post.vert | $(SPVDIR)
	$(GLSLC) --target-env=vulkan1.3 $< -o $@

$(SPVDIR)/post.frag.spv: shaders/post.frag | $(SPVDIR)
	$(GLSLC) --target-env=vulkan1.3 $< -o $@

shaders: $(SPVDIR)/ui.vert.spv $(SPVDIR)/ui.frag.spv $(SPVDIR)/post.vert.spv $(SPVDIR)/post.frag.spv

$(BUILD)/vulkan_backend.o: backend/vulkan_backend.c include/asmui_vulkan.h include/asmui.h | $(BUILD)
	$(CC) $(CFLAGS) -Werror=missing-field-initializers $(INCLUDES) $(VULKAN_CFLAGS) -c $< -o $@

$(BUILD)/vulkan_target_pool.o: backend/vulkan_target_pool.c include/asmui_vulkan.h include/asmui.h | $(BUILD)
	$(CC) $(CFLAGS) -Werror=missing-field-initializers $(INCLUDES) $(VULKAN_CFLAGS) -c $< -o $@

$(BUILD)/vulkan_post.o: backend/vulkan_post.c include/asmui_post_vulkan.h | $(BUILD)
	$(CC) $(CFLAGS) -Werror=missing-field-initializers $(INCLUDES) $(VULKAN_CFLAGS) -c $< -o $@

$(BUILD)/vulkan_texture.o: backend/vulkan_texture.c include/asmui_vulkan_texture.h | $(BUILD)
	$(CC) $(CFLAGS) -Werror=missing-field-initializers $(INCLUDES) $(VULKAN_CFLAGS) -c $< -o $@

vulkan-check: shaders $(BUILD)/vulkan_backend.o $(BUILD)/vulkan_target_pool.o $(BUILD)/vulkan_post.o $(BUILD)/vulkan_texture.o
	@echo "Vulkan backend, texture upload, and SPIR-V shaders built successfully."

$(BUILD)/vulkan_texture_async: tests/vulkan_texture_async.c $(BUILD)/vulkan_texture.o
	$(CC) $(CFLAGS) -Werror=missing-field-initializers $(INCLUDES) $(VULKAN_CFLAGS) tests/vulkan_texture_async.c $(BUILD)/vulkan_texture.o -lvulkan -o $@

vulkan-texture-async-smoke: $(BUILD)/vulkan_texture_async
	./$(BUILD)/vulkan_texture_async

$(BUILD)/vulkan_runtime: tests/vulkan_runtime.c $(BUILD)/vulkan_backend.o $(BUILD)/asmui.o shaders
	$(CC) $(CFLAGS) -Werror=missing-field-initializers $(INCLUDES) $(VULKAN_CFLAGS) tests/vulkan_runtime.c $(BUILD)/vulkan_backend.o $(BUILD)/asmui.o -lvulkan -lm -o $@

vulkan-runtime-smoke: $(BUILD)/vulkan_runtime
	./$(BUILD)/vulkan_runtime

$(BUILD)/vulkan_stencil: tests/vulkan_stencil.c $(BUILD)/vulkan_backend.o $(BUILD)/asmui.o shaders
	$(CC) $(CFLAGS) -Werror=missing-field-initializers $(INCLUDES) $(VULKAN_CFLAGS) tests/vulkan_stencil.c $(BUILD)/vulkan_backend.o $(BUILD)/asmui.o -lvulkan -lm -o $@

vulkan-stencil-smoke: $(BUILD)/vulkan_stencil
	./$(BUILD)/vulkan_stencil


$(BUILD)/vulkan_offscreen: tests/vulkan_offscreen.c $(BUILD)/vulkan_backend.o $(BUILD)/vulkan_target_pool.o $(BUILD)/asmui.o shaders
	$(CC) $(CFLAGS) -Werror=missing-field-initializers $(INCLUDES) $(VULKAN_CFLAGS) tests/vulkan_offscreen.c $(BUILD)/vulkan_backend.o $(BUILD)/vulkan_target_pool.o $(BUILD)/asmui.o -lvulkan -lm -o $@

vulkan-offscreen-smoke: $(BUILD)/vulkan_offscreen
	./$(BUILD)/vulkan_offscreen

$(BUILD)/vulkan_blur: tests/vulkan_blur.c $(BUILD)/vulkan_backend.o $(BUILD)/vulkan_target_pool.o $(BUILD)/vulkan_post.o $(BUILD)/asmui.o shaders
	$(CC) $(CFLAGS) -Werror=missing-field-initializers $(INCLUDES) $(VULKAN_CFLAGS) tests/vulkan_blur.c $(BUILD)/vulkan_backend.o $(BUILD)/vulkan_target_pool.o $(BUILD)/vulkan_post.o $(BUILD)/asmui.o -lvulkan -lm -o $@

vulkan-blur-smoke: $(BUILD)/vulkan_blur
	./$(BUILD)/vulkan_blur

$(BUILD)/vulkan_glow: tests/vulkan_glow.c $(BUILD)/vulkan_backend.o $(BUILD)/vulkan_target_pool.o $(BUILD)/vulkan_post.o $(BUILD)/asmui.o shaders
	$(CC) $(CFLAGS) -Werror=missing-field-initializers $(INCLUDES) $(VULKAN_CFLAGS) tests/vulkan_glow.c $(BUILD)/vulkan_backend.o $(BUILD)/vulkan_target_pool.o $(BUILD)/vulkan_post.o $(BUILD)/asmui.o -lvulkan -lm -o $@

vulkan-glow-smoke: $(BUILD)/vulkan_glow
	./$(BUILD)/vulkan_glow


$(BUILD)/song_select_demo: demo/song_select_vulkan.c demo/song_select_scene.h $(BUILD)/vulkan_backend.o $(BUILD)/vulkan_target_pool.o $(BUILD)/vulkan_post.o $(BUILD)/asmui.o shaders
	$(CC) $(CFLAGS) -Werror=missing-field-initializers $(INCLUDES) $(VULKAN_CFLAGS) demo/song_select_vulkan.c $(BUILD)/vulkan_backend.o $(BUILD)/vulkan_target_pool.o $(BUILD)/vulkan_post.o $(BUILD)/asmui.o -lvulkan -lm -o $@

song-select-demo: $(BUILD)/song_select_demo
	./$(BUILD)/song_select_demo
	@echo "Open build/song_select.ppm to inspect the rendered UI."


$(BUILD)/song_select_live: demo/song_select_live.c demo/song_select_scene.h demo/song_select_scene_text.h $(BUILD)/interaction.o $(BUILD)/asset_cache.o $(BUILD)/vulkan_backend.o $(BUILD)/vulkan_target_pool.o $(BUILD)/vulkan_post.o $(BUILD)/vulkan_texture.o $(BUILD)/text_stream.o $(BUILD)/text_harfbuzz.o $(BUILD)/font_sdf_freetype.o $(BUILD)/image_png.o $(BUILD)/asmui.o shaders
	$(CC) $(CFLAGS) -Werror=missing-field-initializers -Werror=misleading-indentation -Werror=unused-function $(INCLUDES) $(VULKAN_CFLAGS) $(FONT_CFLAGS) $(PNG_CFLAGS) demo/song_select_live.c $(BUILD)/interaction.o $(BUILD)/asset_cache.o $(BUILD)/vulkan_backend.o $(BUILD)/vulkan_target_pool.o $(BUILD)/vulkan_post.o $(BUILD)/vulkan_texture.o $(BUILD)/text_stream.o $(BUILD)/text_harfbuzz.o $(BUILD)/font_sdf_freetype.o $(BUILD)/image_png.o $(BUILD)/asmui.o $$(pkg-config --cflags --libs sdl3) $(FONT_LIBS) $(PNG_LIBS) -lvulkan -lm -o $@

live-demo: $(BUILD)/song_select_live
	./$(BUILD)/song_select_live

live-bench: $(BUILD)/song_select_live
	./$(BUILD)/song_select_live --frames 600

stream-demo-assets: | $(BUILD)
	python3 tools/generate_stream_jackets.py $(BUILD)/jackets 16

live-stream-bench: $(BUILD)/song_select_live stream-demo-assets
	ASMUI_STREAM_BENCH=1 ASMUI_JACKET_DIR=$(BUILD)/jackets ASMUI_JACKET_REPEAT=16 ASMUI_ASSET_LOADS_PER_FRAME=1 ASMUI_VIRTUAL_SONGS=100000 ASMUI_PRESENT_MODE=mailbox ASMUI_FRAMES_IN_FLIGHT=3 ./$(BUILD)/song_select_live --frames 1200

live-bench-1f: $(BUILD)/song_select_live
	ASMUI_FRAMES_IN_FLIGHT=1 ./$(BUILD)/song_select_live --frames 600

live-bench-compare: $(BUILD)/song_select_live
	@echo "===== 1 frame in flight ====="
	ASMUI_FRAMES_IN_FLIGHT=1 ./$(BUILD)/song_select_live --frames 600
	@echo "===== 3 frames in flight ====="
	ASMUI_FRAMES_IN_FLIGHT=3 ./$(BUILD)/song_select_live --frames 600

perf-gpu: vulkan-blur-smoke vulkan-glow-smoke
	@echo "GPU perf pair complete: full-res blur + half-res glow."

verify: test perf-core perf-scene perf-realtime perf-carousel perf-pointer perf-virtual perf-assets font-sdf-check perf-text png-check text-hb-check vulkan-check vulkan-runtime-smoke vulkan-texture-async-smoke vulkan-stencil-smoke vulkan-offscreen-smoke vulkan-blur-smoke vulkan-glow-smoke song-select-demo editor-check editor-visual-check
	@echo "ASMUI v0.21 full verification passed. Editor visual output: build/editor-previews/"

text-hb-check: $(BUILD)/text_harfbuzz.o
	@echo "HarfBuzz adapter built successfully."

clean:
	rm -rf $(BUILD)

include application/DiffSinger-ASM/framework.mk
