# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>

# spectral_keep — the isometric flip-screen adventure, ported from the 3DO
# version. AmigaOS 4.1 PPC only. Build via:
#   scripts/build-example-ppc.sh spectral_keep
ARCH ?= ppc

CC      = ppc-amigaos-gcc
CFLAGS  = -mcrt=newlib -O2 -mcpu=440fp -Wall -Wno-unused-function \
          -I../../amiga-bridge/include \
          -D__PPC__ -D__USE_INLINE__ -D__USE_OLD_TIMEVAL__
LDFLAGS = -mcrt=newlib -lauto

TARGET = spectral_keep
SRCS   = main_os4.c game.c room.c world.c levels.c vox.c font.c textures.c scene.c sound_ahi.c os4_display.c
# the bridge client is compiled in, so the shared ../../amiga-bridge/libbridge.a
# (which may be a 68k build) is left alone
BRIDGE = ../../amiga-bridge/client/bridge_client.c
OBJS   = $(SRCS:.c=.o) bridge_client.o

all: $(TARGET)

ifneq ($(ARCH),ppc)
$(TARGET):
	@echo "spectral_keep is AmigaOS 4 (PPC) only - build with ARCH=ppc"
else
$(TARGET): $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)
endif

bridge_client.o: $(BRIDGE)
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.c keep.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *.o $(TARGET)

.PHONY: all clean
