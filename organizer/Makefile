# Organizer — notes, tasks, calendar for AmigaOS (built for OS4 PPC).
#
# ARCH=m68k (default) — classic AmigaOS 3.x on 680x0
# ARCH=ppc            — AmigaOS 4.1 on PowerPC (sam460ex / QEMU)
# Build for PPC via: scripts/build-example-ppc.sh organizer
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

TARGET = organizer
SRCS   = main.c ui.c storage.c notes.c tasks.c calendar.c recur.c dialogs.c tests.c
OBJS   = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c organizer.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *.o $(TARGET)
