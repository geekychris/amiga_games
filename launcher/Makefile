# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>

# launcher — the game/app picker that starts at boot.
#   ARCH=m68k (default)  AmigaOS 3.x, scans DH2:Dev/ -> launcher
#   ARCH=ppc             AmigaOS 4.1, reads DH1:launcher.list -> launcher_os4
#                        (scripts/build-example-ppc.sh launcher, then deploy
#                        it as DH1:launcher with launcher.list beside it)
ARCH ?= m68k

ifeq ($(ARCH),ppc)
  CC      = ppc-amigaos-gcc
  CFLAGS  = -mcrt=newlib -O2 -mcpu=440 -Wall -I../../amiga-bridge/include \
            -D__PPC__ -D__USE_INLINE__ -D__USE_OLD_TIMEVAL__
  LDFLAGS = -mcrt=newlib -lauto
  TARGET  = launcher_os4
  # the bridge client is compiled in; ../../amiga-bridge/libbridge.a is 68k
  SRCS    = main.c ../../amiga-bridge/client/bridge_client.c
  OBJDIR  = objppc
else
  CC      = m68k-amigaos-gcc
  CFLAGS  = -noixemul -O2 -m68020 -Wall -I../../amiga-bridge/include
  LDFLAGS = -noixemul -g -L../../amiga-bridge -lbridge -lamiga
  TARGET  = launcher
  SRCS    = main.c
  OBJDIR  = obj68k
endif

OBJS = $(addprefix $(OBJDIR)/,$(notdir $(SRCS:.c=.o)))
vpath %.c . ../../amiga-bridge/client

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)

$(OBJDIR)/%.o: %.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR):
	mkdir -p $@

clean:
	rm -rf *.o obj68k objppc launcher launcher_os4

.PHONY: all clean
