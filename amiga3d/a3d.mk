# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
#
# a3d.mk - building a game on the shared engine, for both Amigas.
# A game's Makefile sets:
#   NAME        the program (NAME on the classic Amiga, NAME_os4 on AmigaOS 4)
#   CORE        its pure sources (no Amiga headers; tested on the host too)
#   GAME        its other sources (rendering, the bridge, sound)
#   TESTS       its test cases (they define run_tests(check))
#   HEADERS     what everything depends on
# then: include ../amiga3d/a3d.mk
#
#   make                ARCH=m68k (default): NAME and NAME_test (Docker:
#                       scripts/build-example-68k.sh <game>)
#   make ARCH=ppc       NAME_os4 (scripts/build-example-ppc.sh <game>)
#   make host-test      the tests, built and run natively

A3D     = ../amiga3d
BRIDGE  = ../../amiga-bridge
ARCH   ?= m68k
ENGINE  = $(A3D)/a3d_math.c $(A3D)/a3d_fix.c $(A3D)/a3d_draw.c $(A3D)/a3d_sound.c
A3D_HEADERS = $(A3D)/a3d.h $(A3D)/a3d_fix.h $(A3D)/a3d_draw.h $(A3D)/a3d_font16.h $(A3D)/a3d_plat.h $(A3D)/a3d_sound.h
vpath %.c . tests $(A3D) $(BRIDGE)/client

ifeq ($(ARCH),ppc)

CC      = ppc-amigaos-gcc
CFLAGS  = -mcrt=newlib -O2 -mcpu=440fp -Wall -I. -I$(A3D) -I$(BRIDGE)/include \
          -D__PPC__ -D__USE_INLINE__ -D__USE_OLD_TIMEVAL__
LDFLAGS = -mcrt=newlib -lauto
OBJDIR  = objppc
SRCS    = $(GAME) $(CORE) $(ENGINE) main_os4.c os4_display.c sound_ahi.c modplay.c bridge_client.c
OBJS    = $(addprefix $(OBJDIR)/,$(notdir $(SRCS:.c=.o)))

all: $(NAME)_os4

$(NAME)_os4: $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)

$(OBJDIR)/%.o: %.c $(HEADERS) $(A3D_HEADERS) | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR):
	mkdir -p $@

else

# -m68020-60: runs on a 68020 up to a 68060, avoiding the 64-bit multiply
# and divide the 68060 doesn't have (it traps them to 68060.library's
# emulation, thousands of times slower; the fixed-point games use them a lot)
CC      = m68k-amigaos-gcc
AS      = vasmm68k_mot
CFLAGS  = -noixemul -O2 -m68020-60 -Wall -I. -I$(A3D) -I$(BRIDGE)/include
ASFLAGS = -Fhunk -m68000 -no-opt -I/opt/m68k-amigaos/m68k-amigaos/ndk-include/hardware -I/opt/m68k-amigaos/m68k-amigaos/ndk-include/resources
LDFLAGS = -noixemul -g -L$(BRIDGE) -lbridge -lamiga
OBJDIR  = obj68k
OBJS    = $(addprefix $(OBJDIR)/,$(notdir $(GAME:.c=.o) $(CORE:.c=.o) $(ENGINE:.c=.o) main_68k.o)) $(OBJDIR)/ptplayer.o
TOBJS   = $(addprefix $(OBJDIR)/,$(notdir $(CORE:.c=.o) $(TESTS:.c=.o))) $(OBJDIR)/a3d_math.o $(OBJDIR)/a3d_fix.o $(OBJDIR)/test_amiga.o

all: $(NAME) $(NAME)_test

$(NAME): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

$(NAME)_test: $(TOBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

$(OBJDIR)/ptplayer.o: $(A3D)/ptplayer.asm | $(OBJDIR)
	$(AS) $(ASFLAGS) -o $@ $<

$(OBJDIR)/%.o: %.c $(HEADERS) $(A3D_HEADERS) | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR):
	mkdir -p $@

endif

HOSTCC ?= cc
host-test:
	$(HOSTCC) -O2 -Wall -I. -I$(A3D) -o tests/host_tests $(A3D)/test_host.c $(TESTS) $(CORE) $(A3D)/a3d_math.c $(A3D)/a3d_fix.c
	./tests/host_tests

clean:
	rm -rf obj68k objppc $(NAME) $(NAME)_test $(NAME)_os4 tests/host_tests

.PHONY: all clean host-test
