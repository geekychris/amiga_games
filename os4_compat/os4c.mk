# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
#
# Build rules for a classic game on AmigaOS 4 with the os4_compat layer.
# The game's Makefile, under ARCH=ppc, sets
#   NAME   the program (NAME_os4 is built)
#   SRCS   its C sources (may be in subdirectories)
#   CXXSRCS  its C++ sources, if any
#   EXTRA_CFLAGS  anything more
#   OS4C_CHUNKY = 1  8-bit chunky screen bitmaps instead of planar ones
#                    (not for a game that writes into the screen's Planes[])
# and includes this file.  Every game source is compiled with
# -include os4_compat.h (see include/os4_compat.h); the layer and the
# bridge client are compiled without it.
OS4C    := $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))
BRIDGE  ?= ../../amiga-bridge
CC       = ppc-amigaos-gcc
CXX      = ppc-amigaos-g++
# -mcpu=powerpc, never 440/440fp: those emit dlmzb, which real machines lack
BASEFLAGS = -mcrt=newlib -O2 -mcpu=powerpc -Wall -I$(OS4C)/include -I$(BRIDGE)/include \
            -D__PPC__ -D__USE_INLINE__ -D__USE_OLD_TIMEVAL__ -D__USE_BASETYPE__
# the games' AllocMem/FreeMem still work on AmigaOS 4; and gcc 11's -Wall
# notes about unused code and indentation are about the games' own code,
# the same for the 68k build
ifeq ($(OS4C_CHUNKY),1)
BASEFLAGS += -DOS4C_CHUNKY
endif
GAMEFLAGS = $(BASEFLAGS) -I. -include os4_compat.h -Wno-deprecated-declarations \
            -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable \
            -Wno-misleading-indentation $(EXTRA_CFLAGS)
LDFLAGS  = -mcrt=newlib -lauto -lm
OBJDIR   = objppc
TARGET   = $(NAME)_os4

LAYER_SRCS = os4c_screen.c os4c_paula.c os4c_dos.c ptplayer.c
GAME_OBJS  = $(addprefix $(OBJDIR)/,$(notdir $(SRCS:.c=.o) $(CXXSRCS:.cpp=.o)))
LAYER_OBJS = $(addprefix $(OBJDIR)/os4c_,$(LAYER_SRCS:.c=.o)) $(OBJDIR)/os4c_bridge_client.o
OBJS       = $(GAME_OBJS) $(LAYER_OBJS)
HDRS       = $(wildcard *.h) $(foreach d,$(sort $(dir $(SRCS) $(CXXSRCS))),$(wildcard $(d)*.h)) \
             $(wildcard $(OS4C)/*.h) $(wildcard $(OS4C)/include/*.h) $(wildcard $(OS4C)/include/hardware/*.h)
vpath %.c $(sort $(dir $(SRCS)))
vpath %.cpp $(sort $(dir $(CXXSRCS)))

ifneq ($(CXXSRCS),)
LINK = $(CXX)
else
LINK = $(CC)
endif

all: $(TARGET)

$(TARGET): $(OBJS)
	$(LINK) -o $@ $(OBJS) $(LDFLAGS)

$(OBJDIR)/%.o: %.c $(HDRS) | $(OBJDIR)
	$(CC) $(GAMEFLAGS) -c -o $@ $<

$(OBJDIR)/%.o: %.cpp $(HDRS) | $(OBJDIR)
	$(CXX) $(GAMEFLAGS) -fno-exceptions -fno-rtti -c -o $@ $<

$(OBJDIR)/os4c_%.o: $(OS4C)/%.c $(HDRS) | $(OBJDIR)
	$(CC) $(BASEFLAGS) -I$(OS4C) -c -o $@ $<

$(OBJDIR)/os4c_bridge_client.o: $(BRIDGE)/client/bridge_client.c | $(OBJDIR)
	$(CC) $(BASEFLAGS) -Wno-deprecated-declarations -c -o $@ $<

$(OBJDIR):
	mkdir -p $@

clean:
	rm -rf $(OBJDIR) $(TARGET)

.PHONY: all clean
