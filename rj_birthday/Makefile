# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>

# ARCH=m68k (default): classic Amiga.  ARCH=ppc: AmigaOS 4 (rj_birthday_os4)
# through the shared layer in ../os4_compat (scripts/build-example-ppc.sh).
ARCH ?= m68k

ifeq ($(ARCH),ppc)
NAME = rj_birthday
# numbuf holds the guest numbers (1 to 99)
EXTRA_CFLAGS = -Wno-format-overflow
SRCS = main.c game.c rooms.c draw.c input.c sound.c
include ../os4_compat/os4c.mk
else

CC = m68k-amigaos-gcc
AS = vasmm68k_mot
CFLAGS = -noixemul -O2 -m68020 -Wall -I../../amiga-bridge/include
ASFLAGS = -Fhunk -m68000 -no-opt -I/opt/m68k-amigaos/m68k-amigaos/ndk-include/hardware -I/opt/m68k-amigaos/m68k-amigaos/ndk-include/resources
LDFLAGS = -noixemul -g -L../../amiga-bridge -lbridge -lamiga

TARGET = rj_birthday

SRCS = main.c game.c rooms.c draw.c input.c sound.c
OBJS = $(SRCS:.c=.o) ptplayer.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

ptplayer.o: ptplayer.asm
	$(AS) $(ASFLAGS) -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean

endif
