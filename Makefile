################################################################
#                                                              #
#             More - An M17-only Repeater/HotSpot              #
#                                                              #
#         Copyright (c) 2024 by Thomas A. Early N7TAE          #
#                                                              #
# See the LICENSE file for details about the software license. #
#                                                              #
################################################################

include mhost.mk

CFLAGS  = -std=c++17 -Wall -Isrcs
LIBS    = -pthread

SRCS = $(wildcard srcs/*.cpp)
OBJS = $(SRCS:.cpp=.o)

all : mhost inicheck

mhost : GitVersion.h $(OBJS)
	$(CXX) $(CFLAGS) $(OBJS) $(LIBS) -o $@

inicheck : srcs/Configure.h srcs/Configure.cpp srcs/JsonKeys.h
	$(CXX) $(CFLAGS) -DINICHECK srcs/Configure.cpp -o $@

%.o: %.cpp
		$(CXX) $(CFLAGS) -c -o $@ $<

.PHONY: GitVersion.h

FORCE:

clean :
	$(RM) $(all) srcs/*.o GitVersion.h

install : gateway/gate mhost/host
	install -m 755 m17host $(BINDIR)

# Export the current git version if the index file exists, else 000...
GitVersion.h :
ifneq ("$(wildcard .git/index)","")
	echo "const char *gitversion = \"$(shell git rev-parse HEAD)\";" > srcs/$@
else
	echo "const char *gitversion = \"0000000000000000000000000000000000000000\";" > srcs/$@
endif
