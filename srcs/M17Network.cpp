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

#include "SafePacketQueue.h"
#include "M17Network.h"
#include "M17Defines.h"
#include "M17Utils.h"
#include "Defines.h"
#include "Utils.h"
#include "Log.h"

#include <cstdio>
#include <cassert>
#include <cstring>

extern IPFrameFIFO Host2Gate;
extern IPFrameFIFO Gate2Host;

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

	return true;
}

bool CM17Network::write(const unsigned char* data)
{
	assert(data != NULL);

	auto frame = std::make_unique<SIPFrame>();

	memcpy(frame->data.magic, "M17 ", 4u);
	memcpy(frame->data.magic+6, data, 46u);
	frame->SetCRC(0u);	// dummy CRC

	// Create a random id for this transmission if needed
	if (m_outId == 0U) {
		std::uniform_int_distribution<uint16_t> dist(0x0001, 0xFFFE);
		m_outId = dist(m_random);
	}
	frame->SetStreamID(m_outId);

	if (m_debug)
		CUtils::dump(1U, "M17 Network Transmitted", frame->data.magic, 54U);

	Host2Gate.Push(std::move(frame));
}

void CM17Network::clock(unsigned int ms)
{

	auto Frame = Gate2Host.Pop();
	if (nullptr == Frame)
		return;

	if (m_debug)
		CUtils::dump(1U, "M17 Network Received", Frame->data.magic, 54u);

	else if (0 != ::memcmp(Frame->data.magic, "M17 ", 4U))
	{
		CUtils::dump(2U, "M17, received unknown packet", Frame->data.magic, 54u);
		Frame.reset();
		return;
	}

	if (!m_enabled)
		return;

	uint16_t id = Frame->GetStreamID();
	if (m_inId == 0U)
	{
		m_inId = id;
	}
	else if (id != m_inId)
	{
		Frame.reset();
		return;
	}

	unsigned char c = 48u;
	m_buffer.addData(&c, 1U);

	m_buffer.addData(Frame->data.magic+6, 42u);
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
