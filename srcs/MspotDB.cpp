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

#include <string>
#include <sstream>
#include <fstream>
#include <thread>
#include <vector>
#include <algorithm>

#include "MspotDB.h"
#include "Configure.h"

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

static inline void split(const std::string &s, char delim, std::vector<std::string> &v)
{
	std::istringstream iss(s);
	std::string item;
	while (std::getline(iss, item, delim))
		v.push_back(item);
}

bool CMspotDB::Open(const char *name)
{
	if (sqlite3_open_v2(name, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, NULL))
	{
		Log(EUnit::db, "Open: can't open %s\n", name);
		return true;
	}
	auto rval = sqlite3_busy_timeout(db, 1000);
	if (SQLITE_OK != rval)
	{
		Log(EUnit::db, "sqlite3_busy_timeout returned %d\n", rval);
	}

	return Init();
}

bool CMspotDB::execSqlCmd(const std::string &cmd)
{
	char *eMsg;
	if (SQLITE_OK != sqlite3_exec(db, cmd.c_str(), NULL, 0, &eMsg))
	{
		Log(EUnit::db, "Init [%s] error: %s\n", cmd.c_str(), eMsg);
		sqlite3_free(eMsg);
		return true;
	}
	return false;
}

bool CMspotDB::Init()
{
	char *eMsg;

	std::string sql("DROP TABLE IF EXISTS lastheard;");
	
	if (SQLITE_OK != sqlite3_exec(db, sql.c_str(), NULL, 0, &eMsg))
	{
		Log(EUnit::db, "Init [%s] error: %s\n", sql.c_str(), eMsg);
		sqlite3_free(eMsg);
		return true;
	}

	sql.assign("CREATE TABLE lastheard("
					"src TEXT PRIMARY KEY, "
					"dst TEXT NOT NULL, "
					"framecount INT, "
					"mode TEXT NOT NULL, "
					"maidenhead TEXT DEFAULT '      ', "
					"latitude TEXT, "
					"longitude TEXT, "
					"fromnode TEXT NOT NULL, "
					"lasttime INT NOT NULL"
					") WITHOUT ROWID;");

	if (SQLITE_OK != sqlite3_exec(db, sql.c_str(), NULL, 0, &eMsg))
	{
		Log(EUnit::db, "Init [%s] error: %s\n", sql.c_str(), eMsg);
		sqlite3_free(eMsg);
		return true;
	}

	sql.assign("DROP TABLE IF EXISTS linkstatus;");
	
	if (SQLITE_OK != sqlite3_exec(db, sql.c_str(), NULL, 0, &eMsg))
	{
		Log(EUnit::db, "Init [%s] error: %s\n", sql.c_str(), eMsg);
		sqlite3_free(eMsg);
		return true;
	}

	sql.assign("CREATE TABLE linkstatus("
			   "reflector TEXT PRIMARY KEY, "
			   "address TEXT NOT NULL, "
			   "port INT NOT NULL, "
			   "linked_time INT NOT NULL"
			   ") WITHOUT ROWID;");

	if (SQLITE_OK != sqlite3_exec(db, sql.c_str(), NULL, 0, &eMsg))
	{
		Log(EUnit::db, "Init [%s] error: %s\n", sql.c_str(), eMsg);
		sqlite3_free(eMsg);
		return true;
	}

	sql.assign("DROP TABLE IF EXISTS targets;");
	
	if (SQLITE_OK != sqlite3_exec(db, sql.c_str(), NULL, 0, &eMsg))
	{
		Log(EUnit::db, "Init [%s] error: %s\n", sql.c_str(), eMsg);
		sqlite3_free(eMsg);
		return true;
	}

	sql.assign("CREATE TABLE targets("
			   "name TEXT PRIMARY KEY, "
			   "address TEXT NOT NULL, "
			   "mods TEXT DEFAULT '', "
			   "smods TEXT DEFAULT '', "
			   "port INT NOT NULL"
			   ") WITHOUT ROWID;");

	if (SQLITE_OK != sqlite3_exec(db, sql.c_str(), NULL, 0, &eMsg))
	{
		Log(EUnit::db, "Init [%s] error: %s\n", sql.c_str(), eMsg);
		sqlite3_free(eMsg);
		return true;
	}
	return false;
}

static int countcallback(void *count, int /*argc*/, char **argv, char ** /*azColName*/)
{
	auto c = (int *)count;
	*c = atoi(argv[0]);
	return 0;
}

