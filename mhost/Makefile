# This makefile is for all platforms, but doesn't include support for the HD44780, OLED, or PCF8574 displays on the Raspberry Pi.

# If you have the resampler library installed, add -DHAS_SRC to the CFLAGS line, and -lsamplerate to the LIBS line.

CFLAGS  = -g -std=c++17 -Wall -pthread
LIBS    = -lpthread -lutil
LDFLAGS = -g -L/usr/local/lib

SRCS = $(wildcard *.cpp)
OBJECTS = $(SRCS:.cpp=.o)

all:		M17Host

M17Host:	GitVersion.h $(OBJECTS)
		$(CXX) $(OBJECTS) $(LDFLAGS) $(LIBS) -o M17Host

%.o: %.cpp
		$(CXX) $(CFLAGS) -c -o $@ $<


clean:
		$(RM) M17Host *.o *.d *.bak *~ GitVersion.h

# Export the current git version if the index file exists, else 000...
GitVersion.h:
ifneq ("$(wildcard .git/index)","")
	echo "const char *gitversion = \"$(shell git rev-parse HEAD)\";" > $@
else
	echo "const char *gitversion = \"0000000000000000000000000000000000000000\";" > $@
endif
