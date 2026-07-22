CC       = m68k-amigaos-gcc
CFLAGS   = -noixemul -O2 -m68020 -Wall -Wno-unused-function \
           -I../../amiga-bridge/include
LDFLAGS  = -noixemul -m68020 -L../../amiga-bridge -lbridge -lamiga
TARGET   = void_trader
SRCS     = main.c engine3d.c models.c combat.c scanner.c trade.c
OBJS     = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

main.o:     main.c engine3d.h models.h combat.h scanner.h gamemode.h trade.h
engine3d.o: engine3d.c engine3d.h
models.o:   models.c engine3d.h models.h
combat.o:   combat.c combat.h engine3d.h
scanner.o:  scanner.c scanner.h engine3d.h
trade.o:    trade.c trade.h

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