bool CMspotDB::UpdateLH(const char *src, const char *dst, bool isstream, const char *fromnode, unsigned framecount)
{
	if (NULL == db)
		return false;
	std::stringstream sql;
	sql << "SELECT COUNT(*) FROM lastheard WHERE src='" << src << "';";

	int count = 0;

	char *eMsg;
	if (SQLITE_OK != sqlite3_exec(db, sql.str().c_str(), countcallback, &count, &eMsg))
	{
		Log(EUnit::db, "UpdateLH [%s] error: %s\n", sql.str().c_str(), eMsg);
		sqlite3_free(eMsg);
		return true;
	}

	sql.clear();

	const char *mode = isstream ? "Str" : "Pkt";
	if (count)
	{
		sql << "UPDATE lastheard SET dst = '" << dst << "', mode = '" << mode << "', fromnode = '" << fromnode << "', lasttime = strftime('%s','now'), framecount = " <<framecount << " WHERE src = '" << src << "';";
	}
	else
	{
		sql << "INSERT INTO lastheard (src, dst, mode, fromnode, lasttime) VALUES ('" << src << "', '" << dst << "', '" << mode << "', '" << fromnode << "', strftime('%s','now'));";
	}

	if (SQLITE_OK != sqlite3_exec(db, sql.str().c_str(), NULL, 0, &eMsg))
	{
		Log(EUnit::db, "UpdateLH [%s] error: %s\n", sql.str().c_str(), eMsg);
		sqlite3_free(eMsg);
		return true;
	}

	return false;
}

bool CMspotDB::UpdateLH(const char *src, unsigned framecount)
{
	if (NULL == db)
		return false;
	std::stringstream sql;
	sql << "UPDATE lastheard SET framecount = " << framecount << ", lasttime = strftime('%s','now') WHERE src='" << src << "';";

	char *eMsg;
	if (SQLITE_OK != sqlite3_exec(db, sql.str().c_str(), NULL, 0, &eMsg))
	{
		Log(EUnit::db, "framecount update [%s] error: %s\n", sql.str().c_str(), eMsg);
		sqlite3_free(eMsg);
		return true;
	}

	return false;
}

bool CMspotDB::UpdatePosition(const char *src, const char *maidenhead, const std::string &latitude, const std::string &longitude)
{
	if (NULL == db)
		return false;
	std::stringstream sql;
	sql << "UPDATE lastheard SET maidenhead = '" << maidenhead << "', latitude = '" << latitude << "', longitude = '" << longitude << "', lasttime = strftime('%s','now') WHERE src='" << src << "';";

	char *eMsg;
	if (SQLITE_OK != sqlite3_exec(db, sql.str().c_str(), NULL, 0, &eMsg))
	{
		Log(EUnit::db, "UpdatePosition [%s] error: %s\n", sql.str().c_str(), eMsg);
		sqlite3_free(eMsg);
		return true;
	}

	return false;
}

bool CMspotDB::UpdateLS(const char *address, uint16_t port, const char *reflector)
{
	if (NULL == db)
		return false;
	std::stringstream sql;
	sql << "INSERT OR REPLACE INTO linkstatus (reflector, address, port, linked_time) VALUES ('" << reflector << "', '" << address << "', " << port << ", strftime('%s','now'));";
	char *eMsg;
	if (SQLITE_OK != sqlite3_exec(db, sql.str().c_str(), NULL, 0, &eMsg))
	{
		Log(EUnit::db, "UpdateLS [%s] error: %s\n", sql.str().c_str(), eMsg);
		sqlite3_free(eMsg);
		return true;
	}

	return false;
}

bool CMspotDB::UpdateGW(const std::string &name, const std::string &address, const std::string &mods, const std::string &smods, uint16_t port)
{
	if (NULL == db)
		return true;
	std::stringstream sql;
	sql << "INSERT OR REPLACE INTO targets (name, address, mods, smods, port) VALUES ('" << name.c_str() << "', '" << address.c_str() << "', '" << mods.c_str() <<"', '" << smods.c_str() << "', " << port << ");";

	char *eMsg;
	if (SQLITE_OK != sqlite3_exec(db, sql.str().c_str(), NULL, 0, &eMsg))
	{
		Log(EUnit::db, "UpdateGW [%s] error: %s\n", sql.str().c_str(), eMsg);
		sqlite3_free(eMsg);
		return true;
	}

	return false;
}

