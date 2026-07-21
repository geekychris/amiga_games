CXX      = m68k-amigaos-g++
CXXFLAGS = -noixemul -O2 -m68020 -Wall -Wno-unused-function \
           -fno-exceptions -fno-rtti \
           -I../../amiga-bridge/include
LDFLAGS  = -noixemul -m68020 -L../../amiga-bridge -lbridge -lamiga
TARGET   = fractalus
SRCS     = main.cpp terrain.cpp render.cpp game.cpp pilots.cpp combat.cpp sfx.cpp
OBJS     = $(SRCS:.cpp=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) -o $@ $^ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# Header dependencies — small enough to just enumerate. Prevents the
# "changed a #define, didn't get picked up" trap we hit during Phase 4.
main.o:    main.cpp fixed.h terrain.h render.h game.h pilots.h combat.h
terrain.o: terrain.cpp terrain.h fixed.h
render.o:  render.cpp render.h terrain.h game.h pilots.h combat.h fixed.h
game.o:    game.cpp game.h terrain.h pilots.h combat.h fixed.h
pilots.o:  pilots.cpp pilots.h terrain.h fixed.h
combat.o:  combat.cpp combat.h game.h sfx.h fixed.h
sfx.o:     sfx.cpp sfx.h

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
