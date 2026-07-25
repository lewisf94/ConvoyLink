# ConvoyLink desktop radar simulator (docs/07 §Simulator, T07).
# Links the real pure-C components straight from firmware/components/ -
# same code, same pixels as the device.

CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -O2 -g
SDL_CFLAGS := $(shell pkg-config --cflags sdl2)
SDL_LIBS   := $(shell pkg-config --libs sdl2)

FW := ../firmware/components

SRCS := src/main.c src/scenario.c src/frame_dump.c src/sim_core.c \
       src/checks.c \
       $(FW)/convoy_geo/convoy_geo.c \
       $(FW)/convoy_proto/convoy_proto.c \
       $(FW)/neighbor_table/neighbor_table.c \
       $(FW)/radar_render/rr_prims.c \
       $(FW)/radar_render/rr_font8x8.c \
       $(FW)/radar_render/rr_screen.c

INC := -Isrc \
      -I$(FW)/convoy_geo/include \
      -I$(FW)/convoy_proto/include \
      -I$(FW)/neighbor_table/include \
      -I$(FW)/radar_render/include \
      $(SDL_CFLAGS)

all: build/convoysim

build/convoysim: $(SRCS) src/*.h | build
	$(CC) $(CFLAGS) $(INC) -o $@ $(SRCS) $(SDL_LIBS) -lm

build:
	mkdir -p build

# Builds, renders the bundled scenario headless, sanity-checks the BMP
# dump, and runs the scripted --check scenarios (T08).
smoke: build/convoysim
	rm -rf build/frames
	./build/convoysim scenarios/overtake.csv --pov 0 --headless --dump build/frames
	@n=$$(ls build/frames/*.bmp 2>/dev/null | wc -l | tr -d ' '); \
	if [ "$$n" -lt 10 ]; then echo "smoke: expected >= 10 BMP frames, got $$n"; exit 1; fi; \
	echo "smoke: $$n frames produced"
	@for f in build/frames/*.bmp; do \
		sz=$$(wc -c < "$$f" | tr -d ' '); \
		if [ "$$sz" != "230454" ]; then \
			echo "smoke: $$f is $$sz bytes, expected 230454 (240x320 24bpp BMP)"; exit 1; \
		fi; \
	done
	@echo "smoke: all frames are the expected 230454 bytes"
	./build/convoysim scenarios/split_rejoin.csv --check
	./build/convoysim scenarios/no_fix_start.csv --check
	./build/convoysim scenarios/convoy_cruise.csv --check

clean:
	rm -rf build

.PHONY: all smoke clean
