/*
 *   Copyright (C) 2009-2014,2016,2019,2020 by Jonathan Naylor G4KLX
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

#include "RptNetwork.h"
#include "M17Defines.h"
#include "M17Utils.h"
#include "Utils.h"
#include "Log.h"

#include <cstdio>
#include <cassert>
#include <cstring>

const unsigned int BUFFER_LENGTH = 200U;

CRptNetwork::CRptNetwork(unsigned short localPort, const std::string& gwyAddress, unsigned short gwyPort, bool debug) :
m_socket(localPort),
m_addr(),
m_addrLen(0U),
m_debug(debug),
m_buffer(1000U, "Rpt Network"),
m_timer(1000U, 5U)
{
	if (CUDPSocket::lookup(gwyAddress, gwyPort, m_addr, m_addrLen) != 0) {
		m_addrLen = 0U;
		return;
	}
}

CRptNetwork::~CRptNetwork()
{
}

bool CRptNetwork::open()
{
	if (m_addrLen == 0U) {
		LogError("Rpt, unable to resolve the gateway address");
		return false;
	}

	LogMessage("Opening Rpt Network connection");

	bool ret = m_socket.open(m_addr);

	if (ret) {
		m_timer.start();
		return true;
	} else {
		return false;
	}
}

bool CRptNetwork::write(const unsigned char* data)
{
	if (m_addrLen == 0U)
		return false;

	assert(data != NULL);

	if (m_debug)
		CUtils::dump(1U, "Rpt Network Data Transmitted", data, M17_NETWORK_FRAME_LENGTH);

	return m_socket.write(data, M17_NETWORK_FRAME_LENGTH, m_addr, m_addrLen);
}

void CRptNetwork::clock(unsigned int ms)
{
	m_timer.clock(ms);
	if (m_timer.isRunning() && m_timer.hasExpired()) {
		sendPing();
		m_timer.start();
	}

	unsigned char buffer[BUFFER_LENGTH];

	sockaddr_storage address;
	unsigned int addrLen;
	int length = m_socket.read(buffer, BUFFER_LENGTH, address, addrLen);
	if (length <= 0)
		return;

	if (!CUDPSocket::match(m_addr, address)) {
		LogMessage("Rpt, packet received from an invalid source");
		return;
	}

	if (m_debug)
		CUtils::dump(1U, "Rpt Network Data Received", buffer, length);

	if (::memcmp(buffer + 0U, "PING", 4U) == 0)
		return;

	if (::memcmp(buffer + 0U, "M17 ", 4U) != 0) {
		CUtils::dump(2U, "Rpt, received unknown packet", buffer, length);
		return;
	}

	unsigned char c = length;
	m_buffer.addData(&c, 1U);

	m_buffer.addData(buffer, length);
}

bool CRptNetwork::read(unsigned char* data)
{
	assert(data != NULL);

	if (m_buffer.isEmpty())
		return false;

	unsigned char c = 0U;
	m_buffer.getData(&c, 1U);

	m_buffer.getData(data, c);

	return true;
}

void CRptNetwork::close()
{
	m_socket.close();

	LogMessage("Closing Rpt network connection");
}

void CRptNetwork::sendPing()
{
	unsigned char buffer[5U];

	buffer[0U] = 'P';
	buffer[1U] = 'I';
	buffer[2U] = 'N';
	buffer[3U] = 'G';

	if (m_debug)
		CUtils::dump(1U, "Rpt data transmitted", buffer, 4U);

	m_socket.write(buffer, 4U, m_addr, m_addrLen);
}
