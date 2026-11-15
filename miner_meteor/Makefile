# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>

# miner-meteor — clean-room single-screen tile platformer.
#
# ARCH=m68k (default) — classic AmigaOS 3.x on 680x0
# ARCH=ppc            — AmigaOS 4.1 on PowerPC (sam460ex / QEMU)
#
# Build for PPC via: scripts/build-example-ppc.sh miner_meteor
#   (miner_meteor_os4, through the shared layer in ../os4_compat)

ARCH ?= m68k

ifeq ($(ARCH),ppc)
NAME = miner_meteor
CXXSRCS = main.cpp game.cpp levels.cpp render.cpp audio.cpp tilesheet_data.cpp sfx_data.cpp
include ../os4_compat/os4c.mk
else
  CC       = m68k-amigaos-g++
  CFLAGS   = -noixemul -O2 -m68020 -Wall -Wno-unused-function \
             -I../../amiga-bridge/include
  # -lnix before the libraries g++ adds (-lm): printf then comes from libnix, not
  # libm's floating-point printf, which needs LIBS:mathieeedoubbas.library on disk.
  LDFLAGS  = -noixemul -m68020 -L../../amiga-bridge -lbridge -lamiga -lnix

TARGET = miner_meteor
SRCS   = main.cpp game.cpp levels.cpp render.cpp audio.cpp tilesheet_data.cpp sfx_data.cpp
OBJS   = $(SRCS:.cpp=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.cpp
	$(CC) $(CFLAGS) -c -o $@ $<

# Generated sources, checked in so a build needs no Python (and no PIL,
# which the cross-compiler images lack). Regenerate them by hand:
#   make tilesheet   tilesheet_data.{h,cpp} from art/tilesheet.png (needs PIL)
#   make sfx         sfx_data.{h,cpp} from tools/gen_sfx.py
# Not file rules on purpose: a fresh clone's timestamps are arbitrary.
tilesheet:
	python3 tools/bake_tilesheet.py

sfx:
	python3 tools/gen_sfx.py

main.o:            main.cpp           game.h render.h audio.h
game.o:            game.cpp           game.h levels.h
levels.o:          levels.cpp         levels.h
render.o:          render.cpp         render.h game.h tilesheet_data.h
audio.o:           audio.cpp          audio.h sfx_data.h
sfx_data.o:        sfx_data.cpp       sfx_data.h
tilesheet_data.o:  tilesheet_data.cpp tilesheet_data.h

clean:
	rm -f *.o $(TARGET)

.PHONY: all clean tilesheet sfx

endif
