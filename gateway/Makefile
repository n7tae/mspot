# Use the following CFLAGS and LIBS if you don't want to use gpsd.
CFLAGS  = -g -std=c++17 -Wall -pthread
LIBS    = -lpthread

# Use the following CFLAGS and LIBS if you do want to use gpsd.
#CFLAGS  = -g -std=c++17 -Wall -DUSE_GPSD -pthread
#LIBS    = -lpthread -lgps

LDFLAGS = -g

OBJECTS =	APRSWriter.o Conf.o Echo.o GPSHandler.o Log.o M17LSF.o M17Network.o M17Gateway.o M17Utils.o Reflectors.o \
		RptNetwork.o StopWatch.o Thread.o Timer.o UDPSocket.o Utils.o Voice.o

all:		M17Gateway

M17Gateway:	$(OBJECTS)
		$(CXX) $(OBJECTS) $(CFLAGS) $(LIBS) -o M17Gateway

%.o: %.cpp
		$(CXX) $(CFLAGS) -c -o $@ $<

M17Gateway.o: GitVersion.h

.PHONY: GitVersion.h

FORCE:

clean:
		$(RM) M17Gateway *.o *.d *.bak *~ GitVersion.h

install:
		install -m 755 M17Gateway /usr/local/bin/

# Export the current git version if the index file exists, else 000...
GitVersion.h:
ifneq ("$(wildcard .git/index)","")
	echo "const char *gitversion = \"$(shell git rev-parse HEAD)\";" > $@
else
	echo "const char *gitversion = \"0000000000000000000000000000000000000000\";" > $@
endif
