/*
 *   Copyright (C) 2009-2014,2016,2018,2020,2021 by Jonathan Naylor G4KLX
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

#ifndef	M17Network_H
#define	M17Network_H

#include "M17Defines.h"
#include "RingBuffer.h"
#include "UDPSocket.h"
#include "Timer.h"

#include <cstdint>

enum M17NET_STATUS {
	M17N_NOTLINKED,
	M17N_LINKING,
	M17N_LINKED,
	M17N_UNLINKING,
	M17N_REJECTED,
	M17N_FAILED
};

class CM17Network {
public:
	CM17Network(const std::string& callsign, const std::string& suffix, unsigned short port, bool debug);
	~CM17Network();

	bool link(const std::string& name, const sockaddr_storage& addr, unsigned int addrLen, char module);

	void unlink();

	bool write(const unsigned char* data);

	bool read(unsigned char* data);

	void stop();

	void close();

	void clock(unsigned int ms);

	M17NET_STATUS getStatus() const;

private:
	CUDPSocket       m_socket;
	std::string      m_name;
	sockaddr_storage m_addr;
	unsigned int     m_addrLen;
	bool             m_debug;
	CRingBuffer<unsigned char> m_buffer;
	M17NET_STATUS    m_state;
	unsigned char*   m_encoded;
	char             m_module;
	CTimer           m_timer;
	CTimer           m_timeout;

	void sendConnect();
	void sendDisconnect();
	void sendPong();
};

#endif
