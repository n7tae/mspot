/*

         mspot - an M17-only HotSpot using an MMDVM device
            Copyright (C) 2025 Thomas A. Early N7TAE

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

*/

#pragma once

#include <cstdint>
#include <string>
#include <memory>
#include <map>

struct SHost
{
	std::string cs, version, domainname, ipv4address, ipv6address, mods, smods, source, url;
	uint16_t port;
};

class CHostMap
{
public:
	CHostMap();
	~CHostMap();
	const std::shared_ptr<SHost> Find(const std::string &cs) const;
	void ReadAll();

private:
	void Add(const std::string &cs, const std::string &version, const std::string &dn, const std::string &ipv4, const std::string &ipvs, const std::string &mods, const std::string &smods, const uint16_t port, const std::string &src, const std::string &url);
	void Read(const std::string &file);
	bool getBase(const std::string &cs, std::string &base) const;
	std::map<std::string, std::shared_ptr<SHost>> baseMap;
	bool hasIPv4, hasIPv6;
};
