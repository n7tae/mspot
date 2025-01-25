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

#include <fstream>
#include <regex>

#include "Configure.h"
#include "Log.h"
#include "HostMap.h"

extern CConfigure g_Cfg;

// trim from start (in place)
static inline void ltrim(std::string &s)
{
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](int ch) {
        return !std::isspace(ch);
    }));
}

// trim from end (in place)
static inline void rtrim(std::string &s)
{
    s.erase(std::find_if(s.rbegin(), s.rend(), [](int ch) {
        return !std::isspace(ch);
    }).base(), s.end());
}

// trim from both ends (in place)
static inline void trim(std::string &s)
{
    ltrim(s);
    rtrim(s);
}

CHostMap::CHostMap() {}

CHostMap::~CHostMap()
{
	baseMap.clear();
}

const SHost *CHostMap::Find(const std::string &cs) const
{
	std::string base;
	if (getBase(cs, base))
		return nullptr;
	auto bit = baseMap.find(base);
	if (bit != baseMap.end())
		return &bit->second;
	return nullptr;
}

bool CHostMap::getBase(const std::string &cs, std::string &base) const
{
	auto pos = cs.find_first_of(" /.");
	if (pos < 3)
	{
		LogWarning("'%s' is not a callsign!", cs.c_str());
		return true;
	}
	if (pos > 8)
		pos = 8;
	base.assign(cs.substr(0, pos));
	return false;
}

void CHostMap::Update(const std::string &cs, const std::string &ip4, const std::string &ip6, const std::string &mods, const std::string &smods, const std::string &src, const uint16_t port)
{
	std::string base;
	if (getBase(cs, base))
		return;
	const std::string null("null");
	auto host = &baseMap[base];
	host->cs.assign(base);
	if (ip4.compare(null) and hasIPv4)
		host->ipv4address.assign(ip4);
	else
		host->ipv4address.clear();
	if (ip6.compare(null) and hasIPv6)
		host->ipv6address.assign(ip6);
	else
		host->ipv6address.clear();
	if (mods.compare(null))
		host->mods.assign(mods);
	else
		host->mods.clear();
	if (smods.compare(null))
		host->smods.assign(smods);
	else
		host->smods.clear();
	host->port = port;
	// make sure there is an IP path
	if (host->ipv4address.empty() and host->ipv6address.empty())
		baseMap.erase(base);
}

void CHostMap::ReadAll()
{
	baseMap.clear();
	hasIPv4 = g_Cfg.GetBoolean(g_Keys.gateway.section, g_Keys.gateway.ipv4);
	hasIPv6 = g_Cfg.GetBoolean(g_Keys.gateway.section, g_Keys.gateway.ipv6);
	Read(g_Cfg.GetString(g_Keys.gateway.section, g_Keys.gateway.hostPath));
	Read(g_Cfg.GetString(g_Keys.gateway.section, g_Keys.gateway.myHostPath));
}

void CHostMap::Read(const std::string &path)
{
	std::ifstream file(path, std::ifstream::in);
	if (file.is_open())
	{
		unsigned count = 0;
		std::string line;
		while (getline(file, line))
		{
			count++;
			trim(line);
			if (0==line.size() || '#'==line[0]) continue;
			std::istringstream ss(line);
			std::vector<std::string> elem(std::istream_iterator<std::string>{ss}, std::istream_iterator<std::string>());
			if (elem.size() == 6)
				elem.emplace_back("me");
			if (elem.size() > 6)
				Update(elem[0], elem[1], elem[2], elem[3], elem[4], elem[6], std::stoul(elem[5]));
			else
				LogWarning("Line #%u of %s has %u elements, needs at least 6", count, path.c_str(), elem.size());

		}
		file.close();
	}
	else
		LogWarning("Could not open file '%s'", path.c_str());
}

const std::list<std::string> CHostMap::GetKeys() const
{
	std::list<std::string> keys;
	for (const auto &pair : baseMap)
		keys.push_back(pair.first);
	return keys;
}

size_t CHostMap::Size() const
{
	return baseMap.size();
}
