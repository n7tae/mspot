/*
	mspot - an M17 hot-spot using an  M17 CC1200 Raspberry Pi Hat
				Copyright (C) 2026 Thomas A. Early

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

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
