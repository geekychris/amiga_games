CC      = m68k-amigaos-gcc
CFLAGS  = -noixemul -O2 -m68020 -Wall -Wno-unused-function \
          -I../../amiga-bridge/include
LDFLAGS = -noixemul -m68020 -L../../amiga-bridge -lbridge -lamiga
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
