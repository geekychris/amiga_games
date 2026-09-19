# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>

# warden_keep — isometric Filmation-style room adventure. 68k only.
ARCH ?= m68k

ifeq ($(ARCH),ppc)
  CC      = ppc-amigaos-g++
  CFLAGS  = -mcrt=newlib -O2 -mcpu=440 -Wall -Wno-unused-function \
            -I../../amiga-bridge/include \
            -D__PPC__ -D__USE_INLINE__ -D__USE_OLD_TIMEVAL__
  LDFLAGS = -mcrt=newlib -L../../amiga-bridge -lbridge -lauto
else
  CC      = m68k-amigaos-g++
  CFLAGS  = -noixemul -O2 -m68020 -Wall -Wno-unused-function \
            -I../../amiga-bridge/include
  LDFLAGS = -noixemul -m68020 -L../../amiga-bridge -lbridge -lamiga
endif

TARGET = warden_keep
SRCS   = main.cpp game.cpp render.cpp
OBJS   = $(SRCS:.cpp=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.cpp
	$(CC) $(CFLAGS) -c -o $@ $<

main.o:   main.cpp   game.h render.h
game.o:   game.cpp   game.h
render.o: render.cpp render.h game.h

clean:
	rm -f *.o $(TARGET)

.PHONY: all clean
