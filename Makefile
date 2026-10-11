################################################################
#                                                              #
#            mspot - An M17-only Hotspot/Repeater              #
#                                                              #
#      Copyright (c) 2025-2026 by Thomas A. Early N7TAE        #
#                                                              #
# See the LICENSE file for details about the software license. #
#                                                              #
################################################################

include mspot.mk

ifeq ($(DEBUG), true)
CPPFLAGS = -ggdb -std=c++17 -Wall -Wextra -Werror -Isrcs -DDEBUG
else
CPPFLAGS  = -std=c++17 -Wall -Wextra -Werror -Isrcs
endif

LIBS = -pthread -lm -lgpiod -lsqlite3

ifeq ($(USE_TS), false)
CPPFLAGS += -DNO_TS
endif

SRCS = $(wildcard srcs/*.cpp)
OBJS = $(SRCS:.cpp=.o)
DEPS = $(SRCS:.cpp=.d)
EXES = mspot inicheck cc1200-reset

all : $(EXES)

mspot : $(OBJS)
	$(CXX) $(CPPFLAGS) $(OBJS) /usr/local/lib/libm17.a $(LIBS) -o $@

inicheck : srcs/Configure.h srcs/Configure.cpp srcs/JsonKeys.h
	$(CXX) $(CPPFLAGS) -DINICHECK srcs/Configure.cpp srcs/LineTools.o -o $@

cc1200-reset : tools/cc1200-reset.c
	gcc -o $@ tools/cc1200-reset.c -lgpiod

-include $(DEPS)

%.o: %.cpp
		$(CXX) $(CPPFLAGS) -MMD -MP -c -o $@ $<

.PHONY : clean
clean :
	$(RM) $(EXES) srcs/*.o srcs/*.d

.PHONY : install
install : mspot.service mspot
	### Installing mspot ###
	mkdir -p $(SYSTEMDDIR)
	cp -f mspot.service $(SYSTEMDDIR)
	systemctl --user enable mspot
	systemctl --user daemon-reload
	systemctl --user start mspot
	sudo loginctl enable-linger $(USER)

installdash : index.php
	### Installing mspot Dashboard ###
	mkdir -p $(SYSTEMDDIR)
	/bin/cp -f mdash.service $(SYSTEMDDIR)
	systemctl --user enable mdash
	systemctl --user daemon-reload
	systemctl --user start mdash
	sudo loginctl enable-linger $(USER)

.PHONY : uninstall
uninstall :
	### Uninstall mspot ###
	systemctl --user stop mspot
	systemctl --user disable mspot
	$(RM) $(SYSTEMDDIR)/mspot.service
	systemctl --user daemon-reload

uninstalldash :
	### Uninstall mspot Dashboard ###
	systemctl --user stop mdash
	systemctl --user disable mdash
	/bin/rm -f $(SYSTEMDDIR)/mdash.service
	systemctl --user daemon-reload
