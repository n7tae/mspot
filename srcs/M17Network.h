//   Copyright (C) 2020,2021 by Jonathan Naylor G4KLX
/****************************************************************
 *                                                              *
 *             More - An M17-only Repeater/HotSpot              *
 *                                                              *
 *         Copyright (c) 2024 by Thomas A. Early N7TAE          *
 *                                                              *
 * See the LICENSE file for details about the software license. *
 *                                                              *
 ****************************************************************/

#ifndef	M17Network_H
#define	M17Network_H

#include "M17Defines.h"
#include "RingBuffer.h"
#include "UnixDgramSocket.h"
#include "Timer.h"

#include <random>
#include <cstdint>

class CM17Network {
public:
	CM17Network(bool debug);
	~CM17Network();

	bool open();

	void enable(bool enabled);

	bool write(const unsigned char* data);

	bool read(unsigned char* data);

	void reset();

	void close();

	void clock(unsigned int ms);

private:
	CUnixDgramReader Gate2Host;
	CUnixDgramWriter Host2Gate;
	bool             m_debug;
	bool             m_enabled;
	uint16_t         m_outId;
	uint16_t         m_inId;
	CRingBuffer<unsigned char> m_buffer;
	std::mt19937     m_random;
};

#endif
