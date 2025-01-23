/****************************************************************
 *                                                              *
 *            mspot - An M17-only Hotspot/Repeater              *
 *                                                              *
 *         Copyright (c) 2024 by Thomas A. Early N7TAE          *
 *                                                              *
 * See the LICENSE file for details about the software license. *
 *                                                              *
 ****************************************************************/

#include <string.h>

#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>

#include "Log.h"
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
		LogError("socket() on %s: %s", addr.GetAddress(), strerror(errno));
		return true;
	}

	if (0 > fcntl(m_fd, F_SETFL, O_NONBLOCK))
	{
		LogError("cannon set socket %s to non-blocking", addr.GetAddress());
		close(m_fd);
		m_fd = -1;
		return true;
	}

	const int reuse = 1;
	if (0 > setsockopt(m_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(int)))
	{
		LogError("setsockopt() on %s err: %s", addr.GetAddress(), strerror(errno));
		close(m_fd);
		m_fd = -1;
		return true;
	}

	// initialize sockaddr struct
	m_addr = addr;

	if (0 != bind(m_fd, m_addr.GetCPointer(), m_addr.GetSize()))
	{
		LogError("bind() on %s err: %s", addr.GetAddress(), strerror(errno));
		close(m_fd);
		m_fd = -1;
		return true;
	}

	if (0 == m_addr.GetPort()) {	// get the assigned port for an ephemeral port request
		CSockAddress a;
		socklen_t len = sizeof(struct sockaddr_storage);
		if (getsockname(m_fd, a.GetPointer(), &len)) {
			LogError("getsockname()) on %s err: %s", addr.GetAddress(), strerror(errno));
			Close();
			return false;
		}
		if (a != m_addr)
			LogWarning("getsockname didn't return the same address as set: returned %s, should have been %s", a.GetAddress(), m_addr.GetAddress());

		m_addr.SetPort(a.GetPort());
	}

	return false;
}

void CUDPSocket::Close(void)
{
	if ( m_fd >= 0 )
	{
		LogInfo("Closing socket %d on %s", m_fd, m_addr.GetAddress());
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
		LogError("recvfrom() on %s: %s", m_addr.GetAddress(), strerror(errno));

	return rval;
}

ssize_t CUDPSocket::Write(const void *Buffer, const size_t size, const CSockAddress &Ip) const
{
	//std::cout << "Sent " << size << " bytes to " << Ip << std::endl;
	auto rval = sendto(m_fd, Buffer, size, 0, Ip.GetCPointer(), Ip.GetSize());
	if (0 > rval)
		LogError("sendto() on %s: %s", Ip.GetAddress(), strerror(errno));
	else if ((size_t)rval != size)
		LogWarning("Short Write, %d < %u to %s", rval, size, Ip.GetAddress());
	return rval;
}
