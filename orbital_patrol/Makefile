# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>

# ARCH=m68k (default): classic Amiga.  ARCH=ppc: AmigaOS 4 (orbital_patrol_os4)
# through the shared layer in ../os4_compat (scripts/build-example-ppc.sh).
ARCH ?= m68k

ifeq ($(ARCH),ppc)
NAME = orbital_patrol
SRCS = main.c game.c draw.c input.c sound.c score.c
include ../os4_compat/os4c.mk
else

CC = m68k-amigaos-gcc
AS = vasmm68k_mot
CFLAGS = -noixemul -O2 -m68020 -Wall -I../../amiga-bridge/include
ASFLAGS = -Fhunk -m68000 -no-opt -quiet
LDFLAGS = -noixemul -g -L../../amiga-bridge -lbridge -lamiga

TARGET = orbital_patrol

SRCS = main.c game.c draw.c input.c sound.c score.c
OBJS = $(SRCS:.c=.o) ptplayer.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

ptplayer.o: ptplayer.asm
	$(AS) $(ASFLAGS) -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *.o $(TARGET)

.PHONY: all clean

endif
