# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>

# Terra Strike - Zarch-style landscape shooter.
#   ARCH=m68k (default)  classic Amiga, 68020+:  make
#                        game + on-Amiga test runner (Docker cross-compiler)
#   ARCH=ppc             AmigaOS 4.1:  scripts/build-example-ppc.sh terra_strike
#                        (terra_strike_os4)
#   make host-test       the same unit tests built and run natively
ARCH ?= m68k

CORE = ts_math.c terrain.c world.c
GAME = game.c render.c models.c sound.c title_image.c tests/test_cases.c $(CORE)
HEADERS = ts.h title_image.h render.h game.h sound.h models.h

ifeq ($(ARCH),ppc)

CC = ppc-amigaos-gcc
CFLAGS = -mcrt=newlib -O2 -mcpu=440fp -Wall -I../../amiga-bridge/include \
         -D__PPC__ -D__USE_INLINE__ -D__USE_OLD_TIMEVAL__
LDFLAGS = -mcrt=newlib -lauto
OBJDIR = objppc
# the bridge client is compiled in, so the shared ../../amiga-bridge/libbridge.a
# (a 68k build) is left alone
SRCS = main_os4.c $(GAME) sound_ahi.c modplay.c os4_display.c ../../amiga-bridge/client/bridge_client.c
OBJS = $(addprefix $(OBJDIR)/,$(notdir $(SRCS:.c=.o)))
vpath %.c . tests ../../amiga-bridge/client

all: terra_strike_os4

terra_strike_os4: $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)

$(OBJDIR)/%.o: %.c $(HEADERS) modplay.h os4_display.h | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR):
	mkdir -p $@

else

CC = m68k-amigaos-gcc
AS = vasmm68k_mot
CFLAGS = -noixemul -O2 -m68020 -Wall -I../../amiga-bridge/include
ASFLAGS = -Fhunk -m68000 -no-opt -I/opt/m68k-amigaos/m68k-amigaos/ndk-include/hardware -I/opt/m68k-amigaos/m68k-amigaos/ndk-include/resources
LDFLAGS = -noixemul -g -L../../amiga-bridge -lbridge -lamiga

GAME_OBJS = main.o $(GAME:.c=.o) ptplayer.o
TEST_OBJS = tests/amiga_test_main.o tests/test_cases.o $(CORE:.c=.o)

all: terra_strike terra_strike_test

terra_strike: $(GAME_OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

terra_strike_test: $(TEST_OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

ptplayer.o: ptplayer.asm
	$(AS) $(ASFLAGS) -o $@ $<

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c -o $@ $<

endif

# Regenerate the main screen from the artwork (needs Pillow + numpy on the host)
title:
	python3 tools/make_title.py assets/title_source.png --dither none --out title_image.c

HOSTCC ?= cc
host-test:
	$(HOSTCC) -O2 -Wall -o tests/host_tests tests/host_main.c tests/test_cases.c ts_math.c terrain.c world.c
	./tests/host_tests

clean:
	rm -rf *.o tests/*.o objppc terra_strike terra_strike_test terra_strike_os4 tests/host_tests

.PHONY: all clean title host-test
