# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins

# ARCH=m68k (default) — classic AmigaOS 3.x on 680x0 (FS-UAE / AmiKit)
# ARCH=ppc            — AmigaOS 4.1 on PowerPC (sam460ex / QEMU)
# Build for PPC via: scripts/build-example-ppc.sh fractalus
#
# On PPC we swap modplay.c (direct Paula chip register access, no such
# hardware on sam460ex) for arch_audio_stub_ppc.cpp which no-ops the
# audio API. Game runs silent on OS4. sfx.cpp stays — it only touches
# Paula indirectly via modplay_sfx() which is stubbed.
#
# render.cpp still uses direct-to-bitplane writes on 68k for speed; on
# PPC those code paths are gated out with #ifdef __PPC__ and fall back
# to graphics.library RectFill (slower but works without chip RAM).
ARCH ?= m68k

ifeq ($(ARCH),ppc)
  CXX      = ppc-amigaos-g++
  CC       = ppc-amigaos-gcc
  CXXFLAGS = -mcrt=newlib -O2 -mcpu=440 -Wall -Wno-unused-function \
             -fno-exceptions -fno-rtti \
             -I../../amiga-bridge/include \
             -D__PPC__ -D__USE_INLINE__ -D__USE_OLD_TIMEVAL__
  CFLAGS   = -mcrt=newlib -O2 -mcpu=440 -Wall -Wno-unused-function \
             -I../../amiga-bridge/include \
             -D__PPC__ -D__USE_INLINE__ -D__USE_OLD_TIMEVAL__
  LDFLAGS  = -mcrt=newlib -L../../amiga-bridge -lbridge -lauto
  AUDIO_SRCS = arch_audio_stub_ppc.cpp
else
  CXX      = m68k-amigaos-g++
  CC       = m68k-amigaos-gcc
  CXXFLAGS = -noixemul -O2 -m68020 -Wall -Wno-unused-function \
             -fno-exceptions -fno-rtti \
             -I../../amiga-bridge/include
  CFLAGS   = -noixemul -O2 -m68020 -Wall -Wno-unused-function \
             -I../../amiga-bridge/include
  LDFLAGS  = -noixemul -m68020 -L../../amiga-bridge -lbridge -lamiga
  AUDIO_SRCS = modplay.c
endif

TARGET   = fractalus
CPPSRCS  = main.cpp terrain.cpp render.cpp game.cpp pilots.cpp combat.cpp sfx.cpp
CSRCS    =

# Audio impl swapped per-arch (see block above). arch_audio_stub_ppc.cpp
# is a .cpp so it compiles under both toolchains without a second rule.
AUDIO_CPP = $(filter %.cpp,$(AUDIO_SRCS))
AUDIO_C   = $(filter %.c,$(AUDIO_SRCS))
CPPSRCS  += $(AUDIO_CPP)
CSRCS    += $(AUDIO_C)

OBJS     = $(CPPSRCS:.cpp=.o) $(CSRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) -o $@ $^ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Header dependencies — small enough to just enumerate. Prevents the
# "changed a #define, didn't get picked up" trap we hit during Phase 4.
main.o:    main.cpp fixed.h terrain.h render.h game.h pilots.h combat.h
terrain.o: terrain.cpp terrain.h fixed.h
render.o:  render.cpp render.h terrain.h game.h pilots.h combat.h fixed.h
game.o:    game.cpp game.h terrain.h pilots.h combat.h fixed.h
pilots.o:  pilots.cpp pilots.h terrain.h fixed.h
combat.o:  combat.cpp combat.h game.h sfx.h fixed.h
sfx.o:     sfx.cpp sfx.h modplay.h
arch_audio_stub_ppc.o: arch_audio_stub_ppc.cpp modplay.h

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
