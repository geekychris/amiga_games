# ARCH=m68k (default) — classic AmigaOS 3.x on 680x0
# ARCH=ppc            — AmigaOS 4.1 on PowerPC (sam460ex / QEMU)
# Build for PPC via: scripts/build-example-ppc.sh hello_world
ARCH ?= m68k

ifeq ($(ARCH),ppc)
  CC = ppc-amigaos-gcc
  CFLAGS  = -mcrt=newlib -O2 -mcpu=440 -Wall -I../../amiga-bridge/include -D__PPC__
  LDFLAGS = -mcrt=newlib -L../../amiga-bridge -lbridge -lauto
else
  CC = m68k-amigaos-gcc
  CFLAGS  = -noixemul -O0 -m68020 -Wall -I../../amiga-bridge/include
  LDFLAGS = -noixemul -g -L../../amiga-bridge -lbridge -lamiga
endif

TARGET = hello_world

all: $(TARGET)

$(TARGET): main.o
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *.o $(TARGET)
