# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>

# Terra Strike - Zarch-style landscape shooter for the classic 68k Amiga.
#   make              game + on-Amiga test runner (Docker cross-compiler)
#   make host-test    the same unit tests built and run natively

CC = m68k-amigaos-gcc
AS = vasmm68k_mot
CFLAGS = -noixemul -O2 -m68020 -Wall -I../../amiga-bridge/include
ASFLAGS = -Fhunk -m68000 -no-opt -I/opt/m68k-amigaos/m68k-amigaos/ndk-include/hardware -I/opt/m68k-amigaos/m68k-amigaos/ndk-include/resources
LDFLAGS = -noixemul -g -L../../amiga-bridge -lbridge -lamiga

CORE = ts_math.o terrain.o world.o
GAME_OBJS = main.o render.o models.o sound.o title_image.o tests/test_cases.o $(CORE) ptplayer.o
TEST_OBJS = tests/amiga_test_main.o tests/test_cases.o $(CORE)

all: terra_strike terra_strike_test

terra_strike: $(GAME_OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

terra_strike_test: $(TEST_OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

ptplayer.o: ptplayer.asm
	$(AS) $(ASFLAGS) -o $@ $<

%.o: %.c ts.h title_image.h
	$(CC) $(CFLAGS) -c -o $@ $<

# Regenerate the main screen from the artwork (needs Pillow + numpy on the host)
title:
	python3 tools/make_title.py assets/title_source.png --dither none --out title_image.c

HOSTCC ?= cc
host-test:
	$(HOSTCC) -O2 -Wall -o tests/host_tests tests/host_main.c tests/test_cases.c ts_math.c terrain.c world.c
	./tests/host_tests

clean:
	rm -f *.o tests/*.o terra_strike terra_strike_test tests/host_tests
