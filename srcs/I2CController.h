/*
 *   Copyright (C) 2002-2004,2007-2009,2011-2013,2015-2017,2020,2021 by Jonathan Naylor G4KLX
 *   Copyright (C) 1999-2001 by Thomas Sailor HB9JNX
 */

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

#include <string>

class CI2CController : public ISerialPort
{
public:
	CI2CController(const std::string &device, unsigned int address = 0x22U);
	virtual ~CI2CController();

	virtual bool open();

	virtual int read(unsigned char* buffer, unsigned int length);

	virtual int write(const unsigned char* buffer, unsigned int length);

	virtual void close();

private:
	std::string  m_device;
	unsigned int m_address;
	int          m_fd;
};
