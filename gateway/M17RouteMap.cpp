/*
 *   Copyright (c) 2020-2021 by Thomas A. Early N7TAE
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <fstream>
#include <regex>

#include "M17RouteMap.h"
#include "Utilities.h"

CM17RouteMap::CM17RouteMap() {}

CM17RouteMap::~CM17RouteMap()
{
	std::lock_guard<std::mutex> lck(mux);
	baseMap.clear();
}

const std::shared_ptr<SHost> CM17RouteMap::Find(const std::string &cs) const
{
	std::shared_ptr<SHost> rval = nullptr;
	std::string base;
	auto pos = cs.find_first_of(" /.");
	if (pos < 3)
		return rval;
	base.assign(cs, 0, pos);
	std::lock_guard<std::mutex> lck(mux);
	auto bit = baseMap.find(cs);
	if (bit != baseMap.end())
		rval = bit->second;
	return rval;
}

void CM17RouteMap::Update(bool frmjson, const std::string &cs, const std::string &ip4addr, const std::string &ip6addr, const std::string &url, const std::string &modules, const uint16_t port)
{
	std::string base;
	auto pos = cs.find_first_of(" /.");
	if (pos < 3)
		return;
	base.assign(cs, 0, pos);
	auto host = Find(base);
	if (! host)
		host = std::make_shared<SHost>();
	host->from_json = frmjson;
	host->cs.assign(base);
	if (! url.empty())
		host->url.assign(url);
	if (! ip4addr.empty() && ip4addr.compare("none"))
		host->ip4addr.assign(ip4addr);
	if (! ip6addr.empty() && ip6addr.compare("none"))
		host->ip6addr.assign(ip6addr);
	if (! modules.empty())
		host->modules.assign(modules);
	host->port = port;

	std::lock_guard<std::mutex> lck(mux);
	if (host->ip4addr.size() || host->ip6addr.size() || host->url.size() || host->modules.size())
		host->updated = true;
	baseMap[base] = host;
	//std::cout << "updating " << host->cs << ": Addr4='" << host->ip4addr << "' Addr6='" << host->ip6addr << "' URL='" << host->url << "' Modules='" << host->modules << "' Port=" << host->port << std::endl;

}

void CM17RouteMap::ReadAll()
{
	mux.lock();
	baseMap.clear();
	mux.unlock();
	ReadJson("m17refl.json");
	Read("M17Hosts.cfg");
}

void CM17RouteMap::ReadJson(const char *filename)
{
	auto ecs = std::regex(".*\"designator\":\"([A-Z0-9]{3,3})\".*", std::regex::extended);
	auto eur = std::regex(".*\"url\":\"([^\"]+)\".*", std::regex::extended);
	auto ev4 = std::regex(".*\"ipv4\":[\"]?(null|[0-9.]+)[\"]?.*", std::regex::extended);
	auto ev6 = std::regex(".*\"ipv6\":[\"]?(null|[0-9a-fA-F:]+)[\"]?.*", std::regex::extended);
	auto epo = std::regex(".*\"port\":([0-9]+).*", std::regex::extended);
	bool cs, ur, v4, v6, po;
	cs = ur = v4 = v6 = po = false;
	std::string scs, sur, sv4, sv6, spo;
	std::string path(CFGDIR);
	path.append("/");
	path.append(filename);
	std::ifstream f(path, std::ifstream::in);
	while (f.good()) {
		std::string s;
		std::smatch m;
		std::getline(f, s, ',');
		if (! cs && std::regex_search(s, m, ecs)) {
			scs.assign("M17-");
			scs.append(m[1].str());
			cs = true;
			ur = v4 = v6 = po = false;
		}
		else if (! ur && std::regex_search(s, m, eur)) {
			sur = m[1].str();
			auto pos = sur.find('\\');
			while (pos != std::string::npos) {
				sur.erase(pos, 1);
				pos = sur.find('\\');
			}
			ur = true;
		}
		else if (! v4 && std::regex_search(s, m, ev4)) {
			sv4 = m[1].str();
			if (0 == sv4.compare("null")) sv4.clear();
			v4 = true;
		}
		else if (! v6 && std::regex_search(s, m, ev6)) {
			sv6 = m[1].str();
			if (0 == sv6.compare("null")) sv6.clear();
			v6 = true;
		}
		else if (! po && std::regex_search(s, m, epo)) {
			spo = m[1].str();
			po = true;
		}
		if (cs && ur && v4 && v6 && po) {
			Update(true, scs, sv4, sv6, sur, "", std::stoul(spo));
			cs = ur = v4 = v6 = po = false;
			scs.clear(); sur.clear(); sv4.clear(); sv6.clear(); spo.clear();
		}
	}
	f.close();
}

void CM17RouteMap::Read(const char *filename)
{
	std::string path(CFGDIR);
	path.append("/");
	path.append(filename);
	std::ifstream file(path, std::ifstream::in);
	if (file.is_open()) {
		std::string line;
		while (getline(file, line)) {
			trim(line);
			if (0==line.size() || '#'==line[0]) continue;
			std::vector<std::string> elem;
			split(line, ',', elem);
			Update(false, elem[0], elem[1], elem[2], elem[3], elem[4], std::stoul(elem[5]));
		}
		file.close();
	}
}

void CM17RouteMap::Save() const
{
	std::string path(CFGDIR);
	path.append("/M17Hosts.cfg");
	std::ofstream file(path.c_str(), std::ofstream::out | std::ofstream::trunc);
	if (file.is_open()) {
		mux.lock();
		for (const auto &pair : baseMap) {
			const auto host = pair.second;
			if (! host->from_json) {
				file << host->cs << ',' << host->ip4addr << ',' << host->ip6addr << ',' << host->url << ',' << host->modules << ',' << host->port << std::endl;
			}
		}
		file.close();
		mux.unlock();
	}
}

const std::list<std::string> CM17RouteMap::GetKeys() const
{
	std::list<std::string> keys;
	mux.lock();
	for (const auto &pair : baseMap)
		keys.push_back(pair.first);
	mux.unlock();
	return keys;
}

void CM17RouteMap::Erase(const std::string &cs)
{
	mux.lock();
	auto it = baseMap.find(cs);
	if (it != baseMap.end())
		baseMap.erase(it);
	mux.unlock();
}

size_t CM17RouteMap::Size() const
{
	return baseMap.size();
}
