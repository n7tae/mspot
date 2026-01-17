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

#include <unistd.h>
#include <string.h>
#include <cstring>
#include <thread>
#include <chrono>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "UnixDgramSocket.h"

// globlally declared unix socket names
const char *Modem2Gate = "Modem2Gate";
const char *Gate2Modem = "Gate2Modem";

CUnixDgramReader::CUnixDgramReader() : fd(-1) {}

CUnixDgramReader::~CUnixDgramReader()
{
	Close();
}

bool CUnixDgramReader::Open(const char *path)	// returns true on failure
{
	fd = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (fd < 0)
	{
		printMsg(nullptr, TC_RED, "socket failed for %s: %s\n", path, strerror(errno));
		return true;
	}
	//fcntl(fd, F_SETFL, O_NONBLOCK);

	struct sockaddr_un addr;
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path+1, path, sizeof(addr.sun_path)-2);

	// We know path is a string, so we skip the first null, get the string length and add 1 for the begining Null
	int path_len = sizeof(addr.sun_family) + strlen(addr.sun_path + 1) + 1;
	int rval = bind(fd, (struct sockaddr *)&addr, path_len);
	if (rval < 0)
	{
		printMsg(nullptr, TC_RED, "bind() failed for %s: %s\n", path, strerror(errno));
		close(fd);
		fd = -1;
		return true;
	}
	printMsg(nullptr, TC_GREEN, "OK\n");
	return false;
}

ssize_t CUnixDgramReader::Read(uint8_t *pack, size_t size, const char *where) const
{
	auto len = read(fd, pack, size);
	if (len < 0) {
		printMsg(TC_RED, TC_YELLOW, "read() error in %s: %s\n", where, strerror(errno));
		return len;
	}

	return len;
}

void CUnixDgramReader::Close()
{
	if (fd >= 0)
		close(fd);
	fd = -1;
}

int CUnixDgramReader::GetFD() const
{
	return fd;
}

CUnixDgramWriter::CUnixDgramWriter() {}

CUnixDgramWriter::~CUnixDgramWriter() {}

void CUnixDgramWriter::SetUp(const char *path)	// returns true on failure
{
	// setup the socket address
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path+1, path, sizeof(addr.sun_path)-2);
	path_len = sizeof(addr.sun_family) + strlen(addr.sun_path + 1) + 1;
}

bool CUnixDgramWriter::Send(const uint8_t *pack, size_t size) const
{
	auto len = Write(pack, size);

	if (len != size)
		return true;

	return false;
}

// returns true on error
bool CUnixDgramWriter::Write(const void *buf, ssize_t size) const
{
	// open the socket
	int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (fd < 0)
	{
		printMsg(TC_RED, TC_YELLOW, "socket() failed for %s: %s\n", addr.sun_path+1, strerror(errno));
		return true;
	}
	// connect to the receiver
	// We know path is a string, so we skip the first null, get the string length and add 1 for the begining Null
	int rval = connect(fd, (struct sockaddr *)&addr, path_len);
	if (rval < 0)
	{
		printMsg(TC_RED, TC_YELLOW, "connect() failed for %s: %s\n", addr.sun_path+1, strerror(errno));
		close(fd);
		return true;
	}

	auto wrote = write(fd, buf, size);
	if (0 > wrote) {
		printMsg(TC_RED, TC_YELLOW, "write() error on %s: %s\n", addr.sun_path+1, strerror(errno));
		close(fd);
		return true;
	} else if (wrote != size) {
		printMsg(TC_RED, TC_YELLOW, "Write() on %s only wrote %d of %u requested", addr.sun_path+1, wrote, size);
		close(fd);
		return true;
	}
	return false;
}
