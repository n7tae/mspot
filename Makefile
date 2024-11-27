################################################################
#                                                              #
#             More - An M17-only Repeater/HotSpot              #
#                                                              #
#         Copyright (c) 2024 by Thomas A. Early N7TAE          #
#                                                              #
# See the LICENSE file for details about the software license. #
#                                                              #
################################################################

include morhs.mk

ifeq ($(DEBUG), true)
CPPFLAGS = -g -ggdb -std=c++17 -Wall -Isrcs
else
CPPFLAGS  = -g -std=c++17 -Wall -Isrcs
endif

LIBS    = -pthread

SRCS = $(wildcard srcs/*.cpp)
OBJS = $(SRCS:.cpp=.o)
DEPS = $(SRCS:.cpp=.d)

all : mor inicheck

mor : $(OBJS)
	$(CXX) $(CPPFLAGS) $(OBJS) $(LIBS) -o $@

inicheck : srcs/Configure.h srcs/Configure.cpp srcs/JsonKeys.h
	$(CXX) $(CPPFLAGS) -DINICHECK srcs/Configure.cpp -o $@

-include $(DEPS)

%.o: %.cpp
		$(CXX) $(CPPFLAGS) -MMD -MP -c -o $@ $<

clean :
	$(RM) $(all) srcs/*.o srcs/*.d

install : mor
	install -m 755 mor $(BINDIR)
