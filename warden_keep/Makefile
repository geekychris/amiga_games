# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>

# warden_keep — isometric Filmation-style room adventure. 68k only.
ARCH ?= m68k

ifeq ($(ARCH),ppc)
  CC      = ppc-amigaos-g++
  CFLAGS  = -mcrt=newlib -O2 -mcpu=powerpc -Wall -Wno-unused-function \
            -I../../amiga-bridge/include \
            -D__PPC__ -D__USE_INLINE__ -D__USE_OLD_TIMEVAL__
  # the bridge client is compiled in: ../../amiga-bridge/libbridge.a is a 68k build
  BRIDGE_OBJ = bridge_client.o
  LDFLAGS = -mcrt=newlib -lauto
else
  CC      = m68k-amigaos-g++
  CFLAGS  = -noixemul -O2 -m68020 -Wall -Wno-unused-function \
            -I../../amiga-bridge/include
  # -lnix before the libraries g++ adds (-lm): printf then comes from libnix, not
  # libm's floating-point printf, which needs LIBS:mathieeedoubbas.library on disk.
  LDFLAGS = -noixemul -m68020 -L../../amiga-bridge -lbridge -lamiga -lnix
endif

TARGET = warden_keep
SRCS   = main.cpp game.cpp render.cpp
OBJS   = $(SRCS:.cpp=.o) $(BRIDGE_OBJ)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

bridge_client.o: ../../amiga-bridge/client/bridge_client.c
	ppc-amigaos-gcc $(CFLAGS) -c -o $@ $<

%.o: %.cpp
	$(CC) $(CFLAGS) -c -o $@ $<

main.o:   main.cpp   game.h render.h
game.o:   game.cpp   game.h
render.o: render.cpp render.h game.h

clean:
	rm -f *.o $(TARGET)

.PHONY: all clean
