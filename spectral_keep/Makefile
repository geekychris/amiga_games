# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>

# spectral_keep — the isometric flip-screen adventure, ported from the 3DO
# version.
#   ARCH=ppc (default)  AmigaOS 4.1:  scripts/build-example-ppc.sh spectral_keep
#   ARCH=m68k           AmigaOS 3.x, 68020+, RTG or AGA:
#                       scripts/build-example-68k.sh spectral_keep
ARCH ?= ppc

GAME   = room.c world.c levels.c vox.c font.c textures.c scene.c game.c
BRIDGE = ../../amiga-bridge/client/bridge_client.c

ifeq ($(ARCH),m68k)
  CC      = m68k-amigaos-gcc
  CFLAGS  = -noixemul -O2 -m68020 -Wall -Wno-unused-function -Wno-pointer-sign \
            -I../../amiga-bridge/include -DSCENE_RGB15
  # no -lamiga: its sprintf (16-bit %d) would replace libnix's
  LDFLAGS = -noixemul -L../../amiga-bridge -lbridge
  TARGET  = spectral_keep_68k
  SRCS    = main_68k.c $(GAME) sound_paula.c amiga68k.c paula68k.c
  OBJDIR  = obj68k
else
  CC      = ppc-amigaos-gcc
  CFLAGS  = -mcrt=newlib -O2 -mcpu=440fp -Wall -Wno-unused-function \
            -I../../amiga-bridge/include \
            -D__PPC__ -D__USE_INLINE__ -D__USE_OLD_TIMEVAL__
  LDFLAGS = -mcrt=newlib -lauto
  TARGET  = spectral_keep
  # the bridge client is compiled in, so the shared ../../amiga-bridge/libbridge.a
  # (a 68k build) is left alone
  SRCS    = main_os4.c $(GAME) sound_ahi.c os4_display.c $(BRIDGE)
  OBJDIR  = objppc
endif

OBJS = $(addprefix $(OBJDIR)/,$(notdir $(SRCS:.c=.o)))
vpath %.c . ../../amiga-bridge/client

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)

$(OBJDIR)/%.o: %.c keep.h | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR):
	mkdir -p $@

clean:
	rm -rf obj68k objppc spectral_keep spectral_keep_68k

.PHONY: all clean
