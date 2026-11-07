# 3Disle: `make` builds the app, `make test` runs the checks, `make asan` the checks under sanitizers.
CC ?= cc
BUILD ?= build
CFLAGS ?= -O2 -g
WARN = -std=c17 -D_GNU_SOURCE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers
INC = -Iengine -Igfx -Iapp -Ithird_party -I$(BUILD)/gen
SDL_CFLAGS := $(shell pkg-config --cflags sdl3 2>/dev/null)
SDL_LIBS := $(shell pkg-config --libs sdl3 2>/dev/null)

ENGINE = engine/json.c engine/spec.c engine/mesh.c engine/model.c engine/paint.c engine/shot.c engine/platform.c engine/glb.c
ENGINE_OBJ = $(ENGINE:%.c=$(BUILD)/%.o)
GEN = $(BUILD)/gen/fonts_data.h $(BUILD)/gen/shaders_data.h $(BUILD)/gen/web_data.h
SHADERS = $(wildcard gfx/shaders/*.glsl)

GFX = gfx/render.c gfx/ui.c
APP = app/main.c app/headless.c app/stage.c app/viewer.c app/script.c app/window.c app/video.c app/editor.c app/ed_build.c app/ed_props.c app/ed_animate.c app/export.c app/check.c
GFX_OBJ = $(GFX:%.c=$(BUILD)/%.o)
APP_OBJ = $(APP:%.c=$(BUILD)/%.o)

all: $(BUILD)/3disle

$(BUILD)/3disle: $(ENGINE_OBJ) $(GFX_OBJ) $(APP_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(SDL_LIBS) -lEGL -lGLESv2 -lm

$(BUILD)/gfx/%.o: gfx/%.c $(BUILD)/gen/fonts_data.h $(BUILD)/gen/shaders_data.h $(wildcard engine/*.h gfx/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(WARN) $(INC) -c -o $@ $<

$(BUILD)/app/%.o: app/%.c $(GEN) $(wildcard engine/*.h gfx/*.h app/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(WARN) -Wno-unused-function $(INC) $(SDL_CFLAGS) -c -o $@ $<

$(BUILD)/tools/embed: tools/embed.c
	@mkdir -p $(dir $@)
	$(CC) -O1 -o $@ $<

$(BUILD)/gen/fonts_data.h: $(BUILD)/tools/embed assets/JetBrainsMono-Regular.ttf assets/JetBrainsMono-Bold.ttf
	@mkdir -p $(dir $@)
	$(BUILD)/tools/embed $@ ASSET_JetBrainsMono_Regular_ttf=assets/JetBrainsMono-Regular.ttf ASSET_JetBrainsMono_Bold_ttf=assets/JetBrainsMono-Bold.ttf

$(BUILD)/gen/shaders_data.h: $(BUILD)/tools/embed $(SHADERS)
	@mkdir -p $(dir $@)
	$(BUILD)/tools/embed $@ $(foreach f,$(SHADERS),SH_$(shell basename $(f) .glsl | tr a-z A-Z)=$(f))

# The web page's engine: the same C for wasm32, with a small libc of its own. A build machine without
# wasm-ld still gets the desktop app; only `3disle export` then says what is missing.
WASM_SRC = wasm/libc.c engine/json.c engine/spec.c engine/mesh.c engine/model.c engine/paint.c engine/platform.c engine/glb.c gfx/render.c gfx/ui.c app/stage.c app/viewer.c app/web.c
HAVE_WASM := $(shell command -v wasm-ld >/dev/null 2>&1 && command -v clang >/dev/null 2>&1 && echo 1)
WASM_FLAGS = --target=wasm32 -std=c17 -O2 -mbulk-memory -mnontrapping-fptoint -nostdlib -nostdlibinc -ffreestanding -fvisibility=hidden \
  -D_GNU_SOURCE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -Wno-unused-function -Wno-builtin-requires-header \
  -Iwasm/include -Iengine -Igfx -Iapp -Ithird_party -I$(BUILD)/gen
WASM_LINK = -Wl,--no-entry -Wl,--allow-undefined -Wl,--export-dynamic -Wl,--strip-all -Wl,-z,stack-size=4194304 -Wl,--initial-memory=67108864 -Wl,--max-memory=2147483648

$(BUILD)/web/3disle.wasm: $(WASM_SRC) $(BUILD)/gen/fonts_data.h $(BUILD)/gen/shaders_data.h $(wildcard wasm/include/*.h wasm/include/GLES3/*.h engine/*.h gfx/*.h app/*.h)
	@mkdir -p $(dir $@)
ifeq ($(HAVE_WASM),1)
	clang $(WASM_FLAGS) $(WASM_LINK) -o $@ $(WASM_SRC)
else
	@: >$@
endif

$(BUILD)/gen/web_data.h: $(BUILD)/tools/embed $(BUILD)/web/3disle.wasm wasm/glue.js wasm/page.html
	@mkdir -p $(dir $@)
	$(BUILD)/tools/embed $@ ASSET_web_wasm=$(BUILD)/web/3disle.wasm ASSET_web_glue=wasm/glue.js ASSET_web_page=wasm/page.html

$(BUILD)/engine/%.o: engine/%.c $(BUILD)/gen/fonts_data.h $(BUILD)/gen/shaders_data.h $(wildcard engine/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(WARN) $(INC) -c -o $@ $<

$(BUILD)/test_engine: tests/test_engine.c $(ENGINE_OBJ)
	$(CC) $(CFLAGS) $(WARN) $(INC) -o $@ $^ -lm

test: $(BUILD)/test_engine
	$(BUILD)/test_engine

# The whole app, driven headless through real clicks.
test-app: $(BUILD)/3disle
	BIN=$(BUILD)/3disle tests/app.sh

# Exported pages, run in headless Chromium.
test-web: $(BUILD)/3disle
	BIN=$(BUILD)/3disle tests/web.sh

asan:
	$(MAKE) BUILD=build-asan CC=clang CFLAGS="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer" test

clean:
	rm -rf build build-asan

.PHONY: all test test-app test-web asan clean
