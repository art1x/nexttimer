APOSTROPHE     := apostrophe
INCLUDE_DIR    := $(APOSTROPHE)/include
AP_RES_DIR     := $(APOSTROPHE)/res
SRC_DIR        := src
BUILD_DIR      := build
TG5040_TOOLCHAIN := ghcr.io/loveretro/tg5040-toolchain:latest
DOCKER          ?= $(shell command -v docker 2>/dev/null || echo "flatpak-spawn --host docker")

WARN_CFLAGS := -Wall -Wextra -Wno-unused-parameter
SRCS        := $(shell find $(SRC_DIR) -name '*.c')

TMP_PAK  := $(BUILD_DIR)/tg5040/nexttimer.pak
FINAL_PAK := $(BUILD_DIR)/tg5040/Next Timer.pak

.PHONY: linux tg5040 clean

linux:
	@mkdir -p $(BUILD_DIR)/linux
	cc -std=gnu11 -O0 -g $(WARN_CFLAGS) \
		-DPLATFORM_LINUX \
		-I$(INCLUDE_DIR) -I$(SRC_DIR) \
		$(shell pkg-config --cflags sdl2 SDL2_ttf SDL2_image) \
		-o $(BUILD_DIR)/linux/nexttimer \
		$(SRCS) \
		$(shell pkg-config --libs sdl2 SDL2_ttf SDL2_image) \
		-lm -lpthread
	@echo "→ $(BUILD_DIR)/linux/nexttimer"

tg5040:
	@mkdir -p '$(TMP_PAK)'
	$(DOCKER) run --rm \
		-v "$(CURDIR)":/workspace \
		-v "$(shell realpath $(APOSTROPHE))":/workspace/apostrophe \
		$(TG5040_TOOLCHAIN) \
		make -C /workspace -f ports/tg5040/Makefile \
			BUILD_DIR=/workspace/$(TMP_PAK)
	@cp -f launch.sh '$(TMP_PAK)/launch.sh'
	@chmod +x '$(TMP_PAK)/launch.sh'
	@rm -rf '$(FINAL_PAK)'
	@mv '$(TMP_PAK)' '$(FINAL_PAK)'
	@echo "→ $(FINAL_PAK)/"

clean:
	rm -rf $(BUILD_DIR)
