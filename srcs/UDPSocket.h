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

#include "SockAddress.h"

#define UDP_BUFFER_LENMAX 1024

class CUDPSocket
{
public:
	CUDPSocket();

	~CUDPSocket();

	bool Open(const CSockAddress &addr);
	void Close(void);

	int GetSocket(void) const { return m_fd; }
	unsigned short GetPort() const { return m_addr.GetPort(); }

	ssize_t Read(unsigned char *buf, const size_t size, CSockAddress &addr);
	ssize_t Write(const void *buf, const size_t size, const CSockAddress &addr) const;

protected:
	int m_fd;
	CSockAddress m_addr;
};
