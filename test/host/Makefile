# Host unit tests for ConvoyLink pure-C components.
#
# Convention: every test_<component>.c here becomes one binary. Declare the
# component sources/includes it needs via SRCS_test_<component> /
# INC_test_<component> below (see convoy_proto as the pattern).
# `make` (or `make test`) builds and runs everything; non-zero exit on failure.

CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined \
           -fno-sanitize-recover=all
LDLIBS  ?= -lm
FW      := ../../firmware/components

# --- per-test source lists -------------------------------------------------
SRCS_test_convoy_proto := $(FW)/convoy_proto/convoy_proto.c
INC_test_convoy_proto  := -I$(FW)/convoy_proto/include

SRCS_test_convoy_geo := $(FW)/convoy_geo/convoy_geo.c
INC_test_convoy_geo  := -I$(FW)/convoy_geo/include -I$(FW)/convoy_proto/include

SRCS_test_adpcm := $(FW)/adpcm/adpcm.c
INC_test_adpcm  := -I$(FW)/adpcm/include -I$(FW)/convoy_proto/include

# voice_pipe links the codec it frames (adpcm) + shared cfg:
SRCS_test_voice_pipe := $(FW)/voice_pipe/voice_pipe.c $(FW)/adpcm/adpcm.c
INC_test_voice_pipe  := -I$(FW)/voice_pipe/include -I$(FW)/adpcm/include \
                        -I$(FW)/convoy_proto/include

SRCS_test_nmea := $(FW)/nmea/nmea.c
INC_test_nmea  := -I$(FW)/nmea/include -I$(FW)/convoy_proto/include

SRCS_test_neighbor_table := $(FW)/neighbor_table/neighbor_table.c $(FW)/convoy_proto/convoy_proto.c
INC_test_neighbor_table  := -I$(FW)/neighbor_table/include -I$(FW)/convoy_proto/include

SRCS_test_radar_render := $(wildcard $(FW)/radar_render/*.c)
INC_test_radar_render  := -I$(FW)/radar_render/include -I$(FW)/convoy_proto/include \
                          -I$(FW)/convoy_geo/include -I$(FW)/neighbor_table/include
# radar_render's screen composition also links geo + neighbor_table:
SRCS_test_radar_render += $(FW)/convoy_geo/convoy_geo.c $(FW)/neighbor_table/neighbor_table.c
# ---------------------------------------------------------------------------

TEST_SRCS := $(wildcard test_*.c)
TEST_BINS := $(addprefix build/,$(TEST_SRCS:.c=))

all: test

build/%: %.c tinytest.h FORCE | build
	$(CC) $(CFLAGS) $(INC_$*) -I. -o $@ $< $(SRCS_$*) $(LDLIBS)

test: $(TEST_BINS)
	@fail=0; \
	for t in $(TEST_BINS); do echo "== $$t"; ./$$t || fail=1; done; \
	if [ $$fail -eq 0 ]; then echo "ALL HOST TESTS PASSED"; \
	else echo "HOST TESTS FAILED"; exit 1; fi

build:
	mkdir -p build

clean:
	rm -rf build

FORCE:
.PHONY: all test clean FORCE
