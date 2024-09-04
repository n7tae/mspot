/*
*   Copyright (C) 2016,2018,2020,2021 by Jonathan Naylor G4KLX
*
*   This program is free software; you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation; either version 2 of the License, or
*   (at your option) any later version.
*
*   This program is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU General Public License for more details.
*
*   You should have received a copy of the GNU General Public License
*   along with this program; if not, write to the Free Software
*   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#if !defined(M17Gateway_H)
#define	M17Gateway_H

#include "M17Network.h"
#include "APRSWriter.h"
#include "GPSHandler.h"
#include "Timer.h"
#include "Conf.h"

#include <cstdio>
#include <string>
#include <vector>

#include <netdb.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>

enum M17_STATUS {
	M17S_NOTLINKED,
	M17S_LINKED,
	M17S_LINKING,
	M17S_UNLINKING,
	M17S_ECHO
};

class CM17Gateway
{
public:
	CM17Gateway(const std::string& file);
	~CM17Gateway();

	int run();

private:
	CConf            m_conf;
	M17_STATUS       m_status;
	M17_STATUS       m_oldStatus;
	CM17Network*     m_network;
	CTimer           m_timer;
	std::string      m_reflector;
	unsigned int     m_addrLen;
	sockaddr_storage m_addr;
	char             m_module;
	CAPRSWriter*     m_writer;
	CGPSHandler*     m_gps;

	void linking();
	void unlinking();

	void createGPS();
};

#endif
