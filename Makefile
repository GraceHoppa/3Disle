# 3Disle: `make` builds the app, `make test` runs the checks, `make asan` the checks under sanitizers.
CC ?= cc
BUILD ?= build
CFLAGS ?= -O2 -g
WARN = -std=c17 -D_GNU_SOURCE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers
INC = -Iengine -Igfx -Iapp -Ithird_party -I$(BUILD)/gen
SDL_CFLAGS := $(shell pkg-config --cflags sdl3 2>/dev/null)
SDL_LIBS := $(shell pkg-config --libs sdl3 2>/dev/null)

ENGINE = engine/json.c engine/spec.c engine/mesh.c engine/model.c engine/paint.c engine/platform.c
ENGINE_OBJ = $(ENGINE:%.c=$(BUILD)/%.o)
GEN = $(BUILD)/gen/fonts_data.h $(BUILD)/gen/shaders_data.h
SHADERS = $(wildcard gfx/shaders/*.glsl)

GFX = gfx/render.c
APP = app/main.c app/headless.c
GFX_OBJ = $(GFX:%.c=$(BUILD)/%.o)
APP_OBJ = $(APP:%.c=$(BUILD)/%.o)

all: $(BUILD)/3disle

$(BUILD)/3disle: $(ENGINE_OBJ) $(GFX_OBJ) $(APP_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ -lEGL -lGLESv2 -lm

$(BUILD)/gfx/%.o: gfx/%.c $(GEN) $(wildcard engine/*.h gfx/*.h)
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

$(BUILD)/engine/%.o: engine/%.c $(GEN) $(wildcard engine/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(WARN) $(INC) -c -o $@ $<

$(BUILD)/test_engine: tests/test_engine.c $(ENGINE_OBJ)
	$(CC) $(CFLAGS) $(WARN) $(INC) -o $@ $^ -lm

test: $(BUILD)/test_engine
	$(BUILD)/test_engine

asan:
	$(MAKE) BUILD=build-asan CC=clang CFLAGS="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer" test

clean:
	rm -rf build build-asan

.PHONY: all test asan clean
