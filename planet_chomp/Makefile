# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>

# planet_chomp — Pac-Man on a tiny sphere, ported from the 3DO version.
#   ARCH=ppc (default)  AmigaOS 4.1, OpenGL through OSMesa
#                       (../../third_party/mesa-os4/build.sh first):
#                       scripts/build-example-ppc.sh planet_chomp
#   ARCH=m68k           AmigaOS 3.x, 68020+, RTG or AGA, software cels:
#                       scripts/build-example-68k.sh planet_chomp
#
# -mcpu=440fp, not 440: with this GCC 11, plain 440 means soft float,
# which won't link against the hard-float libOSMesa / newlib libm.
ARCH ?= ppc

GAME   = game.c maze.c sprites.c
BRIDGE = ../../amiga-bridge/client/bridge_client.c

ifeq ($(ARCH),m68k)
  CC      = m68k-amigaos-gcc
  CFLAGS  = -noixemul -O2 -m68020 -Wall -Wno-unused-function -Wno-pointer-sign \
            -I../../amiga-bridge/include
  # no -lamiga: its sprintf (16-bit %d) would replace libnix's
  LDFLAGS = -noixemul -L../../amiga-bridge -lbridge
  TARGET  = planet_chomp_68k
  SRCS    = main_68k.c $(GAME) render_cel.c cels_soft.c softcel.c sfx_paula.c amiga68k.c paula68k.c
  OBJDIR  = obj68k
else
  MESA    = ../../third_party/mesa-os4/out
  CC      = ppc-amigaos-gcc
  CFLAGS  = -mcrt=newlib -O2 -mcpu=440fp -Wall -Wno-unused-function \
            -I$(MESA)/include -I../../amiga-bridge/include \
            -D__PPC__ -D__USE_INLINE__ -D__USE_OLD_TIMEVAL__
  LDFLAGS = -mcrt=newlib -L$(MESA)/lib -lOSMesa -lm -lauto
  TARGET  = planet_chomp
  # the bridge client is compiled in, so the shared ../../amiga-bridge/libbridge.a
  # (a 68k build) is left alone
  SRCS    = main_os4.c $(GAME) sfx_ahi.c gl_render.c os4_display.c $(BRIDGE)
  OBJDIR  = objppc
endif

OBJS = $(addprefix $(OBJDIR)/,$(notdir $(SRCS:.c=.o)))
vpath %.c . ../../amiga-bridge/client

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)

$(OBJDIR)/%.o: %.c pc.h game.h sfx.h sprites.h | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR):
	mkdir -p $@

clean:
	rm -rf obj68k objppc planet_chomp planet_chomp_68k

.PHONY: all clean
