# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>

# emberkeep — top-down lantern-in-the-dark explorer.
# 68k build only for now; PPC scaffolding follows miner_meteor.

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

TARGET = emberkeep
SRCS   = main.cpp game.cpp levels.cpp render.cpp audio.cpp tilesheet_data.cpp
OBJS   = $(SRCS:.cpp=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.cpp
	$(CC) $(CFLAGS) -c -o $@ $<

# To regenerate the baked tilesheet from art/tilesheet.png, run:
#   python3 tools/bake_tilesheet.py
# The .{h,cpp} outputs are checked in so builds don't need Python.

main.o:            main.cpp           game.h render.h audio.h
game.o:            game.cpp           game.h levels.h
levels.o:          levels.cpp         levels.h
render.o:          render.cpp         render.h game.h tilesheet_data.h
audio.o:           audio.cpp          audio.h
tilesheet_data.o:  tilesheet_data.cpp tilesheet_data.h

clean:
	rm -f *.o $(TARGET)

.PHONY: all clean
