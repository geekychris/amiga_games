# ARCH=m68k (default) — classic AmigaOS 3.x on 680x0
# ARCH=ppc            — AmigaOS 4.1 on PowerPC (sam460ex / QEMU)
# Build for PPC via: scripts/build-example-ppc.sh void_trader
#
# On PPC we swap modplay.c + sfx.c (both use direct Paula chip register
# access which doesn't exist on OS4/sam460ex) for arch_audio_stub_ppc.c
# which no-ops the audio API. Game runs silent on OS4.
ARCH ?= m68k

ifeq ($(ARCH),ppc)
  CC = ppc-amigaos-gcc
  CFLAGS  = -mcrt=newlib -O2 -mcpu=440 -Wall -Wno-unused-function \
            -I../../amiga-bridge/include \
            -D__PPC__ -D__USE_INLINE__ -D__USE_OLD_TIMEVAL__
  LDFLAGS = -mcrt=newlib -L../../amiga-bridge -lbridge -lauto
  # Swap in the arch audio stub instead of modplay + sfx.
  AUDIO_SRCS = arch_audio_stub_ppc.c
else
  CC = m68k-amigaos-gcc
  CFLAGS  = -noixemul -O2 -m68020 -Wall -Wno-unused-function \
            -I../../amiga-bridge/include
  LDFLAGS = -noixemul -m68020 -L../../amiga-bridge -lbridge -lamiga
  AUDIO_SRCS = modplay.c sfx.c
endif

TARGET   = void_trader
SRCS     = main.c engine3d.c models.c combat.c scanner.c trade.c $(AUDIO_SRCS)
OBJS     = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

main.o:     main.c engine3d.h models.h combat.h scanner.h gamemode.h trade.h modplay.h sfx.h
engine3d.o: engine3d.c engine3d.h
models.o:   models.c engine3d.h models.h
combat.o:   combat.c combat.h engine3d.h sfx.h
scanner.o:  scanner.c scanner.h engine3d.h
trade.o:    trade.c trade.h
modplay.o:  modplay.c modplay.h
sfx.o:      sfx.c sfx.h modplay.h
arch_audio_stub_ppc.o: arch_audio_stub_ppc.c modplay.h sfx.h

clean:
	rm -f *.o $(TARGET)

.PHONY: all clean
