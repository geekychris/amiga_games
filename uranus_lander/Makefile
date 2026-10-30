# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>

# ARCH=m68k (default): classic Amiga.  ARCH=ppc: AmigaOS 4 (uranus_lander_os4)
# through the shared layer in ../os4_compat (scripts/build-example-ppc.sh).
ARCH ?= m68k

ifeq ($(ARCH),ppc)
NAME = uranus_lander
# the high score names are copied 3 characters, then ended
EXTRA_CFLAGS = -Wno-stringop-truncation
SRCS = main.c game.c draw.c input.c sound.c
include ../os4_compat/os4c.mk
else

CC = m68k-amigaos-gcc
AS = vasmm68k_mot
CFLAGS = -noixemul -O2 -m68020 -Wall -I../../amiga-bridge/include
ASFLAGS = -Fhunk -m68000 -no-opt -I/opt/m68k-amigaos/m68k-amigaos/ndk-include -I/opt/m68k-amigaos/m68k-amigaos/ndk-include/include_h
LDFLAGS = -noixemul -g -L../../amiga-bridge -lbridge -lamiga
TARGET = uranus_lander
SRCS = main.c game.c draw.c input.c sound.c
OBJS = $(SRCS:.c=.o) ptplayer.o
all: $(TARGET)
$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)
%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<
ptplayer.o: ptplayer.asm
	$(AS) $(ASFLAGS) -o $@ $<
clean:
	rm -f $(OBJS) $(TARGET)
.PHONY: all clean

endif
