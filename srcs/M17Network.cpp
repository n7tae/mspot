//  Copyright (C) 2020,2021,2023 by Jonathan Naylor G4KLX
/****************************************************************
 *                                                              *
 *             More - An M17-only Repeater/HotSpot              *
 *                                                              *
 *         Copyright (c) 2024 by Thomas A. Early N7TAE          *
 *                                                              *
 * See the LICENSE file for details about the software license. *
 *                                                              *
 ****************************************************************/

#include "M17Network.h"
#include "M17Defines.h"
#include "M17Utils.h"
#include "Defines.h"
#include "Utils.h"
#include "Log.h"

#include <cstdio>
#include <cassert>
#include <cstring>

const unsigned int BUFFER_LENGTH = 200U;

CM17Network::CM17Network(bool debug) :
m_debug(debug),
m_enabled(false),
m_outId(0U),
m_inId(0U),
m_buffer(1000U, "M17 Network"),
m_random()
{
	std::random_device rd;
	std::mt19937 mt(rd());
	m_random = mt;
}

CM17Network::~CM17Network()
{
}

bool CM17Network::open()
{
	LogMessage("Opening M17 network connection");

	if (Gate2Host.Open("gate2host"))
		return false;
	Host2Gate.SetUp("host2gate");
	return true;
}

bool CM17Network::write(const unsigned char* data)
{
	assert(data != NULL);

	unsigned char buffer[54U];

	buffer[0U] = 'M';
	buffer[1U] = '1';
	buffer[2U] = '7';
	buffer[3U] = ' ';

	// Create a random id for this transmission if needed
	if (m_outId == 0U) {
		std::uniform_int_distribution<uint16_t> dist(0x0001, 0xFFFE);
		m_outId = dist(m_random);
	}

	buffer[4U] = m_outId / 256U;	// Unique session id
	buffer[5U] = m_outId % 256U;

	::memcpy(buffer + 6U, data, 46U);

	// Dummy CRC
	buffer[52U] = 0x00U;
	buffer[53U] = 0x00U;

	if (m_debug)
		CUtils::dump(1U, "M17 Network Transmitted", buffer, 54U);

	return 54 != Host2Gate.Write(buffer, 54);
}

void CM17Network::clock(unsigned int ms)
{
	unsigned char buffer[BUFFER_LENGTH];

	auto length = Gate2Host.Read(buffer, BUFFER_LENGTH);
	if (length <= 0)
		return;

	if (m_debug)
		CUtils::dump(1U, "M17 Network Received", buffer, length);

	else if (0 != ::memcmp(buffer + 0U, "M17 ", 4U))
	{
		CUtils::dump(2U, "M17, received unknown packet", buffer, length);
		return;
	}

	if (!m_enabled)
		return;

	uint16_t id = (buffer[4U] << 8) + (buffer[5U] << 0);
	if (m_inId == 0U)
	{
		m_inId = id;
	}
	else
	{
		if (id != m_inId)
			return;
	}

	unsigned char c = length - 6U;
	m_buffer.addData(&c, 1U);

	m_buffer.addData(buffer + 6U, length - 6U);
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
	LogMessage("Closing M17 network connection");
	Gate2Host.Close();
}

void CM17Network::reset()
{
	m_outId = 0U;
	m_inId  = 0U;
}

void CM17Network::enable(bool enabled)
{
	if (enabled && !m_enabled)
		reset();
	else if (!enabled && m_enabled)
		m_buffer.clear();

	m_enabled = enabled;
}
