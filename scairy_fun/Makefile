# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>

# ScAIry Fun - a self-driving car game for the classic Amiga (68020+).
#   make              game + on-Amiga test runner (Docker cross-compiler:
#                     scripts/build-example-68k.sh scairy_fun)
#   make host-test    the unit and gameplay tests built and run natively
#   make music        scairy.mod   (tools/modgen/scairy_fun.py)
#   make voice        scairy.voc   (tools/modgen/scairy_voices.py, needs Kokoro)

CORE = sc_math.c track.c world.c
GAME = main.c game.c render.c models.c sound.c tests/test_cases.c $(CORE)
HEADERS = sc.h render.h game.h sound.h models.h

CC = m68k-amigaos-gcc
AS = vasmm68k_mot
CFLAGS = -noixemul -O2 -m68020 -Wall -I../../amiga-bridge/include
ASFLAGS = -Fhunk -m68000 -no-opt -I/opt/m68k-amigaos/m68k-amigaos/ndk-include/hardware -I/opt/m68k-amigaos/m68k-amigaos/ndk-include/resources
LDFLAGS = -noixemul -g -L../../amiga-bridge -lbridge -lamiga

GAME_OBJS = $(GAME:.c=.o) ptplayer.o
TEST_OBJS = tests/amiga_test_main.o tests/test_cases.o $(CORE:.c=.o)
TRACE_OBJS = tests/trace_main.o $(CORE:.c=.o)

all: scairy_fun scairy_fun_test scairy_fun_trace

scairy_fun: $(GAME_OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

scairy_fun_test: $(TEST_OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

scairy_fun_trace: $(TRACE_OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

ptplayer.o: ptplayer.asm
	$(AS) $(ASFLAGS) -o $@ $<

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c -o $@ $<

HOSTCC ?= cc
host-test:
	$(HOSTCC) -O2 -Wall -o tests/host_tests tests/host_main.c tests/test_cases.c $(CORE)
	./tests/host_tests

host-trace:
	$(HOSTCC) -O2 -Wall -o tests/host_trace tests/trace_main.c $(CORE)

music:
	cd ../tools/modgen && python3 scairy_fun.py

voice:
	cd ../tools/modgen && python3 scairy_voices.py

clean:
	rm -f *.o tests/*.o scairy_fun scairy_fun_test scairy_fun_trace tests/host_tests tests/host_trace

.PHONY: all clean host-test music voice
