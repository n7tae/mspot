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

class CBasePort
{
public:
	virtual ~CBasePort() {}

	virtual bool open() = 0;

	virtual int read(unsigned char* buffer, unsigned int length) = 0;

	virtual int write(const unsigned char* buffer, unsigned int length) = 0;

	virtual void close() = 0;
};
