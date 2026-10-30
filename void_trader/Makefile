# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>

# ARCH=m68k (default) — classic AmigaOS 3.x on 680x0
# ARCH=ppc            — AmigaOS 4.1 on PowerPC (sam460ex / QEMU)
# Build for PPC via: scripts/build-example-ppc.sh void_trader
#   (void_trader_os4, through the shared layer in ../os4_compat, with sound)
ARCH ?= m68k

ifeq ($(ARCH),ppc)
NAME = void_trader
SRCS = main.c engine3d.c models.c combat.c scanner.c trade.c modplay.c sfx.c
include ../os4_compat/os4c.mk
else
  CC = m68k-amigaos-gcc
  CFLAGS  = -noixemul -O2 -m68020 -Wall -Wno-unused-function \
            -I../../amiga-bridge/include
  LDFLAGS = -noixemul -m68020 -L../../amiga-bridge -lbridge -lamiga
  AUDIO_SRCS = modplay.c sfx.c

TARGET   = void_trader
SRCS     = main.c engine3d.c models.c combat.c scanner.c trade.c $(AUDIO_SRCS)
OBJS     = $(SRCS:.c=.o)
vpath %.c . ../../amiga-bridge/client

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

main.o:     main.c engine3d.h models.h combat.h scanner.h gamemode.h trade.h modplay.h sfx.h
engine3d.o: engine3d.c engine3d.h
models.o:   models.c engine3d.h models.h
combat.o:   combat.c combat.h engine3d.h sfx.h
scanner.o:  scanner.c scanner.h engine3d.h
trade.o:    trade.c trade.h
modplay.o:  modplay.c modplay.h
sfx.o:      sfx.c sfx.h modplay.h
arch_audio_stub_ppc.o: arch_audio_stub_ppc.c modplay.h sfx.h

clean:
	rm -f *.o $(TARGET)

.PHONY: all clean

endif
