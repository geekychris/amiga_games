# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>

# ARCH=m68k (default): classic Amiga.  ARCH=ppc: AmigaOS 4 (frank_the_frog_os4)
# through the shared layer in ../os4_compat (scripts/build-example-ppc.sh).
ARCH ?= m68k

ifeq ($(ARCH),ppc)
NAME = frank_the_frog
SRCS = main.c gfx.c frank.c lanes.c playfield.c score.c sound.c modplay.c
include ../os4_compat/os4c.mk
else

CC = m68k-amigaos-gcc
CFLAGS = -noixemul -O2 -m68020 -Wall -I../../amiga-bridge/include
LDFLAGS = -noixemul -g -L../../amiga-bridge -lbridge -lamiga
TARGET = frank_the_frog

SRCS = main.c gfx.c frank.c lanes.c playfield.c score.c sound.c modplay.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET) closescr

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

closescr: closescr.c
	$(CC) $(CFLAGS) -o $@ $< -noixemul -L../../amiga-bridge -lbridge -lamiga

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *.o $(TARGET) closescr

endif
