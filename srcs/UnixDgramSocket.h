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

#include <cstdlib>
#include <cstdint>
#include <sys/un.h>

#include "Base.h"

class CUnixDgramReader : public CBase
{
public:
	CUnixDgramReader();
	~CUnixDgramReader();
	bool Open(const char *path);
	ssize_t Read(uint8_t *pack, size_t size, const char *where) const;
	void Close();
	int GetFD() const;

	private:
	int fd;
};

class CUnixDgramWriter : public CBase
{
public:
	CUnixDgramWriter();
	~CUnixDgramWriter();
	void SetUp(const char *path);
	bool Send(const uint8_t *pack, size_t size) const;

	private:
	bool Write(const void *buf, ssize_t size) const;
	struct sockaddr_un addr;
	int path_len;
};
