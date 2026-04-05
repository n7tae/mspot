/*
	mspot - an M17 hot-spot using an  M17 CC1200 Raspberry Pi Hat
				Copyright (C) 2026 Thomas A. Early

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either capabilities 3 of the License, or
	(at your option) any later capabilities.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include <nlohmann/json.hpp>
#include <fstream>
#include <thread>
#include <vector>

#include "MspotDB.h"
#include "Configure.h"

extern CConfigure g_Cfg;

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

	hasIPv4 = g_Cfg.GetBoolean(g_Keys.gateway.section, g_Keys.gateway.ipv4);
	hasIPv6 = g_Cfg.GetBoolean(g_Keys.gateway.section, g_Keys.gateway.ipv6);

	return Init();
}

bool CMspotDB::execSqlCmd(const std::string &cmd, const std::string &where)
{
	char *eMsg;
	if (SQLITE_OK != sqlite3_exec(db, cmd.c_str(), NULL, 0, &eMsg))
	{
		Log(EUnit::db, "%s [%s] error: %s\n", where, cmd.c_str(), eMsg);
		sqlite3_free(eMsg);
		return true;
	}
	return false;
}

bool CMspotDB::Init()
{
	const std::string here("Init");
	std::string sql("DROP TABLE IF EXISTS lastheard;");
	if (execSqlCmd(sql, here))
		return true;

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
	if (execSqlCmd(sql, here))
		return true;

	sql.assign("DROP TABLE IF EXISTS linkstatus;");
	if (execSqlCmd(sql, here))
		return true;

	sql.assign("CREATE TABLE linkstatus("
		"reflector TEXT PRIMARY KEY, "
		"address TEXT NOT NULL, "
		"port INT NOT NULL, "
		"linked_time INT NOT NULL"
		") WITHOUT ROWID;");
	if (execSqlCmd(sql, here))
		return true;

	sql.assign("DROP TABLE IF EXISTS targets;");
	if (execSqlCmd(sql, here))
		return true;

	sql.assign("CREATE TABLE targets("
		"name TEXT PRIMARY KEY, "
		"capabilities TEXT DEFAULT '', "
		"mods TEXT DEFAULT '', "
		"smods TEXT DEFAULT '', "
		"ipaddress TEXT NOT NULL, "
		"port INT NOT NULL"
		"url TEXT DEFAULT '', "
		") WITHOUT ROWID;");
	if (execSqlCmd(sql, here))
		return true;

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

	if (execSqlCmd(sql.str(), "UpdateLH"))
		return true;

	return false;
}

bool CMspotDB::UpdateLH(const char *src, unsigned framecount)
{
	if (NULL == db)
		return false;
	std::stringstream sql;
	sql << "UPDATE lastheard SET framecount = " << framecount << ", lasttime = strftime('%s','now') WHERE src='" << src << "';";

	if (execSqlCmd(sql.str(), "Framecout update"))
		return true;

	return false;
}

bool CMspotDB::UpdatePosition(const char *src, const char *maidenhead, const std::string &latitude, const std::string &longitude)
{
	if (NULL == db)
		return false;
	std::stringstream sql;
	sql << "UPDATE lastheard SET maidenhead = '" << maidenhead << "', latitude = '" << latitude << "', longitude = '" << longitude << "', lasttime = strftime('%s','now') WHERE src='" << src << "';";

	if (execSqlCmd(sql.str(), "UpdatePosition"))
		return true;

	return false;
}

bool CMspotDB::UpdateLS(const char *address, uint16_t port, const char *reflector)
{
	if (NULL == db)
		return false;
	std::stringstream sql;
	sql << "INSERT OR REPLACE INTO linkstatus (reflector, address, port, linked_time) VALUES ('" << reflector << "', '" << address << "', " << port << ", strftime('%s','now'));";

	if (execSqlCmd(sql.str(), "UpdateLS"))
		return true;

	return false;
}

const char *CMspotDB::getCapabilities(const std::string &cs, const std::string &ver)
{
	if (0 == cs.find("M17-")) {
		if (ver.empty())
			return "BL";
		switch (ver.at(0)) {
			case '0': return "SL";
			case '1': return "BL";
			case '2': return "BB";
			case '3': return "B3";
			default: return "??";
		}
	} else if (0 == cs.find("URF")) {
		return "SL";
	} else { // direct routing targets
		if (2 == ver.size()) {
			switch (ver[0]) {
				case 'B': case 'S': case 'P':
					switch (ver[1]) {
						case 'L': case 'B': case '3':
							return ver.c_str();
						default: break;
					}
				default: break;
			}
		}
	}
	return "??";
}

// returns true on success
bool CMspotDB::UpdateGW(const std::string &name, const std::string &version, const std::string &mods, const std::string &smods, const std::string &address, uint16_t port, const std::string &url)
{
	if (NULL == db)
		return false;
	std::stringstream sql;
	std::string caps;
	caps.assign(getCapabilities(name, version));
	if (caps == "??") {
		Log(EUnit::db, "ERROR! Target %s with Version %s was rejected\n", name.c_str(), version.c_str());
		return false;
	}
	sql << "INSERT OR REPLACE INTO targets (name, capabilities, mods, smods, address, port, url) VALUES ('" << name.c_str() << "', '" << caps << "', '" << mods.c_str() <<"', '" << smods.c_str() << "', '"<< address.c_str() << "', "  << port << ", '" << url << "');";

	if (execSqlCmd(sql.str(), "UpdateGW"))
		return false;

	return true;
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

bool CMspotDB::GetTarget(const char *name, EDataType &dType, ETypeVersion &tVersion, std::string &mods, std::string &smods, CSockAddress &addr)
{
	if (NULL == db)
		return false;
	std::stringstream sql;
	sql << "SELECT capabilities, mods, smods, address, port FROM targets WHERE name=='" << name << "';";

	sqlite3_stmt *stmt;
	int rval = sqlite3_prepare_v2(db, sql.str().c_str(), -1, &stmt, 0);
	if (SQLITE_OK != rval)
	{
		Log(EUnit::db, "FindGW error: %d\n", rval);
		return false;
	}

	if (SQLITE_ROW == sqlite3_step(stmt))
	{
		switch (sqlite3_column_text(stmt, 0)[0])
		{
			default:
			case 'S': dType = EDataType::str_only; break;
			case 'P': dType = EDataType::pkt_only; break;
			case 'B': dType = EDataType::both;     break;
		}
		switch (sqlite3_column_text(stmt, 0)[1])
		{
			default:
			case 'L': tVersion = ETypeVersion::deprecated; break;
			case '3': tVersion = ETypeVersion::v3;         break;
			case 'B': tVersion = ETypeVersion::both;       break;
		}
		mods.assign((const char *)sqlite3_column_text(stmt, 1));
		smods.assign((const char *)sqlite3_column_text(stmt, 2));
		addr.Initialize((const char *)sqlite3_column_text(stmt, 3), uint16_t(sqlite3_column_int(stmt, 4)));
		sqlite3_finalize(stmt);
		return true;
	}
	else
	{
		sqlite3_finalize(stmt);
		return false;
	}
}

void CMspotDB::UpdateGW(const std::string &cs, const std::string &capabilities, const CSockAddress &addr)
{
	UpdateGW(cs, capabilities, "", "", addr.GetAddress(), addr.GetPort(), "");
}

int CMspotDB::FillGW(const char *pname)
{
	int added = 0;
	if (NULL == db)
		return added;

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
			// Reflector;Version;Modules;SpecialModules;IPv4Address;IPv6Address;Port;DashboardURL
			//      0       1       2           3            4         5          6      7
			if (elem.size() == 8) {
				if (hasIPv6 and (EIPType::ipv6 == g_Cfg.GetIPType(elem[5]))) {
					if (UpdateGW(elem[0], elem[1], elem[2], elem[3], elem[5], std::stoul(elem[6]), elem[7]))
						added++;
				} else if (hasIPv4 and (EIPType::ipv4 == g_Cfg.GetIPType(elem[4]))) {
					if (UpdateGW(elem[0], elem[1], elem[2], elem[3], elem[4], std::stoul(elem[6]), elem[7]))
						added++;
				} else
					Log(EUnit::db, "Reflector %s at line %u does not have a compatible IP address\n", elem[0].c_str(), lineno);
			} else if (elem.size() == 5) {
				// Callsign;Capabilities;IPv4Address;IPv6Address;Port
				//    0          1            2           3       4
				if (hasIPv6 and (EIPType::ipv6 == g_Cfg.GetIPType(elem[3]))) {
					if (UpdateGW(elem[0], elem[1], "", "", elem[3], std::stoul(elem[4]), ""))
						added++;
				} else if (hasIPv4 and (EIPType::ipv4 == g_Cfg.GetIPType(elem[2]))) {
					if (UpdateGW(elem[0], elem[1], "", "", elem[2], std::stoul(elem[4]), ""))
						added++;
				} else
					Log(EUnit::db, "Target %s at line %u does not have a compatible IP address\n", elem[0].c_str(), lineno);
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

#ifdef DVREF
using json = nlohmann::json;

#define GET_STRING(a) ((a).is_string() ? a : "")

int CMspotDB::ParseJsonFile(const std::string &filepath)
{
	json mref;
	std::ifstream jfile(filepath, std::ifstream::in);
	if (jfile.is_open()) {
		try {
			mref = json::parse(jfile);
		}
		catch (const std::exception &e) {
			Log(EUnit::db, "ERROR: %s\n", e.what());
			jfile.close();
			return 0;
		}
	} else {
		Log(EUnit::db, "ERROR: Could not open %s\n", filepath.c_str());
		return 0;
	}

	if (mref.contains("reflectors"))
	{
		unsigned ucount = 0, mcount = 0;
		for (auto &ref : mref["reflectors"])
		{
			const std::string cs(GET_STRING(ref["designator"]));
			if (0 == cs.substr(0,4).compare("M17-"))
			{
				const std::string dn(GET_STRING(ref["dns"]));
				const std::string ipv4(GET_STRING(ref["ipv4"]));
				if (0==ipv4.compare("127.0.0.1") or 0==ipv4.compare("0.0.0.0"))
					continue;
				const std::string ipv6(GET_STRING(ref["ipv6"]));
				if (0==ipv6.compare("::") or 0==ipv6.compare("::1"))
					continue;
				std::string mods(""), emods("");
				if (ref.contains("modules"))
				{
					for (const auto &item : ref["modules"])
						mods.append(item);
				}
				if (ref.contains("encrypted"))
				{
					for (const auto &item : ref["encrypted"])
						emods.append(item);
				}
				uint16_t port;
				if (ref.contains("port") and ref["port"].is_number_unsigned())
					port = ref["port"].get<uint16_t>();
				else
					continue;
				if (UpdateGW(cs, "", mods, emods, ipv4, ipv6, port, GET_STRING(ref["url"])))
					mcount++;
				}
			}
			else if (0 == cs.substr(0,3).compare("URF"))
			{
				const std::string dn(GET_STRING(ref["dns"]));
				const std::string ipv4(GET_STRING(ref["ipv4"]));
				if (0==ipv4.compare("127.0.0.1") or 0==ipv4.compare("0.0.0.0"))
					continue;
				const std::string ipv6(GET_STRING(ref["ipv6"]));
				if (0==ipv6.compare("::") or 0==ipv6.compare("::1"))
					continue;
				std::string mods(""), smods("");
				uint16_t port = 17000u;
				if (ref.contains("modules"))
				{
					for (auto &mod : ref["modules"])
					{
						auto m = mod["module"].get<std::string>();
						const std::string mode(GET_STRING(mod["mode"]));
						if (0==mode.compare("All") or 0==mode.compare("M17"))
						{
							mods.append(m);
							if (mod["transcode"].is_boolean())
							{
								if (mod["transcode"].get<bool>())
									smods.append(m);
							}
							if (0 == mode.compare("M17"))
							{
								if (mod["port"].is_number_unsigned())
									port = mod["port"].get<uint16_t>();
							}
						}
					}
				}
				if (UpdateGW(cs, "SL", mods, smods, ipv4, ipv6, port, GET_STRING(ref["url"])))
					ucount++;
			}
		}
		std::cout << "Loaded " << mcount << " M17 and " << ucount << " URF reflectors from " << filepath << std::endl;
	}
	else
	{
		std::cerr << "ERROR: No M17 reflectors found at " << filepath << std::endl;
	}
}
#endif
