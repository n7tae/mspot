/****************************************************************
 *                                                              *
 *            mspot - An M17-only Hotspot/Repeater              *
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
	std::string cs, ipv4address, ipv6address, mods, smods, source;
	uint16_t port;
};

class CHostMap
{
public:
	CHostMap();
	~CHostMap();
	const SHost *Find(const std::string &cs) const;
	void Update(const std::string &, const std::string &, const std::string &, const std::string &, const std::string &, const std::string &, const uint16_t);
	void ReadAll();
	const std::list<std::string> GetKeys() const;
	size_t Size() const;

private:
	void Read(const std::string &file);
	bool getBase(const std::string &cs, std::string &base) const;
	std::map<std::string, SHost> baseMap;
	bool hasIPv4, hasIPv6;
};
