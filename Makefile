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

CFLAGS  = -g -std=c++17 -Wall -Isrcs
LIBS    = -pthread

SRCS = $(wildcard srcs/*.cpp)
OBJS = $(SRCS:.cpp=.o)
DEPS = $(SRCS:.cpp=.d)

all : mhost inicheck

mhost : $(OBJS)
	$(CXX) $(CFLAGS) $(OBJS) $(LIBS) -o $@

inicheck : srcs/Configure.h srcs/Configure.cpp srcs/JsonKeys.h
	$(CXX) $(CFLAGS) -DINICHECK srcs/Configure.cpp -o $@

-include $(DEPS)

%.o: %.cpp
		$(CXX) $(CFLAGS) -MMD -MP -c -o $@ $<

clean :
	$(RM) $(all) srcs/*.o srcs/*.d

install : gateway/gate mhost/host
	install -m 755 m17host $(BINDIR)
