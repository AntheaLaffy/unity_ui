DSE_APP := application/DiffSinger-ASM
DSE_BUILD := $(DSE_APP)/build
DSE_FLAGS := -I$(DSE_APP)/include $(shell pkg-config --cflags json-c yaml-0.1 icu-i18n icu-uc sdl3 sndfile samplerate) -DDSE_FRAMEWORK_ROOT='"$(CURDIR)"'
DSE_LIBS := $(shell pkg-config --libs json-c yaml-0.1 icu-i18n icu-uc sdl3 sndfile samplerate) $(shell mecab-config --libs)
DSE_PLATFORM := $(BUILD)/desktop.o $(BUILD)/canvas.o $(BUILD)/asmui.o $(BUILD)/text_stream.o $(BUILD)/font_sdf_freetype.o $(BUILD)/text_harfbuzz.o $(BUILD)/vulkan_backend.o $(BUILD)/vulkan_target_pool.o $(BUILD)/vulkan_texture.o
DSE_OBJECTS := $(addprefix $(DSE_BUILD)/,score.o mix.o audio.o project.o ustx.o frontend.o engine.o app.o ui.o main.o)
DSE_HEADERS := $(addprefix $(DSE_APP)/include/,editor.h app.h engine.h frontend.h audio.h project.h) include/asmui_canvas.h include/asmui_desktop.h
.PHONY: diffsinger diffsinger-build diffsinger-demo diffsinger-check diffsinger-render-check diffsinger-window-check diffsinger-engine diffsinger-sanitize diffsinger-visual-check
$(DSE_BUILD):
	mkdir -p $@
