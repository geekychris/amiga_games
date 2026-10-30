# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>

# ARCH=m68k (default): classic Amiga.  ARCH=ppc: AmigaOS 4 (lunar_rider_os4)
# through the shared layer in ../os4_compat (scripts/build-example-ppc.sh).
ARCH ?= m68k

ifeq ($(ARCH),ppc)
NAME = lunar_rider
SRCS = main.c game.c draw.c input.c sound.c
include ../os4_compat/os4c.mk
else

CC = m68k-amigaos-gcc
CFLAGS = -noixemul -O2 -m68020 -Wall -I../../amiga-bridge/include
LDFLAGS = -noixemul -g -L../../amiga-bridge -lbridge -lamiga
TARGET = lunar_rider
SRCS = main.c game.c draw.c input.c sound.c
OBJS = $(SRCS:.c=.o)
all: $(TARGET)
$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)
%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<
clean:
	rm -f $(OBJS) $(TARGET)
.PHONY: all clean

endif
