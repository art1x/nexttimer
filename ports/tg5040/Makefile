CROSS_COMPILE ?= aarch64-nextui-linux-gnu-
CC      := $(CROSS_COMPILE)gcc
PLATFORM_ID := tg5040

PKG_CONFIG  := pkg-config
SDL_CFLAGS  := $(shell $(PKG_CONFIG) --cflags sdl2)
SDL_LDFLAGS := $(shell $(PKG_CONFIG) --libs sdl2) -lSDL2_ttf -lSDL2_image

BUILD_DIR ?= /workspace/build/tg5040/nexttimer.pak
SRCS      := $(shell find /workspace/src -name '*.c')

CFLAGS := -std=gnu11 -O2 -Wall -Wextra -Wno-unused-parameter \
	-mcpu=cortex-a53 -mtune=cortex-a53 \
	-DPLATFORM_TG5040 \
	-I/workspace/apostrophe/include \
	-I/workspace/src \
	$(SDL_CFLAGS)

LDFLAGS := $(SDL_LDFLAGS) -lpthread -ldl -lm

.PHONY: all clean

all: $(BUILD_DIR)/nexttimer

$(BUILD_DIR)/nexttimer: $(SRCS)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $(SRCS) $(LDFLAGS)

clean:
	rm -rf $(BUILD_DIR)