$(BUILD)/canvas.o: backend/canvas.c include/asmui_canvas.h | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@
$(BUILD)/desktop.o: backend/desktop.c include/asmui_desktop.h include/asmui_canvas.h | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) $(FONT_CFLAGS) $(PNG_CFLAGS) $$(pkg-config --cflags sdl3) -c $< -o $@
$(DSE_BUILD)/score.o: $(DSE_APP)/src/score.S $(DSE_APP)/src/abi.inc | $(DSE_BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@
$(DSE_BUILD)/mix.o: $(DSE_APP)/src/mix.S | $(DSE_BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@
$(DSE_BUILD)/%.o: $(DSE_APP)/src/%.c $(DSE_HEADERS) include/asmui.h | $(DSE_BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) $(DSE_FLAGS) -c $< -o $@
$(DSE_BUILD)/diffsinger-asm: $(DSE_OBJECTS) $(DSE_PLATFORM) $(BUILD)/shaders/ui.vert.spv $(BUILD)/shaders/ui.frag.spv
	$(CC) $(DSE_OBJECTS) $(DSE_PLATFORM) $(DSE_LIBS) $(FONT_LIBS) $(PNG_LIBS) -lvulkan -lm -pthread -o $@
diffsinger-build: $(DSE_BUILD)/diffsinger-asm
diffsinger: diffsinger-build
	./$(DSE_BUILD)/diffsinger-asm $(DIFFSINGER_ARGS)
diffsinger-demo: diffsinger-build
	./$(DSE_BUILD)/diffsinger-asm --headless --output $(DSE_BUILD)/editor.png
diffsinger-engine:
	$(MAKE) -C application/diffsinger build/dsasm-acoustic build/dsasm-vocoder-m40 build/dsasm-variance
$(DSE_BUILD)/test-core: $(DSE_APP)/tests/core.c $(addprefix $(DSE_BUILD)/,score.o mix.o audio.o project.o ustx.o frontend.o engine.o) $(BUILD)/canvas.o $(BUILD)/asmui.o | $(DSE_BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) $(DSE_FLAGS) $^ $(DSE_LIBS) -lm -pthread -o $@
$(DSE_BUILD)/test-app: $(DSE_APP)/tests/app.c $(filter-out $(DSE_BUILD)/main.o,$(DSE_OBJECTS)) $(BUILD)/canvas.o $(BUILD)/asmui.o | $(DSE_BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) $(DSE_FLAGS) $^ $(DSE_LIBS) -lm -pthread -o $@
$(DSE_BUILD)/test-audio: $(DSE_APP)/tests/audio.c $(addprefix $(DSE_BUILD)/,score.o mix.o audio.o project.o ustx.o frontend.o engine.o) $(BUILD)/canvas.o $(BUILD)/asmui.o | $(DSE_BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) $(DSE_FLAGS) $^ $(DSE_LIBS) -lm -pthread -o $@
$(DSE_BUILD)/test-frontend: $(DSE_APP)/tests/frontend.c $(addprefix $(DSE_BUILD)/,score.o mix.o audio.o project.o ustx.o frontend.o engine.o) $(BUILD)/canvas.o $(BUILD)/asmui.o | $(DSE_BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) $(DSE_FLAGS) $^ $(DSE_LIBS) -lm -pthread -o $@
diffsinger-check: $(DSE_BUILD)/test-core $(DSE_BUILD)/test-app $(DSE_BUILD)/test-audio $(DSE_BUILD)/test-frontend diffsinger-build
	./$(DSE_BUILD)/test-core
	./$(DSE_BUILD)/test-app
	./$(DSE_BUILD)/test-audio
	./$(DSE_BUILD)/test-frontend
$(DSE_BUILD)/test-sanitize: $(DSE_APP)/tests/app.c $(addprefix $(DSE_APP)/src/,app.c ui.c frontend.c engine.c audio.c project.c ustx.c) backend/canvas.c $(DSE_BUILD)/score.o $(DSE_BUILD)/mix.o $(BUILD)/asmui.o $(DSE_HEADERS) | $(DSE_BUILD)
	$(CC) -O1 -g -Wall -Wextra -Werror -std=c11 -fsanitize=address,undefined -fno-omit-frame-pointer $(INCLUDES) $(DSE_FLAGS) $(filter-out %.h,$^) $(DSE_LIBS) -lm -pthread -o $@
$(DSE_BUILD)/test-core-sanitize: $(DSE_APP)/tests/core.c $(addprefix $(DSE_APP)/src/,frontend.c engine.c audio.c project.c ustx.c) backend/canvas.c $(DSE_BUILD)/score.o $(DSE_BUILD)/mix.o $(BUILD)/asmui.o $(DSE_HEADERS) | $(DSE_BUILD)
	$(CC) -O1 -g -Wall -Wextra -Werror -std=c11 -fsanitize=address,undefined -fno-omit-frame-pointer $(INCLUDES) $(DSE_FLAGS) $(filter-out %.h,$^) $(DSE_LIBS) -lm -pthread -o $@
$(DSE_BUILD)/test-audio-sanitize: $(DSE_APP)/tests/audio.c $(addprefix $(DSE_APP)/src/,frontend.c engine.c audio.c project.c ustx.c) backend/canvas.c $(DSE_BUILD)/score.o $(DSE_BUILD)/mix.o $(BUILD)/asmui.o $(DSE_HEADERS) | $(DSE_BUILD)
	$(CC) -O1 -g -Wall -Wextra -Werror -std=c11 -fsanitize=address,undefined -fno-omit-frame-pointer $(INCLUDES) $(DSE_FLAGS) $(filter-out %.h,$^) $(DSE_LIBS) -lm -pthread -o $@
$(DSE_BUILD)/test-frontend-sanitize: $(DSE_APP)/tests/frontend.c $(addprefix $(DSE_APP)/src/,frontend.c engine.c audio.c project.c ustx.c) backend/canvas.c $(DSE_BUILD)/score.o $(DSE_BUILD)/mix.o $(BUILD)/asmui.o $(DSE_HEADERS) | $(DSE_BUILD)
	$(CC) -O1 -g -Wall -Wextra -Werror -std=c11 -fsanitize=address,undefined -fno-omit-frame-pointer $(INCLUDES) $(DSE_FLAGS) $(filter-out %.h,$^) $(DSE_LIBS) -lm -pthread -o $@
diffsinger-sanitize: $(DSE_BUILD)/test-sanitize $(DSE_BUILD)/test-core-sanitize $(DSE_BUILD)/test-audio-sanitize $(DSE_BUILD)/test-frontend-sanitize
	ASAN_OPTIONS=detect_leaks=1 ./$(DSE_BUILD)/test-sanitize
	ASAN_OPTIONS=detect_leaks=1 ./$(DSE_BUILD)/test-core-sanitize
	ASAN_OPTIONS=detect_leaks=1 ./$(DSE_BUILD)/test-audio-sanitize
	ASAN_OPTIONS=detect_leaks=1 ./$(DSE_BUILD)/test-frontend-sanitize
diffsinger-render-check: diffsinger-build
	python3 $(DSE_APP)/tools/check_render.py
diffsinger-window-check: diffsinger-build
	python3 $(DSE_APP)/tools/check_window.py
diffsinger-visual-check: diffsinger-build
	python3 $(DSE_APP)/tools/check_visual.py

.PHONY: diffsinger-ustx-check diffsinger-ustx-window-check
diffsinger-ustx-check: diffsinger-build
	python3 $(DSE_APP)/tools/check_ustx.py $(USTX_ROOT)
diffsinger-ustx-window-check: diffsinger-build
	python3 $(DSE_APP)/tools/check_ustx_window.py

.PHONY: diffsinger-audio-check diffsinger-audio-window-check
diffsinger-audio-check: diffsinger-build $(DSE_BUILD)/test-audio
	./$(DSE_BUILD)/test-audio
	python3 $(DSE_APP)/tools/check_audio.py
diffsinger-audio-window-check: diffsinger-build
	python3 $(DSE_APP)/tools/check_audio_window.py

.PHONY: diffsinger-frontend-check diffsinger-frontend-window-check
diffsinger-frontend-check: diffsinger-build $(DSE_BUILD)/test-frontend
	./$(DSE_BUILD)/test-frontend
	python3 $(DSE_APP)/tools/check_frontend.py
diffsinger-frontend-window-check: diffsinger-build
	python3 $(DSE_APP)/tools/check_frontend_window.py
