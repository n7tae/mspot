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

#include <string.h>

#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>

#include "UDPSocket.h"

CUDPSocket::CUDPSocket() : m_fd(-1) {}

CUDPSocket::~CUDPSocket()
{
	Close();
}

bool CUDPSocket::Open(const CSockAddress &addr)
{
	// create socket
	m_fd = socket(addr.GetFamily(), SOCK_DGRAM, 0);
	if (0 > m_fd)
	{
		printMsg(TC_YELLOW, TC_RED, "socket() on %s: %s\n", addr.GetAddress(), strerror(errno));
		return true;
	}

	if (0 > fcntl(m_fd, F_SETFL, O_NONBLOCK))
	{
		printMsg(TC_YELLOW, TC_RED, "cannot set socket %s to non-blocking\n", addr.GetAddress());
		close(m_fd);
		m_fd = -1;
		return true;
	}

	const int reuse = 1;
	if (0 > setsockopt(m_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(int)))
	{
		printMsg(TC_YELLOW, TC_RED, "setsockopt() on %s err: %s\n", addr.GetAddress(), strerror(errno));
		close(m_fd);
		m_fd = -1;
		return true;
	}

	// initialize sockaddr struct
	m_addr = addr;

	if (0 != bind(m_fd, m_addr.GetCPointer(), m_addr.GetSize()))
	{
		printMsg(TC_YELLOW, TC_RED, "bind() on %s err: %s\n", addr.GetAddress(), strerror(errno));
		close(m_fd);
		m_fd = -1;
		return true;
	}

	if (0 == m_addr.GetPort()) {	// get the assigned port for an ephemeral port request
		CSockAddress a;
		socklen_t len = sizeof(struct sockaddr_storage);
		if (getsockname(m_fd, a.GetPointer(), &len)) {
			printMsg(TC_YELLOW, TC_RED, "getsockname()) on %s err: %s\n", addr.GetAddress(), strerror(errno));
			Close();
			return false;
		}
		if (a != m_addr)
			printMsg(TC_YELLOW, TC_RED, "getsockname didn't return the same address as set: returned %s, should have been %s\n", a.GetAddress(), m_addr.GetAddress());

		m_addr.SetPort(a.GetPort());
	}

	return false;
}

void CUDPSocket::Close(void)
{
	if ( m_fd >= 0 )
	{
		printMsg(TC_YELLOW, TC_DEFAULT, "Closing socket %d on %s\n", m_fd, m_addr.GetAddress());
		close(m_fd);
		m_fd = -1;
	}
}

////////////////////////////////////////////////////////////////////////////////////////
// read

ssize_t CUDPSocket::Read(unsigned char *buf, const size_t size, CSockAddress &Ip)
{
	if ( 0 > m_fd )
		return 0;

	unsigned int len = sizeof(struct sockaddr_storage);
	auto rval = recvfrom(m_fd, buf, size, 0, Ip.GetPointer(), &len);
	if (0 > rval)
		printMsg(TC_YELLOW, TC_RED, "recvfrom() error on %s: %s\n", m_addr.GetAddress(), strerror(errno));

	return rval;
}

ssize_t CUDPSocket::Write(const void *Buffer, const size_t size, const CSockAddress &Ip) const
{
	//std::cout << "Sent " << size << " bytes to " << Ip << std::endl;
	auto rval = sendto(m_fd, Buffer, size, 0, Ip.GetCPointer(), Ip.GetSize());
	if (0 > rval)
		printMsg(TC_YELLOW, TC_RED, "sendto() error on %s: %s\n", Ip.GetAddress(), strerror(errno));
	else if ((size_t)rval != size)
		printMsg(TC_YELLOW, TC_RED, "Short Write, %d < %u to %s\n", rval, size, Ip.GetAddress());
	return rval;
}
