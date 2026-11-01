# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>

# Boing Twist - a twisty puzzle on the Boing ball, for AmigaOS 4.1 (PowerPC),
# drawn in 3D with OpenGL through software Mesa (OSMesa):
#   ../../third_party/mesa-os4/build.sh        once, for libOSMesa
#   scripts/build-example-ppc.sh boing_twist   boing_twist
#   make host-test                             the puzzle core's tests on the host
#   make ubsan                                 the same under UBSan and ASan
#   make vectors                               regenerate tests/vectors.h
#   make data                                  images/ and themes/ -> data/ (needs Pillow)
#
# -mcpu=powerpc, never 440/440fp: GCC then inlines string functions with
# dlmzb, which only the 440/460 have (QEMU runs it; an X5000 or A1222 crashes).
HOSTCC ?= cc
MESA    = ../../third_party/mesa-os4/out
CC      = ppc-amigaos-gcc
CFLAGS  = -mcrt=newlib -O2 -mcpu=powerpc -Wall -Wno-unused-function \
          -I$(MESA)/include -I../../amiga-bridge/include \
          -D__PPC__ -D__USE_INLINE__ -D__USE_OLD_TIMEVAL__
LDFLAGS = -mcrt=newlib -L$(MESA)/lib -lOSMesa -lm -lauto
TARGET  = boing_twist
SRCS    = main_os4.c bt_core.c render.c hud.c os4_display.c sound_ahi.c ../../amiga-bridge/client/bridge_client.c
OBJDIR  = objppc
OBJS    = $(addprefix $(OBJDIR)/,$(notdir $(SRCS:.c=.o)))
HEADERS = bt_core.h render.h hud.h os4_display.h sound_ahi.h tests/vectors.h
vpath %.c . ../../amiga-bridge/client

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)
	@if command -v ppc-amigaos-objdump >/dev/null; then \
	  n=$$(ppc-amigaos-objdump -d $@ | grep -c dlmzb || true); \
	  echo "dlmzb instructions: $$n"; test "$$n" = 0; fi

$(OBJDIR)/%.o: %.c $(HEADERS) | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR):
	mkdir -p $@

host-test:
	$(HOSTCC) -O2 -Wall -Wextra -o tests/host_tests tests/test_core.c bt_core.c
	./tests/host_tests

ubsan:
	$(HOSTCC) -g -O1 -Wall -fsanitize=undefined,address -fno-sanitize-recover=undefined \
	    -o tests/host_tests_san tests/test_core.c bt_core.c
	./tests/host_tests_san

vectors:
	$(HOSTCC) -O2 -Wall -o tests/host_tests tests/test_core.c bt_core.c
	./tests/host_tests --vectors > tests/vectors.h.new && mv tests/vectors.h.new tests/vectors.h
	cat tests/vectors.h

data:
	python3 tools/make_texture.py --all

clean:
	rm -rf $(OBJDIR) $(TARGET) tests/host_tests tests/host_tests_san

.PHONY: all clean host-test ubsan vectors data
