# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>

# rolling_steel — the marble racer, ported from the 3DO version.
#   ARCH=ppc (default)  AmigaOS 4.1, OpenGL through OSMesa
#                       (../../third_party/mesa-os4/build.sh first):
#                       scripts/build-example-ppc.sh rolling_steel
#   ARCH=m68k           AmigaOS 3.x, 68020+, RTG or AGA, software rasteriser
#                       with a depth buffer (glcels_soft.c):
#                       scripts/build-example-68k.sh rolling_steel
#
# -mcpu=440fp, not 440: with this GCC 11, plain 440 means soft float,
# which won't link against the hard-float libOSMesa / newlib libm.
ARCH ?= ppc

GAME   = game.c phys.c course.c
BRIDGE = ../../amiga-bridge/client/bridge_client.c

ifeq ($(ARCH),m68k)
  CC      = m68k-amigaos-gcc
  CFLAGS  = -noixemul -O2 -m68020 -Wall -Wno-unused-function -Wno-pointer-sign \
            -I../../amiga-bridge/include
  # no -lamiga: its sprintf (16-bit %d) would replace libnix's
  LDFLAGS = -noixemul -L../../amiga-bridge -lbridge
  TARGET  = rolling_steel_68k
  SRCS    = main_68k.c $(GAME) render.c glcels_soft.c softcel.c sound_paula.c amiga68k.c paula68k.c
  OBJDIR  = obj68k
else
  MESA    = ../../third_party/mesa-os4/out
  CC      = ppc-amigaos-gcc
  CFLAGS  = -mcrt=newlib -O2 -mcpu=440fp -Wall -Wno-unused-function \
            -I$(MESA)/include -I../../amiga-bridge/include \
            -D__PPC__ -D__USE_INLINE__ -D__USE_OLD_TIMEVAL__
  LDFLAGS = -mcrt=newlib -L$(MESA)/lib -lOSMesa -lm -lauto
  TARGET  = rolling_steel
  # the bridge client is compiled in, so the shared ../../amiga-bridge/libbridge.a
  # (a 68k build) is left alone
  SRCS    = main_os4.c $(GAME) render.c glcels.c sound_ahi.c os4_display.c $(BRIDGE)
  OBJDIR  = objppc
endif

OBJS = $(addprefix $(OBJDIR)/,$(notdir $(SRCS:.c=.o)))
vpath %.c . ../../amiga-bridge/client

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)

# game.c (the 3DO code, unchanged) has a static kill(): newlib declares one too
$(OBJDIR)/game.o: game.c game.h rs.h game_os4.h | $(OBJDIR)
	$(CC) $(CFLAGS) -include game_os4.h -c -o $@ $<

$(OBJDIR)/%.o: %.c rs.h | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR):
	mkdir -p $@

clean:
	rm -rf obj68k objppc rolling_steel rolling_steel_68k

.PHONY: all clean
