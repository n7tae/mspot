// Copyright (C) 2021 by Jonathan Naylor G4KLX

/*

         mspot - an M17-only HotSpot using an MMDVM device
            Copyright (C) 2025 Thomas A. Early N7TAE

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

*/
#pragma once

#include "BasePort.h"
#include "RingBuffer.h"
#include "UDPSocket.h"

#include <string>

class CUDPController : public CBasePort
{
public:
	CUDPController(const std::string& modemAddress, unsigned modemPort, const std::string& localAddress, unsigned localPort);
	virtual ~CUDPController();

	virtual bool open();

	virtual int read(unsigned char *buffer, unsigned length);

	virtual int write(const unsigned char *buffer, unsigned length);

	virtual void close();

protected:
	const std::string m_modemAddress, m_localAddress;
	const unsigned m_modemPort, m_localPort;
	CSockAddress m_localSocket, m_modemSocket;
	CUDPSocket m_udpSocket;
	CRingBuffer<unsigned char> m_buffer;
};
