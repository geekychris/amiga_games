# Masterwork — dual-pane file manager, inspired by Directory Opus.
#
# ARCH=m68k (default) — classic AmigaOS 3.x on 680x0
# ARCH=ppc            — AmigaOS 4.1 on PowerPC (sam460ex / QEMU)
# Build for PPC via: scripts/build-example-ppc.sh masterwork
ARCH ?= m68k

ifeq ($(ARCH),ppc)
  CC = ppc-amigaos-gcc
  CFLAGS  = -mcrt=newlib -O2 -mcpu=440 -Wall -I../../amiga-bridge/include \
            -D__PPC__ -D__USE_INLINE__ -D__USE_OLD_TIMEVAL__
  LDFLAGS = -mcrt=newlib -L../../amiga-bridge -lbridge -lauto
else
  CC = m68k-amigaos-gcc
  CFLAGS  = -noixemul -O2 -m68020 -Wall -I../../amiga-bridge/include
  LDFLAGS = -noixemul -L../../amiga-bridge -lbridge -lamiga
endif

TARGET = masterwork
SRCS   = main.c panes.c fileops.c dialogs.c buttons.c
OBJS   = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c masterwork.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *.o $(TARGET)
