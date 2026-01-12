################################################################
#                                                              #
#            mspot - An M17-only Hotspot/Repeater              #
#                                                              #
#         Copyright (c) 2025 by Thomas A. Early N7TAE          #
#                                                              #
# See the LICENSE file for details about the software license. #
#                                                              #
################################################################

include mspot.mk

ifeq ($(DEBUG), true)
CPPFLAGS = -ggdb -std=c++17 -Wall -Wextra -Werror -Isrcs
else
CPPFLAGS  = -std=c++17 -Wall -Wextra -Werror -Isrcs
endif

LIBS    = -pthread -lm -lgpiod

SRCS = $(wildcard srcs/*.cpp)
OBJS = $(SRCS:.cpp=.o)
DEPS = $(SRCS:.cpp=.d)

all : mspot inicheck

mspot : $(OBJS)
	$(CXX) $(CPPFLAGS) $(OBJS) /usr/local/lib/libm17.a $(LIBS) -o $@

inicheck : srcs/Configure.h srcs/Configure.cpp srcs/JsonKeys.h
	$(CXX) $(CPPFLAGS) -DINICHECK srcs/Configure.cpp -o $@

-include $(DEPS)

%.o: %.cpp
		$(CXX) $(CPPFLAGS) -MMD -MP -c -o $@ $<

.PHONY : clean
clean :
	$(RM) mspot inicheck srcs/*.o srcs/*.d

.PHONY : install
install : mspot.service mspot
	### Installing mspot ###
	cp -f mspot $(BINDIR)
	cp -f mspot.service /etc/systemd/system/
	systemctl enable mspot
	systemctl daemon-reload
	systemctl start mspot

.PHONY : uninstall
uninstall :
	### Uninstalling mspot... ###
	systemctl stop mspot
	systemctl disable mspot
	$(RM) /etc/systemd/system/mspot.service
	systemctl daemon-reload
	$(RM) $(BINDIR)/mspot
