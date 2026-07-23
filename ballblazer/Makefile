CC = m68k-amigaos-gcc
CFLAGS = -noixemul -O2 -m68020 -Wall -Wno-unused-function -I../../amiga-bridge/include
LDFLAGS = -noixemul -L../../amiga-bridge -lbridge -lamiga
TARGET = ballblazer

SRCS = main.c pitch.c sound.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

main.o:  main.c ballblazer.h sound.h
pitch.o: pitch.c ballblazer.h
sound.o: sound.c sound.h

clean:
	rm -f *.o $(TARGET)
