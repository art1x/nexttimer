CROSS_COMPILE ?= aarch64-nextui-linux-gnu-
CC      := $(CROSS_COMPILE)gcc
PLATFORM_ID := tg5040

PKG_CONFIG  := pkg-config
SDL_CFLAGS  := $(shell $(PKG_CONFIG) --cflags sdl2)
SDL_LDFLAGS := $(shell $(PKG_CONFIG) --libs sdl2) -lSDL2_ttf -lSDL2_image

BUILD_DIR ?= /workspace/build/tg5040/nexttimer.pak

# bg.c wird als separates Binary gebaut — nicht in die Hauptanwendung linken
BG_SRC    := /workspace/src/bg.c
MAIN_SRCS := $(filter-out $(BG_SRC), $(shell find /workspace/src -name '*.c'))

CFLAGS := -std=gnu11 -O2 -Wall -Wextra -Wno-unused-parameter \
	-mcpu=cortex-a53 -mtune=cortex-a53 \
	-DPLATFORM_TG5040 \
	-I/workspace/apostrophe/include \
	-I/workspace/src \
	$(SDL_CFLAGS)

LDFLAGS     := $(SDL_LDFLAGS) -lpthread -ldl -lm -lrt
BG_LDFLAGS  := $(shell $(PKG_CONFIG) --libs sdl2) -lpthread -lm -lrt

.PHONY: all clean

all: $(BUILD_DIR)/nexttimer $(BUILD_DIR)/nexttimer-bg

$(BUILD_DIR)/nexttimer: $(MAIN_SRCS)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $(MAIN_SRCS) $(LDFLAGS)

$(BUILD_DIR)/nexttimer-bg: $(BG_SRC)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $(BG_SRC) $(BG_LDFLAGS)

clean:
	rm -rf $(BUILD_DIR)
