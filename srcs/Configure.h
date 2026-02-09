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

#include <nlohmann/json.hpp>
#include <filesystem>
#include <cstdint>
#include <string>
#include <regex>

#include "JsonKeys.h"

extern SJsonKeys g_Keys;

enum class ErrorLevel { fatal, mild };
enum class ESection { none, repeater, modem, gateway };

#define IS_TRUE(a) ((a)=='t' || (a)=='T' || (a)=='1')

class CConfigure
{
public:
	CConfigure();
	bool ReadData(const std::string &path);
	bool Contains(const std::string &section, const std::string &key) const;
	void Dump(bool justpublic) const;
	std::string GetString(const std::string &section, const std::string &key) const;
	float GetFloat(const std::string &section, const std::string &key) const;
	unsigned GetUnsigned(const std::string &section, const std::string &key) const;
	int GetInt(const std::string &section, const std::string &key) const;
	bool GetBoolean(const std::string &section, const std::string &key) const;
	bool IsString(const std::string &section, const std::string &key) const;

	const nlohmann::json &GetData() { return data; }

private:
	// CFGDATA data;
	unsigned counter;
	nlohmann::json data;
	std::regex IPv4RegEx, IPv6RegEx, MoreCS, MrefdCS, UrfdCS;

	unsigned getUnsigned(const std::string &value, const std::string &label, unsigned min, unsigned max, unsigned defaultvalue) const;
	int getInt(const std::string &value, const std::string &label, int min, int max, int defaultvalue) const;
	float getFloat(const std::string &value, const std::string &label, float min, float max, float defaultValue) const;
	void badParam(const std::string &section, const std::string &key) const;
	void checkPath(const std::string &section, const std::string &key, const std::string &filepath, const std::filesystem::file_type type) const;
	bool isDefined(ErrorLevel level, const std::string &section, const std::string &key, bool &rval);
};
