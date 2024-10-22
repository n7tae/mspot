// Copyright (C) 2021 by Jonathan Naylor G4KLX

/****************************************************************
 *                                                              *
 *             More - An M17-only Repeater/HotSpot              *
 *                                                              *
 *         Copyright (c) 2024 by Thomas A. Early N7TAE          *
 *                                                              *
 * See the LICENSE file for details about the software license. *
 *                                                              *
 ****************************************************************/

#pragma once

#include "SerialPort.h"

#include "RingBuffer.h"

class CNullController : public ISerialPort
{
public:
	CNullController();
	virtual ~CNullController();

	virtual bool open();

	virtual int read(unsigned char* buffer, unsigned int length);

	virtual int write(const unsigned char* buffer, unsigned int length);

	virtual void close();

private:
	CRingBuffer<unsigned char> m_buffer;

	void writeVersion();
	void writeStatus();
	void writeAck(unsigned char type);
};
