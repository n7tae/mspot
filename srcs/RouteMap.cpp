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

#include "RouteMap.h"
#include "Configure.h"
#include "Utilities.h"

extern CConfigure g_Cfg;

CM17RouteMap::CM17RouteMap() {}

CM17RouteMap::~CM17RouteMap()
{
	baseMap.clear();
}

const SHost *CM17RouteMap::Find(const std::string &cs) const
{
	std::string base;
	auto pos = cs.find_first_of(" /.");
	if (pos < 3)
		return nullptr;
	base.assign(cs, 0, pos);
	auto bit = baseMap.find(cs);
	if (bit != baseMap.end())
		return &bit->second;
	return nullptr;
}

void CM17RouteMap::Update(const std::string &cs, const std::string &ip4addr, const std::string &ip6addr, const std::string &mods, const std::string &smods, const uint16_t port)
{
	std::string base;
	auto pos = cs.find_first_of(" /.");
	if (pos < 3)
		return;
	base.assign(cs, 0, pos);
	auto host = &baseMap[base];
	host->cs.assign(base);
	host->ip4addr.assign(ip4addr);
	host->mods.assign(mods);
	host->ip6addr.assign(ip6addr);
	host->smods.assign(smods);
	host->port = port;
}

void CM17RouteMap::ReadAll()
{
	baseMap.clear();
	Read(g_Cfg.GetString(g_Keys.gateway.hostPath));
	Read(g_Cfg.GetString(g_Keys.gateway.myHostPath));
}

void CM17RouteMap::Read(const std::string &path)
{
	std::ifstream file(path, std::ifstream::in);
	if (file.is_open()) {
		std::string line;
		while (getline(file, line))
		{
			trim(line);
			if (0==line.size() || '#'==line[0]) continue;
			std::vector<std::string> elem;
			split(line, ' ', elem);
			Update(elem[0], elem[1], elem[2], elem[3], elem[4], std::stoul(elem[5]));
		}
		file.close();
	}
}

const std::list<std::string> CM17RouteMap::GetKeys() const
{
	std::list<std::string> keys;
	for (const auto &pair : baseMap)
		keys.push_back(pair.first);
	return keys;
}

size_t CM17RouteMap::Size() const
{
	return baseMap.size();
}
