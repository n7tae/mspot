/****************************************************************
 *                                                              *
 *             More - An M17-only Repeater/HotSpot              *
 *                                                              *
 *         Copyright (c) 2024 by Thomas A. Early N7TAE          *
 *                                                              *
 * See the LICENSE file for details about the software license. *
 *                                                              *
 ****************************************************************/

#include <fstream>
#include <regex>

#include "Configure.h"
#include "Utilities.h"
#include "Log.h"
#include "HostMap.h"

extern CConfigure g_Cfg;

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

void CHostMap::Update(const std::string &cs, const std::string &ip4addr, const std::string &ip6addr, const std::string &mods, const std::string &smods, const uint16_t port)
{
	std::string base;
	if (getBase(cs, base))
		return;
	const std::string null("null");
	auto host = &baseMap[base];
	host->cs.assign(base);
	if (ip4addr.compare(null) and hasIPv4)
		host->ip4addr.assign(ip4addr);
	else
		host->ip4addr.clear();
	if (ip6addr.compare(null) and hasIPv6)
		host->ip6addr.assign(ip6addr);
	else
		host->ip6addr.clear();
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
	if (host->ip4addr.empty() and host->ip6addr.empty())
		baseMap.erase(base);
}

void CHostMap::ReadAll()
{
	baseMap.clear();
	hasIPv4 = g_Cfg.GetBoolean(g_Keys.gateway.ipv4);
	hasIPv6 = g_Cfg.GetBoolean(g_Keys.gateway.ipv6);
	Read(g_Cfg.GetString(g_Keys.gateway.hostPath));
	Read(g_Cfg.GetString(g_Keys.gateway.myHostPath));
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
			std::vector<std::string> elem;
			split(line, ' ', elem);
			if (5 == elem.size())
				Update(elem[0], elem[1], elem[2], elem[3], elem[4], std::stoul(elem[5]));
			else
				LogWarning("Line #%u of %s doesn't have 5 elements", count, path.c_str());

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
