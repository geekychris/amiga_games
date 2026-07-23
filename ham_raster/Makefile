CC = m68k-amigaos-gcc
CFLAGS = -noixemul -O2 -m68020 -Wall -Wno-unused-function -I../../amiga-bridge/include
LDFLAGS = -noixemul -L../../amiga-bridge -lbridge -lamiga
TARGET = ham_raster

SRCS = main.c rasterizer.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

main.o:       main.c rasterizer.h
rasterizer.o: rasterizer.c rasterizer.h

clean:
	rm -f *.o $(TARGET)
