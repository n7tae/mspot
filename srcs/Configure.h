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

#include <nlohmann/json.hpp>
#include <cstdint>
#include <string>
#include <regex>

#include "JsonKeys.h"

extern SJsonKeys g_Keys;

enum class ErrorLevel { fatal, mild };
enum class ESection { none, general, log, modem, dht, gateway };

#define IS_TRUE(a) ((a)=='t' || (a)=='T' || (a)=='1')

class CConfigure
{
public:
	CConfigure();
	bool ReadData(const std::string &path);
	bool Contains(const std::string &key) const;
	void Dump(bool justpublic) const;
	std::string GetString(const std::string &key) const;
	unsigned GetUnsigned(const std::string &key) const;
	int GetInt(const std::string &key) const;
	bool GetBoolean(const std::string &key) const;
	bool IsString(const std::string &key) const;
	const nlohmann::json &GetData() { return data; }

private:
	// CFGDATA data;
	unsigned counter;
	nlohmann::json data;
	std::regex IPv4RegEx, IPv6RegEx, MoreCS, MrefdCS, UrfdCS;

	unsigned getUnsigned(const std::string &value, const std::string &label, unsigned min, unsigned max, unsigned defaultvalue) const;
	int getInt(const std::string &value, const std::string &label, int min, int max, int defaultvalue) const;
	void badParam(const std::string &param) const;
	void checkPath(const std::string &section, const std::string &key, const std::string &filepath, const std::filesystem::file_type type) const;
	bool isDefined(ErrorLevel level, const std::string &section, const std::string &key, bool &rval);
};
