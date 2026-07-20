CXX      = m68k-amigaos-g++
CXXFLAGS = -noixemul -O2 -m68020 -Wall -Wno-unused-function \
           -fno-exceptions -fno-rtti \
           -I../../amiga-bridge/include
LDFLAGS  = -noixemul -m68020 -L../../amiga-bridge -lbridge -lamiga
TARGET   = fractalus
SRCS     = main.cpp terrain.cpp render.cpp game.cpp pilots.cpp
OBJS     = $(SRCS:.cpp=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) -o $@ $^ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
