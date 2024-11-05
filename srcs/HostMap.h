/****************************************************************
 *                                                              *
 *             More - An M17-only Repeater/HotSpot              *
 *                                                              *
 *         Copyright (c) 2024 by Thomas A. Early N7TAE          *
 *                                                              *
 * See the LICENSE file for details about the software license. *
 *                                                              *
 ****************************************************************/

#pragma once

#include <string>
#include <map>
#include <list>
#include <cstdint>

struct SHost
{
	SHost() {}
	std::string cs, ip4addr, ip6addr, mods, smods;
	uint16_t port;
};

class CHostMap
{
public:
	CHostMap();
	~CHostMap();
	const SHost *Find(const std::string &cs) const;
	void Update(const std::string &cs, const std::string &ip4addr, const std::string &ip6addr, const std::string &modules, const std::string &specialmodules, const uint16_t port);
	void ReadAll();
	const std::list<std::string> GetKeys() const;
	size_t Size() const;

private:
	void Read(const std::string &file);
	bool getBase(const std::string &cs, std::string &base) const;
	std::map<std::string, SHost> baseMap;
	bool hasIPv4, hasIPv6;
};