bool CMspotDB::GetLS(std::string &address, uint16_t &port, std::string &target, time_t &linked_time)
{
	if (NULL == db)
		return false;
	std::stringstream sql;
	sql << "SELECT address,port,target,linked_time FROM linkstatus;";

	sqlite3_stmt *stmt;
	int rval = sqlite3_prepare_v2(db, sql.str().c_str(), -1, &stmt, 0);
	if (SQLITE_OK != rval)
	{
		Log(EUnit::db, "FindLS [%s] error\n", sql.str().c_str());
		return true;
	}

	if (SQLITE_ROW == sqlite3_step(stmt))
	{
		address.assign((const char *)sqlite3_column_text(stmt, 0));
		port = uint16_t(sqlite3_column_int(stmt, 1));
		target.assign((const char *)sqlite3_column_text(stmt, 2));
		linked_time = time_t(sqlite3_column_int(stmt, 3));
	}

	sqlite3_finalize(stmt);
	return false;
}

bool CMspotDB::GetTarget(const char *name, std::string &address, std::string &mods, std::string &smods, uint16_t &port)
{
	if (NULL == db)
		return false;
	std::stringstream sql;
	sql << "SELECT address, mods, smods, port FROM targets WHERE name=='" << name << "';";

	sqlite3_stmt *stmt;
	int rval = sqlite3_prepare_v2(db, sql.str().c_str(), -1, &stmt, 0);
	if (SQLITE_OK != rval)
	{
		Log(EUnit::db, "FindGW error: %d\n", rval);
		return false;
	}

	if (SQLITE_ROW == sqlite3_step(stmt))
	{
		address.assign((const char *)sqlite3_column_text(stmt, 0));
		mods.assign((const char *)sqlite3_column_text(stmt, 1));
		smods.assign((const char *)sqlite3_column_text(stmt, 2));
		port = (uint16_t)(sqlite3_column_int(stmt, 3));
		sqlite3_finalize(stmt);
		return true;
	}
	else
	{
		sqlite3_finalize(stmt);
		return false;
	}
}

void CMspotDB::UpdateGW(const std::string &src, const CSockAddress &addr)
{
	UpdateGW(src, addr.GetAddress(), "", "", addr.GetPort());
}

int CMspotDB::FillGW(const char *pname)
{
	int added = 0;
	if (NULL == db)
		return added;

	const auto hasIPv6 = g_Cfg.GetBoolean(g_Keys.gateway.section, g_Keys.gateway.ipv6);
	std::ifstream file(pname, std::ifstream::in);
	if (file.is_open())
	{
		unsigned lineno = 0;
		std::string line;
		while (getline(file, line))
		{
			lineno++;
			trim(line);
			if (0==line.size() || '#'==line[0]) continue;

			std::vector<std::string> elem;
			split(line, ';', elem);
			if (elem.size() == 10 or elem.size() == 9) {
				if (hasIPv6 and (not elem[4].empty())) {
					if (not UpdateGW(elem[0], elem[4], elem[5], elem[6], std::stoul(elem[7])))
						added++;
				} else if (not elem[3].empty()) {
					if (not UpdateGW(elem[0], elem[3], elem[5], elem[6], std::stoul(elem[7])))
						added++;
				} else
					Log(EUnit::db, "Gateway %s at line %u does not have a compatible IP address\n", elem[0].c_str(), lineno);
			} else if (elem.size() == 3) {
				if (not UpdateGW(elem[0], elem[1], "", "", std::stoul(elem[3])))
					added++;
			}
		}
		file.close();
	}
	else
		Log(EUnit::db, "Could not open file '%s'\n", pname);
	return added;
}

void CMspotDB::ClearTable(const char *table)
{
	if (NULL == db)
		return;

	char *eMsg;

	std::string sql("DELETE FROM ");
	sql.append(table);
	sql.append(";");
	if (SQLITE_OK != sqlite3_exec(db, sql.c_str(), NULL, 0, &eMsg))
	{
		Log(EUnit::db, "ClearTable(%s) error: %s\n", table, eMsg);
		sqlite3_free(eMsg);
	}
}

int CMspotDB::Count(const char *table)
{
	if (NULL == db)
		return 0;

	std::string sql("SELECT COUNT(*) FROM ");
	sql.append(table);
	sql.append(";");

	int count = 0;

	char *eMsg;
	if (SQLITE_OK != sqlite3_exec(db, sql.c_str(), countcallback, &count, &eMsg))
	{
		Log(EUnit::db, "Count error: %s\n", eMsg);
		sqlite3_free(eMsg);
	}

	return count;
}
