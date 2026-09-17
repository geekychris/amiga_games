# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>

# miner-meteor — clean-room single-screen tile platformer.
#
# ARCH=m68k (default) — classic AmigaOS 3.x on 680x0
# ARCH=ppc            — AmigaOS 4.1 on PowerPC (sam460ex / QEMU)
#
# Build for PPC via: scripts/build-example-ppc.sh miner_meteor

ARCH ?= m68k

ifeq ($(ARCH),ppc)
  CC       = ppc-amigaos-g++
  CFLAGS   = -mcrt=newlib -O2 -mcpu=440 -Wall -Wno-unused-function \
             -I../../amiga-bridge/include \
             -D__PPC__ -D__USE_INLINE__ -D__USE_OLD_TIMEVAL__
  LDFLAGS  = -mcrt=newlib -L../../amiga-bridge -lbridge -lauto
else
  CC       = m68k-amigaos-g++
  CFLAGS   = -noixemul -O2 -m68020 -Wall -Wno-unused-function \
             -I../../amiga-bridge/include
  LDFLAGS  = -noixemul -m68020 -L../../amiga-bridge -lbridge -lamiga
endif

TARGET = miner_meteor
SRCS   = main.cpp game.cpp levels.cpp render.cpp audio.cpp
OBJS   = $(SRCS:.cpp=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.cpp
	$(CC) $(CFLAGS) -c -o $@ $<

main.o:    main.cpp    game.h render.h audio.h
game.o:    game.cpp    game.h levels.h
levels.o:  levels.cpp  levels.h
render.o:  render.cpp  render.h game.h
audio.o:   audio.cpp   audio.h

clean:
	rm -f *.o $(TARGET)

.PHONY: all clean
