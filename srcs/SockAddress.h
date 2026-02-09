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

#include <iostream>
#include <cstring>

#include <strings.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include "Base.h"

class CSockAddress : public CBase
{
public:
	CSockAddress()
	{
		Clear();
	}

	~CSockAddress() {}

	bool Initialize(const std::string &address, uint16_t port = 0)
	{
		Clear();
		struct addrinfo hints, *result;
		memset(&hints, 0, sizeof(struct addrinfo));
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_DGRAM;
		if (0 == getaddrinfo(address.c_str(), (port ? std::to_string(port).c_str() : nullptr), &hints, &result))
		{
			memcpy(&addr, result->ai_addr, result->ai_addrlen);
			addr.ss_family = result->ai_family;
			freeaddrinfo(result);
			if (port)
				SetPort(port);
			return false;
		}
		else
		{
			Log(EUnit::sock, "Could not find address for %s\n", address.c_str());
			return true;
		}
	}

	bool Initialize(const int family, const uint16_t port = 0U, const char *address = nullptr)
	{
		Clear();
		addr.ss_family = family;
		if (AF_INET == family)
		{
			auto addr4 = (struct sockaddr_in *)&addr;
			addr4->sin_port = htons(port);
			if (address)
			{
				if (0 == strncasecmp(address, "loc", 3))
					inet_pton(AF_INET, "127.0.0.1", &(addr4->sin_addr));
				else if (0 == strncasecmp(address, "any", 3))
					inet_pton(AF_INET, "0.0.0.0", &(addr4->sin_addr));
				else
				{
					if (1 > inet_pton(AF_INET, address, &(addr4->sin_addr)))
					{
						Log(EUnit::sock, "IPv4 SockAddress initialization failed for '%s'\n", address);
						return true;
					}
				}
			}
		}
		else if (AF_INET6 == family)
		{
			auto addr6 = (struct sockaddr_in6 *)&addr;
			addr6->sin6_port = htons(port);
			if (address)
			{
				if (0 == strncasecmp(address, "loc", 3))
					inet_pton(AF_INET6, "::1", &(addr6->sin6_addr));
				else if (0 == strncasecmp(address, "any", 3))
					inet_pton(AF_INET6, "::", &(addr6->sin6_addr));
				else if (address)
				{
					if (1 > inet_pton(AF_INET6, address, &(addr6->sin6_addr)))
					{
						Log(EUnit::sock, "IPv6 SockAddress initialization failed for '%s'\n", address);
						return true;
					}
				}
			}
		}
		else
		{
			addr.ss_family = AF_INET;
			Log(EUnit::sock, "Address Family must be IPv4 or IPv6\n");
			return true;
		}
		return false;
	}

	CSockAddress &operator=(const CSockAddress &from)
	{
		Clear();
		if (AF_INET == from.addr.ss_family)
			memcpy(&addr, &from.addr, sizeof(struct sockaddr_in));
		else
			memcpy(&addr, &from.addr, sizeof(struct sockaddr_in6));
		strcpy(straddr, from.straddr);
		return *this;
	}

	bool operator==(const CSockAddress &rhs) const	// doesn't compare ports, only addresses and families
	{
		if (addr.ss_family != rhs.addr.ss_family)
			return false;
		if (AF_INET == addr.ss_family) {
			auto l = (struct sockaddr_in *)&addr;
			auto r = (struct sockaddr_in *)&rhs.addr;
			return (l->sin_addr.s_addr == r->sin_addr.s_addr);
		} else if (AF_INET6 == addr.ss_family) {
			auto l = (struct sockaddr_in6 *)&addr;
			auto r = (struct sockaddr_in6 *)&rhs.addr;
			return (0 == memcmp(&(l->sin6_addr), &(r->sin6_addr), sizeof(struct in6_addr)));
		}
		return false;
	}

