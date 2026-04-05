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

#include <stdio.h>
#include <sqlite3.h>
#include <string>
#include <list>
#include <cstdint>

#include "Base.h"
#include "Target.h"
#include "LineTools.h"
#include "SockAddress.h"

class CMspotDB : public CBase, public CLineTools
{
public:
	CMspotDB() : db(NULL) {}
	~CMspotDB() { if (db) sqlite3_close(db); }
	bool Open(const char *name);
	bool UpdateLH(const char *src, const char *dst, bool isstream, const char *from, unsigned framecount = 0);
	bool UpdateLH(const char *src, unsigned framecount);
	bool UpdatePosition(const char *callsign, const char *maidenhead, const std::string &latitude, const std::string &longitude);
	bool UpdateLS(const char *address, uint16_t port, const char *to_callsign);
	bool GetLS(std::string &address, uint16_t &port, std::string &target, time_t &connect_time);
	bool GetTarget(const char *name, EDataType &dType, ETypeVersion &tVersion, std::string &mods, std::string &smods, CSockAddress &addr);
	void ClearTable(const char *table);
	void UpdateGW(const std::string &cs, const std::string &capabilities, const CSockAddress &addr);
	bool UpdateGW(const std::string &name, const std::string &version, const std::string &mods, const std::string &smods, const std::string &ipaddress, uint16_t port, const std::string &url);
	int FillGW(const char *pathname);
	#ifdef DVREF
	int ParseJsonFile(const std::string &path);
	#endif
	int Count(const char *table);

private:
	bool Init();
	bool hasIPv4, hasIPv6;
	const char *getCapabilities(const std::string &cs, const std::string &ver);
	bool execSqlCmd(const std::string &cmd, const std::string &where);

	sqlite3 *db;
};
