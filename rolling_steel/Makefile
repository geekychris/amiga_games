# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>

# rolling_steel — the marble racer, ported from the 3DO version.
# AmigaOS 4.1 PPC only: the 3D goes through Mesa's software rasteriser
# (OSMesa), built by ../../third_party/mesa-os4/build.sh.
# Build via: scripts/build-example-ppc.sh rolling_steel
#
# -mcpu=440fp, not 440: with this GCC 11, plain 440 means soft float,
# which won't link against the hard-float libOSMesa / newlib libm.
ARCH ?= ppc

MESA    = ../../third_party/mesa-os4/out
CC      = ppc-amigaos-gcc
CFLAGS  = -mcrt=newlib -O2 -mcpu=440fp -Wall -Wno-unused-function \
          -I$(MESA)/include -I../../amiga-bridge/include \
          -D__PPC__ -D__USE_INLINE__ -D__USE_OLD_TIMEVAL__
LDFLAGS = -mcrt=newlib -L$(MESA)/lib -lOSMesa -lm -lauto

TARGET = rolling_steel
SRCS   = main_os4.c game.c phys.c course.c render.c glcels.c sound_ahi.c os4_display.c
# the bridge client is compiled in, so the shared ../../amiga-bridge/libbridge.a
# (which may be a 68k build) is left alone
BRIDGE = ../../amiga-bridge/client/bridge_client.c
OBJS   = $(SRCS:.c=.o) bridge_client.o

all: $(TARGET)

ifneq ($(ARCH),ppc)
$(TARGET):
	@echo "rolling_steel is AmigaOS 4 (PPC) only - build with ARCH=ppc"
else
$(TARGET): $(OBJS) $(MESA)/lib/libOSMesa.a
	$(CC) -o $@ $(OBJS) $(LDFLAGS)
endif

$(MESA)/lib/libOSMesa.a:
	@echo "Missing $@ - run third_party/mesa-os4/build.sh first" && false

# game.c (the 3DO code, unchanged) has a static kill(): newlib declares one too
game.o: game.c game.h rs.h game_os4.h
	$(CC) $(CFLAGS) -include game_os4.h -c -o $@ $<

bridge_client.o: $(BRIDGE)
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

main_os4.o:  main_os4.c game.h rs.h glcels.h
phys.o:      phys.c rs.h
course.o:    course.c rs.h
render.o:    render.c rs.h glcels.h
glcels.o:    glcels.c glcels.h
sound_ahi.o: sound_ahi.c rs.h

clean:
	rm -f *.o $(TARGET)

.PHONY: all clean
