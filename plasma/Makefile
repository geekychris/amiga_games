# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>

# ARCH=m68k (default): classic Amiga.  ARCH=ppc: AmigaOS 4 (plasma_os4)
# through the shared layer in ../os4_compat (scripts/build-example-ppc.sh).
ARCH ?= m68k

ifeq ($(ARCH),ppc)
NAME = plasma
SRCS = main.c
include ../os4_compat/os4c.mk
else

CC = m68k-amigaos-gcc
CFLAGS = -noixemul -O0 -m68020 -Wall -I../../amiga-bridge/include
LDFLAGS = -noixemul -g -L../../amiga-bridge -lbridge -lamiga
TARGET = plasma

all: $(TARGET)

$(TARGET): main.o
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *.o $(TARGET)

endif
