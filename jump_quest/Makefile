# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>

# ARCH=m68k (default): classic Amiga.  ARCH=ppc: AmigaOS 4 (jump_quest_os4)
# through the shared layer in ../os4_compat (scripts/build-example-ppc.sh).
ARCH ?= m68k

ifeq ($(ARCH),ppc)
NAME = jump_quest
SRCS = src/main.c src/gfx.c src/input.c src/player.c src/level.c src/enemy.c src/items.c src/hud.c src/title.c src/sound.c
include ../os4_compat/os4c.mk
else

CC = m68k-amigaos-gcc
CFLAGS = -noixemul -O2 -m68020 -Wall -I../../amiga-bridge/include
LDFLAGS = -noixemul -g -L../../amiga-bridge -lbridge -lamiga

TARGET = jump_quest
SRCS = src/main.c src/gfx.c src/input.c src/player.c src/level.c \
       src/enemy.c src/items.c src/hud.c src/title.c src/sound.c

OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)

src/%.o: src/%.c src/game.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJS)

.PHONY: all clean

endif
