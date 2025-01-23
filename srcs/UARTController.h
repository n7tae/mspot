/*
 *   Copyright (C) 2002-2004,2007-2009,2011-2013,2015-2017,2020,2021 by Jonathan Naylor G4KLX
 *   Copyright (C) 1999-2001 by Thomas Sailor HB9JNX
 */

 /****************************************************************
 *                                                              *
 *            mspot - An M17-only Hotspot/Repeater              *
 *                                                              *
 *         Copyright (c) 2024 by Thomas A. Early N7TAE          *
 *                                                              *
 * See the LICENSE file for details about the software license. *
 *                                                              *
 ****************************************************************/


#pragma once

#include "BasePort.h"

#include <string>

class CUARTController : public CBasePort
{
public:
	CUARTController(const std::string& device, unsigned int speed, bool assertRTS = false);
	virtual ~CUARTController();

	virtual bool open();

	virtual int read(unsigned char* buffer, unsigned int length);

	virtual int write(const unsigned char* buffer, unsigned int length);

	virtual void close();

protected:
	CUARTController(unsigned int speed, bool assertRTS = false);

	std::string    m_device;
	unsigned int   m_speed;
	bool           m_assertRTS;
	int            m_fd;

	bool canWrite();
	bool setRaw();
};
