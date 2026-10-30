# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>

# ARCH=m68k (default): the classic Amiga demo (assembly, takes over the
# machine).  ARCH=ppc: AmigaOS 4 (starfield_os4), the same demo in C
# (main_os4.c) on the shared layer in ../os4_compat.
ARCH ?= m68k

ifeq ($(ARCH),ppc)
NAME = starfield
SRCS = main_os4.c
include ../os4_compat/os4c.mk
else

CC = m68k-amigaos-gcc
AS = vasmm68k_mot
CFLAGS = -noixemul -O2 -m68020 -Wall -I../../amiga-bridge/include
ASFLAGS = -Fhunk -m68020 -no-opt -quiet
LDFLAGS = -noixemul -g -L../../amiga-bridge -lbridge -lamiga

TARGET = starfield

C_OBJS = startup.o
ASM_OBJS = starfield.o ptplayer.o

OBJS = $(C_OBJS) $(ASM_OBJS)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

starfield.o: starfield.asm
	$(AS) $(ASFLAGS) -o $@ $<

ptplayer.o: ptplayer.asm
	$(AS) -Fhunk -m68000 -no-opt -quiet -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *.o $(TARGET)

endif