	bool operator!=(const CSockAddress &rhs) const	// doesn't compare ports, only addresses and families
	{
		if (addr.ss_family != rhs.addr.ss_family)
			return true;
		if (AF_INET == addr.ss_family) {
			auto l = (struct sockaddr_in *)&addr;
			auto r = (struct sockaddr_in *)&rhs.addr;
			return (l->sin_addr.s_addr != r->sin_addr.s_addr);
		} else if (AF_INET6 == addr.ss_family) {
			auto l = (struct sockaddr_in6 *)&addr;
			auto r = (struct sockaddr_in6 *)&rhs.addr;
			return (0 != memcmp(&(l->sin6_addr), &(r->sin6_addr), sizeof(struct in6_addr)));
		}
		return true;
	}

	operator const char *() const { return GetAddress(); }

	friend std::ostream &operator<<(std::ostream &stream, const CSockAddress &addr)
	{
		const char *sz = addr;
		if (AF_INET6 == addr.GetFamily())
			stream << "[" << sz << "]";
		else
			stream << sz;
		const uint16_t port = addr.GetPort();
		if (port)
			stream << ":" << port;
		return stream;
	}

	bool AddressIsZero() const
	{
		if (AF_INET == addr.ss_family) {
			 auto addr4 = (struct sockaddr_in *)&addr;
			return (addr4->sin_addr.s_addr == 0U);
		} else {
			auto addr6 = (struct sockaddr_in6 *)&addr;
			for (unsigned int i=0; i<16; i++) {
				if (addr6->sin6_addr.s6_addr[i])
					return false;
			}
			return true;
		}
	}

	const char *GetAddress() const
	{
		if (straddr[0])
			return straddr;
		if (AF_INET == addr.ss_family) {
			auto addr4 = (struct sockaddr_in *)&addr;
			inet_ntop(AF_INET, &(addr4->sin_addr), straddr, INET6_ADDRSTRLEN);
		} else if (AF_INET6 == addr.ss_family) {
			auto addr6 = (struct sockaddr_in6 *)&addr;
			inet_ntop(AF_INET6, &(addr6->sin6_addr), straddr, INET6_ADDRSTRLEN);
		} else {
			Log(EUnit::sock, "Can't get address, unknown family: %u", addr.ss_family);
			return "UNKNOWN";
		}
		return straddr;
	}

    int GetFamily() const
    {
        return addr.ss_family;
    }

	unsigned short GetPort() const
	{
		if (AF_INET == addr.ss_family) {
			auto addr4 = (struct sockaddr_in *)&addr;
			return ntohs(addr4->sin_port);
		} else if (AF_INET6 == addr.ss_family) {
			auto addr6 = (struct sockaddr_in6 *)&addr;
			return ntohs(addr6->sin6_port);
		} else
			return 0;
	}

	void SetPort(const uint16_t newport)
	{
		if (AF_INET == addr.ss_family) {
			auto addr4 = (struct sockaddr_in *)&addr;
			addr4->sin_port = htons(newport);
		} else if (AF_INET6 == addr.ss_family) {
			auto addr6 = (struct sockaddr_in6 *)&addr;
			addr6->sin6_port = htons(newport);
		}
	}

	struct sockaddr *GetPointer()
	{
		memset(straddr, 0, INET6_ADDRSTRLEN);	// things might change
		return (struct sockaddr *)&addr;
	}

	const struct sockaddr *GetCPointer() const
	{
		return (const struct sockaddr *)&addr;
	}

	size_t GetSize() const
	{
		if (AF_INET == addr.ss_family)
			return sizeof(struct sockaddr_in);
		else
			return sizeof(struct sockaddr_in6);
	}

	void Clear()
	{
		memset(&addr, 0, sizeof(struct sockaddr_storage));
		memset(straddr, 0, INET6_ADDRSTRLEN);
	}

private:
	struct sockaddr_storage addr;
	mutable char straddr[INET6_ADDRSTRLEN];
};
