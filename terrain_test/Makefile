# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins
#
# ARCH=m68k (default) — classic AmigaOS 3.x on 680x0 (FS-UAE)
# ARCH=ppc            — AmigaOS 4.1 on PowerPC (sam460ex / QEMU)
# Build for PPC via: scripts/build-example-ppc.sh terrain_test
ARCH ?= m68k

ifeq ($(ARCH),ppc)
  CC = ppc-amigaos-gcc
  CFLAGS  = -mcrt=newlib -O2 -mcpu=440 -Wall -Wno-unused-function \
            -I../../amiga-bridge/include \
            -D__PPC__ -D__USE_INLINE__ -D__USE_OLD_TIMEVAL__
  LDFLAGS = -mcrt=newlib -L../../amiga-bridge -lbridge -lauto
else
  CC = m68k-amigaos-gcc
  CFLAGS  = -noixemul -O2 -m68020 -Wall -Wno-unused-function \
            -I../../amiga-bridge/include
  LDFLAGS = -noixemul -m68020 -L../../amiga-bridge -lbridge -lamiga
endif

TARGET  = terrain_test
SRCS    = main.c
OBJS    = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
