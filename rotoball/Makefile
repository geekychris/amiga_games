# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>

# ARCH=m68k (default): classic Amiga.  ARCH=ppc: AmigaOS 4 (rotoball_os4)
# through the shared layer in ../os4_compat (scripts/build-example-ppc.sh).
ARCH ?= m68k

ifeq ($(ARCH),ppc)
NAME = rotoball
SRCS = main.c pitch.c sound.c
include ../os4_compat/os4c.mk
else

CC = m68k-amigaos-gcc
CFLAGS = -noixemul -O2 -m68020 -Wall -Wno-unused-function -I../../amiga-bridge/include
LDFLAGS = -noixemul -L../../amiga-bridge -lbridge -lamiga
TARGET = rotoball

SRCS = main.c pitch.c sound.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

main.o:  main.c rotoball.h sound.h
pitch.o: pitch.c rotoball.h
sound.o: sound.c sound.h

clean:
	rm -f *.o $(TARGET)

endif
