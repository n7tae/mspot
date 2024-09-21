# Use the following CFLAGS and LIBS if you don't want to use gpsd.
CFLAGS  = -std=c++17 -Wall -Icommon -Igateway -Imhost
LIBS    = -pthread

# Use the following CFLAGS and LIBS if you do want to use gpsd.
#CFLAGS  = -g -std=c++17 -Wall -DUSE_GPSD -pthread
#LIBS    = -lpthread -lgps

COMSRCS = $(wildcard common/*.cpp)
COMOBJS = $(COMSRCS:.cpp=.o)
GATSRCS = $(wildcard gateway/*.cpp)
GATOBJS = $(GATSRCS:.cpp=.o)
HSTSRCS = $(wildcard mhost/*.cpp)
HSTOBJS = $(HSTSRCS:.cpp=.o)

all : gate host

gate : $(GATOBJS) $(COMOBJS) GitVersion.h
	$(CXX) $(CFLAGS) $(COMOBJS) $(GATOBJS) $(LIBS) -o $@

host : $(HSTOBJS) $(COMOBJS) GitVersion.h
	$(CXX) $(CFLAGS) $(COMOBJS) $(HSTOBJS) $(LIBS) -o $@

%.o: %.cpp
		$(CXX) $(CFLAGS) -c -o $@ $<

.PHONY: GitVersion.h

FORCE:

clean :
	$(RM) gate host common/*.o gateway/*.o mhost/*.o GitVersion.h

install : gateway/gate mhost/host
	install -m 755 gate /usr/local/bin/
	install -m 755 host /usr/local/bin/

# Export the current git version if the index file exists, else 000...
GitVersion.h :
ifneq ("$(wildcard .git/index)","")
	echo "const char *gitversion = \"$(shell git rev-parse HEAD)\";" > $@
else
	echo "const char *gitversion = \"0000000000000000000000000000000000000000\";" > $@
endif
