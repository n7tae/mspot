/*
 *   Copyright (C) 2009-2014,2016,2019,2020,2021,2024 by Jonathan Naylor G4KLX
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

#include "M17Network.h"
#include "M17Defines.h"
#include "M17Utils.h"
#include "Utils.h"
#include "Log.h"

#include <cstdio>
#include <cassert>
#include <cstring>

const unsigned int BUFFER_LENGTH = 200U;

CM17Network::CM17Network(const std::string& callsign, const std::string& suffix, unsigned short port, bool debug) :
m_socket(port),
m_name(),
m_addr(),
m_addrLen(0U),
m_debug(debug),
m_buffer(1000U, "M17 Network"),
m_state(M17N_NOTLINKED),
m_encoded(NULL),
m_module(' '),
m_timer(1000U, 1U),
m_timeout(1000U, 30U)
{
	assert(!callsign.empty());
	assert(!suffix.empty());
	// assert(port > 0U);

	m_encoded = new unsigned char[6U];

	std::string call = callsign;
	call.resize(M17_CALLSIGN_LENGTH - 1U, ' ');
	call += suffix.substr(0U, 1U);

	CM17Utils::encodeCallsign(call, m_encoded);
}

CM17Network::~CM17Network()
{
	delete[] m_encoded;
}

bool CM17Network::link(const std::string& name, const sockaddr_storage& addr, unsigned int addrLen, char module)
{
	close();

	LogMessage("Opening M17 Network connection");

	bool ret = m_socket.open(addr);
	if (!ret)
		return false;

	m_name    = name;
	m_addr    = addr;
	m_addrLen = addrLen;
	m_module  = module;

	m_state = M17N_LINKING;

	sendConnect();

	m_timer.start();

	return true;
}

void CM17Network::unlink()
{
	if (m_state != M17N_LINKED && m_state != M17N_LINKING)
		return;

	m_state = M17N_UNLINKING;

	sendDisconnect();

	m_timer.start();
	m_timeout.stop();
}

bool CM17Network::write(const unsigned char* data)
{
	assert(data != NULL);

	if (m_state != M17N_LINKED)
		return false;

	assert(data != NULL);

	if (m_debug)
		CUtils::dump(1U, "M17 Network Data Transmitted", data, M17_NETWORK_FRAME_LENGTH);

	return m_socket.write(data, M17_NETWORK_FRAME_LENGTH, m_addr, m_addrLen);
}

void CM17Network::clock(unsigned int ms)
{
	m_timer.clock(ms);
	if (m_timer.isRunning() && m_timer.hasExpired()) {
		switch (m_state) {
		case M17N_LINKING:
			sendConnect();
			m_timer.start();
			break;
		case M17N_UNLINKING:
			sendDisconnect();
			m_timer.start();
			break;
		default:
			m_timer.stop();
			break;
		}
	}

	m_timeout.clock(ms);
	if (m_timeout.isRunning() && m_timeout.hasExpired()) {
		LogMessage("Link lost to reflector %s", m_name.c_str());
		m_state = M17N_FAILED;
		m_timeout.stop();
	}

	if (m_state == M17N_NOTLINKED)
		return;

	unsigned char buffer[BUFFER_LENGTH];

	sockaddr_storage address;
	unsigned int addrLen;
	int length = m_socket.read(buffer, BUFFER_LENGTH, address, addrLen);
	if (length <= 0)
		return;

	if (!CUDPSocket::match(m_addr, address)) {
		LogMessage("Packet received from an invalid source");
		return;
	}

	if (m_debug)
		CUtils::dump(1U, "M17 Network Data Received", buffer, length);

	if (::memcmp(buffer + 0U, "ACKN", 4U) == 0) {
		m_timer.stop();
		m_state = M17N_LINKED;
		m_timeout.start();
		LogMessage("Linked to reflector %s", m_name.c_str());
		return;
	}

	if (::memcmp(buffer + 0U, "NACK", 4U) == 0) {
		m_timer.stop();
		m_state = M17N_REJECTED;
		LogMessage("Link refused by reflector %s", m_name.c_str());
		return;
	}

	if (::memcmp(buffer + 0U, "DISC", 4U) == 0) {
		m_timer.stop();
		m_state = M17N_NOTLINKED;
		LogMessage("Unlinked from reflector %s", m_name.c_str());
		return;
	}

	if (::memcmp(buffer + 0U, "PING", 4U) == 0) {
		if (m_state == M17N_LINKED) {
			m_timeout.start();
			sendPong();
		}
		return;
	}

	if (::memcmp(buffer + 0U, "M17 ", 4U) != 0) {
		CUtils::dump(2U, "M17, received unknown packet", buffer, length);
		return;
	}

	if (m_state == M17N_LINKED)
		m_timeout.start();

	unsigned char c = length;
	m_buffer.addData(&c, 1U);

	m_buffer.addData(buffer, length);
}

bool CM17Network::read(unsigned char* data)
{
	assert(data != NULL);

	if (m_buffer.isEmpty())
		return false;

	unsigned char c = 0U;
	m_buffer.getData(&c, 1U);

	m_buffer.getData(data, c);

	return true;
}

void CM17Network::close()
{
	m_socket.close();

	LogMessage("Closing M17 network connection");
}

void CM17Network::stop()
{
	m_timeout.stop();
	m_timer.stop();
}

M17NET_STATUS CM17Network::getStatus() const
{
	return m_state;
}

void CM17Network::sendConnect()
{
	unsigned char buffer[15U];

	buffer[0U] = 'C';
	buffer[1U] = 'O';
	buffer[2U] = 'N';
	buffer[3U] = 'N';

	buffer[4U] = m_encoded[0U];
	buffer[5U] = m_encoded[1U];
	buffer[6U] = m_encoded[2U];
	buffer[7U] = m_encoded[3U];
	buffer[8U] = m_encoded[4U];
	buffer[9U] = m_encoded[5U];

	buffer[10U] = m_module;

	LogDebug("Connecting module %c", m_module);

	if (m_debug)
		CUtils::dump(1U, "M17 data transmitted", buffer, 11U);

	m_socket.write(buffer, 11U, m_addr, m_addrLen);
}

void CM17Network::sendPong()
{
	unsigned char buffer[15U];

	buffer[0U] = 'P';
	buffer[1U] = 'O';
	buffer[2U] = 'N';
	buffer[3U] = 'G';

	buffer[4U] = m_encoded[0U];
	buffer[5U] = m_encoded[1U];
	buffer[6U] = m_encoded[2U];
	buffer[7U] = m_encoded[3U];
	buffer[8U] = m_encoded[4U];
	buffer[9U] = m_encoded[5U];

	if (m_debug)
		CUtils::dump(1U, "M17 data transmitted", buffer, 10U);

	m_socket.write(buffer, 10U, m_addr, m_addrLen);
}

void CM17Network::sendDisconnect()
{
	unsigned char buffer[15U];

	buffer[0U] = 'D';
	buffer[1U] = 'I';
	buffer[2U] = 'S';
	buffer[3U] = 'C';

	buffer[4U] = m_encoded[0U];
	buffer[5U] = m_encoded[1U];
	buffer[6U] = m_encoded[2U];
	buffer[7U] = m_encoded[3U];
	buffer[8U] = m_encoded[4U];
	buffer[9U] = m_encoded[5U];

	if (m_debug)
		CUtils::dump(1U, "M17 data transmitted", buffer, 10U);

	m_socket.write(buffer, 10U, m_addr, m_addrLen);
}
