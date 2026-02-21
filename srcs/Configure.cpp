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

#include <filesystem>
#include <pwd.h>
#include <stdlib.h>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <cstring>
#include <vector>
#include <list>
#include <map>
#include <algorithm>
#include <regex>

#include "Configure.h"

// the global definition
SJsonKeys  g_Keys;

static inline void split(const std::string &s, char delim, std::vector<std::string> &v)
{
	std::istringstream iss(s);
	std::string item;
	while (std::getline(iss, item, delim))
		v.push_back(item);
}

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

CConfigure::CConfigure()
{
	MoreCS  = std::regex("^[0-9]?[A-Z]{1,2}[0-9]{1,2}[A-Z]{1,4}([./-][0-9A-Z])?[ ]*$", std::regex::extended);
	MrefdCS = std::regex("^M17-[A-Z0-9]{3,3} [A-Z]$", std::regex::extended);
	UrfdCS  = std::regex("^URF[A-Z0-9]{3,3}  [A-Z]$", std::regex::extended);
	IPv4RegEx = std::regex("^((25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[1-9][0-9]|[0-9])\\.){3,3}(25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[1-9][0-9]|[0-9]){1,1}$", std::regex::extended);
	IPv6RegEx = std::regex("^(([0-9a-fA-F]{1,4}:){7,7}[0-9a-fA-F]{1,4}|([0-9a-fA-F]{1,4}:){1,7}:|([0-9a-fA-F]{1,4}:){1,6}(:[0-9a-fA-F]{1,4}){1,1}|([0-9a-fA-F]{1,4}:){1,5}(:[0-9a-fA-F]{1,4}){1,2}|([0-9a-fA-F]{1,4}:){1,4}(:[0-9a-fA-F]{1,4}){1,3}|([0-9a-fA-F]{1,4}:){1,3}(:[0-9a-fA-F]{1,4}){1,4}|([0-9a-fA-F]{1,4}:){1,2}(:[0-9a-fA-F]{1,4}){1,5}|([0-9a-fA-F]{1,4}:){1,1}(:[0-9a-fA-F]{1,4}){1,6}|:((:[0-9a-fA-F]{1,4}){1,7}|:))$", std::regex::extended);
}

bool CConfigure::ReadData(const std::string &path)
// returns true on failure
{
	bool rval = false;
	ESection section = ESection::none;
	counter = 0;

	//data.ysfalmodule = 0;
	//data.DPlusPort = data.DCSPort = data.DExtraPort = data.BMPort = data.DMRPlusPort = 0;
	std::ifstream cfgfile(path.c_str(), std::ifstream::in);
	if (! cfgfile.is_open())
	{
		std::cerr << "ERROR: '" << path << "' was not found!" << std::endl;
		return true;
	}

	std::string line;
	while (std::getline(cfgfile, line))
	{
		counter++;
		trim(line);
		if (3 > line.size())
			continue;	// can't be anything
		if (';' == line.at(0))
			continue;	// skip comments

		// check for next section
		if ('[' == line.at(0))
		{
			std::string hname(line.substr(1));
			auto pos = hname.find(']');
			if (std::string::npos != pos)
				hname.resize(pos);
			section = ESection::none;
			if (0 == hname.compare(g_Keys.repeater.section))
				section = ESection::repeater;
			else if (0 == hname.compare(g_Keys.modem.section))
				section = ESection::modem;
			else if (0 == hname.compare(g_Keys.gateway.section))
				section = ESection::gateway;
			else if (0 == hname.compare(g_Keys.dashboard.section))
				section = ESection::dashboard;
			else
			{
				std::cerr << "WARNING: unknown ini file section: " << line << std::endl;
				section = ESection::none;
			}
			continue;
		}

		std::vector<std::string> tokens;
		split(line, '=', tokens);
		if (2 > tokens.size())
		{
			std::cout << "WARNING: line #" << counter << ": '" << line << "' does not contain an equal sign, skipping" << std::endl;
			continue;
		}
		// check value for end-of-line comment
		auto pos = tokens[1].find(';');
		if (std::string::npos != pos)
		{
			tokens[1].assign(tokens[1].substr(0, pos));
			rtrim(tokens[1]); // whitespace between the value and the end-of-line comment
		}
		// trim whitespace from around the '='
		rtrim(tokens[0]);
		ltrim(tokens[1]);
		const std::string key(tokens[0]);
		const std::string value(tokens[1]);
		if (key.empty() || value.empty())
		{
			std::cout << "WARNING: line #" << counter << " '" << line << "' missing key or value, skipping" << std::endl;
			continue;
		}
		switch (section)
		{
			case ESection::repeater:
				if (0 == key.compare(g_Keys.repeater.callsign))
					data[g_Keys.repeater.section][g_Keys.repeater.callsign] = getString(value, g_Keys.repeater.callsign, rval);
				else if (0 == key.compare(g_Keys.repeater.module))
					data[g_Keys.repeater.section][g_Keys.repeater.module] = getString(value, g_Keys.repeater.module, rval);
				else if (0 == key.compare(g_Keys.repeater.can))
					data[g_Keys.repeater.section][g_Keys.repeater.can] = getUnsigned(value, "Channel Access Number", 0u, 15u, 0u);
				else if (0 == key.compare(g_Keys.repeater.radioTypeIsV3))
					data[g_Keys.repeater.section][g_Keys.repeater.radioTypeIsV3] = IS_TRUE(value[0]);
				else if (0 == key.compare(g_Keys.repeater.debug))
					data[g_Keys.repeater.section][g_Keys.repeater.debug] = IS_TRUE(value[0]);
				else
					badParam(g_Keys.repeater.section, key);
				break;
			case ESection::modem:
				if      (0 == key.compare(g_Keys.modem.gpiochipDevice))
					data[g_Keys.modem.section][g_Keys.modem.gpiochipDevice] = getString(value, g_Keys.modem.gpiochipDevice, rval);
				else if (0 == key.compare(g_Keys.modem.uartDevice))
					data[g_Keys.modem.section][g_Keys.modem.uartDevice] = getString(value, g_Keys.modem.uartDevice, rval);
				else if (0 == key.compare(g_Keys.modem.uartBaudRate))
					data[g_Keys.modem.section][g_Keys.modem.uartBaudRate] = getUnsigned(value, "Uart Speed", 38400u, 921600u, 460800u);
				else if (0 == key.compare(g_Keys.modem.boot0))
					data[g_Keys.modem.section][g_Keys.modem.boot0] = getUnsigned(value, "BOOT0 Pin", 0, 54, 20);
				else if (0 == key.compare(g_Keys.modem.nrst))
					data[g_Keys.modem.section][g_Keys.modem.nrst] = getUnsigned(value, "nRST Pin", 0, 54, 21);
				else if (0 == key.compare(g_Keys.modem.rxFreq))

					data[g_Keys.modem.section][g_Keys.modem.rxFreq] = getUnsigned(value, "Receive Frequency", 130000000u, 1000000000u, 446500000u);
				else if (0 == key.compare(g_Keys.modem.txFreq))
					data[g_Keys.modem.section][g_Keys.modem.txFreq] = getUnsigned(value, "Transmit Frequency", 130000000u, 1000000000u, 446500000u);

				else if (0 == key.compare(g_Keys.modem.afc))
					data[g_Keys.modem.section][g_Keys.modem.afc] = IS_TRUE(value[0]);
				else if (0 == key.compare(g_Keys.modem.freqCorr))
					data[g_Keys.modem.section][g_Keys.modem.freqCorr] = getInt(value, "Receive Offset (Hz)", -32768, 32767, 0);
				else if (0 == key.compare(g_Keys.modem.txPower))
					data[g_Keys.modem.section][g_Keys.modem.txPower] = getFloat(value, "Transmit Power (dBm)", -20.0f, 10.0f, 10.0f);
				else if (0 == key.compare(g_Keys.modem.debug))
					data[g_Keys.modem.section][g_Keys.modem.debug] = IS_TRUE(value[0]);
				else
					badParam(g_Keys.modem.section, key);
				break;
			case ESection::gateway:
				if (0 == key.compare(g_Keys.gateway.ipv4))
					data[g_Keys.gateway.section][g_Keys.gateway.ipv4] = IS_TRUE(value[0]);
				else if (0 == key.compare(g_Keys.gateway.ipv6))
					data[g_Keys.gateway.section][g_Keys.gateway.ipv6] = IS_TRUE(value[0]);
				else if (0 == key.compare(g_Keys.gateway.startupLink))
					data[g_Keys.gateway.section][g_Keys.gateway.startupLink] = getString(value, g_Keys.gateway.startupLink, rval);
				else if (0 == key.compare(g_Keys.gateway.maintainLink))
					data[g_Keys.gateway.section][g_Keys.gateway.maintainLink] = IS_TRUE(value[0]);
				else if (0 == key.compare(g_Keys.gateway.hostPath))
					data[g_Keys.gateway.section][g_Keys.gateway.hostPath] = getString(value, g_Keys.gateway.hostPath, rval);
				else if (0 == key.compare(g_Keys.gateway.myHostPath))
					data[g_Keys.gateway.section][g_Keys.gateway.myHostPath] = getString(value, g_Keys.gateway.myHostPath, rval);
				else if (0 == key.compare(g_Keys.gateway.dbPath))
					data[g_Keys.gateway.section][g_Keys.gateway.dbPath] = getString(value, g_Keys.gateway.dbPath, rval);
				else if (0 == key.compare(g_Keys.gateway.allowNotTranscoded))
					data[g_Keys.gateway.section][g_Keys.gateway.allowNotTranscoded] = IS_TRUE(value[0]);
				else if (0 == key.compare(g_Keys.gateway.audioFolder))
					data[g_Keys.gateway.section][g_Keys.gateway.audioFolder] = getString(value, g_Keys.gateway.audioFolder, rval);
				else
					badParam(g_Keys.gateway.section, key);
				break;
			case ESection::dashboard:
				if (0 == key.compare(g_Keys.dashboard.refresh))
					data[g_Keys.dashboard.section][g_Keys.dashboard.refresh] = getUnsigned(value, "Dashboard Refresh Rate", 2, 20, 10);
				else if (0 == key.compare(g_Keys.dashboard.lhcount))
					data[g_Keys.dashboard.section][g_Keys.dashboard.lhcount] = getUnsigned(value, "Lastheard Size", 1, 100, 20);
				else if (0 == key.compare(g_Keys.dashboard.showorder))
					data[g_Keys.dashboard.section][g_Keys.dashboard.showorder] = getString(value, g_Keys.dashboard.showorder, rval);
				else
					badParam(g_Keys.dashboard.section, key);
				break;
			case ESection::none:
			default:
				std::cout << "WARNING: parameter '" << line << "' defined before any [section]" << std::endl;
				break;
		}

	}
	cfgfile.close();

	////////////////////////////// check the input //////////////////////////////
	// Reflector section
	if (isDefined(ErrorLevel::fatal, g_Keys.repeater.section, g_Keys.repeater.callsign, rval))
	{
		auto cs = GetString(g_Keys.repeater.section, g_Keys.repeater.callsign);
		if (not std::regex_match(cs, MoreCS) or cs.size()>8)
		{
			std::cerr << "ERROR: Callsign '" << cs << "' does not look like a valid callsign" << std::endl;
			rval = true;
		}
		data[g_Keys.repeater.section][g_Keys.repeater.callsign] = cs;
	}
	if (isDefined(ErrorLevel::fatal, g_Keys.repeater.section, g_Keys.repeater.module, rval))
	{
		auto mod = GetString(g_Keys.repeater.section, g_Keys.repeater.module).at(0);
		if ('a' <= mod and mod <= 'z')
		{
			char newmod = 'A' + (mod - 'a');
			std::cout << "WARNING: '" << mod << "' is not uppercase, set to '" << newmod << "'" << std::endl;
			mod = newmod;
		}
		if (not('A' <= mod and mod <= 'Z'))
		{
			std::cerr << "ERROR: '" << mod << "' has to be an upper case letter A-Z" << std::endl;
			rval = true;
		}
		data[g_Keys.repeater.section][g_Keys.repeater.module] = std::string(1, mod);
	}
	isDefined(ErrorLevel::fatal, g_Keys.repeater.section, g_Keys.repeater.can,          rval);
	isDefined(ErrorLevel::fatal, g_Keys.repeater.section, g_Keys.repeater.radioTypeIsV3,rval);
	isDefined(ErrorLevel::fatal, g_Keys.repeater.section, g_Keys.repeater.debug,        rval);

	// Modem section
	if (not data[g_Keys.modem.section].contains(g_Keys.modem.boot0))
		data[g_Keys.modem.section][g_Keys.modem.boot0] = 20u;
	if (not data[g_Keys.modem.section].contains(g_Keys.modem.nrst))
		data[g_Keys.modem.section][g_Keys.modem.boot0] = 21u;
	if (isDefined(ErrorLevel::fatal, g_Keys.modem.section, g_Keys.modem.uartDevice, rval))
	{
		const auto path = GetString(g_Keys.modem.section, g_Keys.modem.uartDevice);
		checkPath(g_Keys.modem.section, g_Keys.modem.uartDevice, path, std::filesystem::file_type::character);
	}
	if (isDefined(ErrorLevel::fatal, g_Keys.modem.section, g_Keys.modem.uartBaudRate, rval))
	{
		const auto speed = GetUnsigned(g_Keys.modem.section, g_Keys.modem.uartBaudRate);
		switch (speed)
		{
			case 38400u:
			case 57600u:
			case 115200u:
			case 230400u:
			case 460800u:
			case 921600u:
				break;
			default:
				std::cerr << "Uart Speed of " << speed << " is not acceptable" << std::endl;
				rval = true;
		}
	}
	isDefined(ErrorLevel::fatal, g_Keys.modem.section, g_Keys.modem.rxFreq,   rval);
	if (not data[g_Keys.modem.section].contains(g_Keys.modem.txFreq))
		data[g_Keys.modem.section][g_Keys.modem.txFreq] = data[g_Keys.modem.section][g_Keys.modem.rxFreq];
	isDefined(ErrorLevel::fatal, g_Keys.modem.section, g_Keys.modem.txFreq,   rval);
	isDefined(ErrorLevel::fatal, g_Keys.modem.section, g_Keys.modem.afc,      rval);
	isDefined(ErrorLevel::fatal, g_Keys.modem.section, g_Keys.modem.freqCorr, rval);
	isDefined(ErrorLevel::fatal, g_Keys.modem.section, g_Keys.modem.txPower,  rval);
	isDefined(ErrorLevel::fatal, g_Keys.modem.section, g_Keys.modem.debug,    rval);

	// Gateway section
	isDefined(ErrorLevel::fatal, g_Keys.gateway.section, g_Keys.gateway.ipv4, rval);
	isDefined(ErrorLevel::fatal, g_Keys.gateway.section, g_Keys.gateway.ipv6, rval);
	isDefined(ErrorLevel::fatal, g_Keys.gateway.section, g_Keys.gateway.maintainLink, rval);
	if (data[g_Keys.gateway.section].contains(g_Keys.gateway.startupLink))
	{
		const auto ref = GetString(g_Keys.gateway.section, g_Keys.gateway.startupLink);
		if (not(std::regex_match(ref, MrefdCS) and not(std::regex_match(ref, UrfdCS))))
		{
			std::cout << "WARNING: [" << g_Keys.gateway.section << "]" << g_Keys.gateway.startupLink << "doesn't look like a reflector module" << std::endl;
		}
	}
	if (isDefined(ErrorLevel::mild, g_Keys.gateway.section, g_Keys.gateway.hostPath, rval))
	{
		const auto path = GetString(g_Keys.gateway.section, g_Keys.gateway.hostPath);
		checkPath(g_Keys.gateway.section, g_Keys.gateway.hostPath, path, std::filesystem::file_type::regular);
	}
	if (isDefined(ErrorLevel::mild, g_Keys.gateway.section, g_Keys.gateway.myHostPath, rval))
	{
		const auto path = GetString(g_Keys.gateway.section, g_Keys.gateway.myHostPath);
		checkPath(g_Keys.gateway.section, g_Keys.gateway.myHostPath, path, std::filesystem::file_type::regular);
	}
	if (isDefined(ErrorLevel::mild, g_Keys.gateway.section, g_Keys.gateway.dbPath, rval))
	{
		const auto path = GetString(g_Keys.gateway.section, g_Keys.gateway.dbPath);
		checkPath(g_Keys.gateway.section, g_Keys.gateway.myHostPath, path, std::filesystem::file_type::regular);
	}
	//isDefined(ErrorLevel::fatal, g_Keys.gateway.section, g_Keys.gateway.allowNotTranscoded, rval);
	if (isDefined(ErrorLevel::fatal, g_Keys.gateway.section, g_Keys.gateway.audioFolder, rval))
	{
		const auto path = GetString(g_Keys.gateway.section, g_Keys.gateway.audioFolder);
		checkPath(g_Keys.gateway.section, g_Keys.gateway.audioFolder, path, std::filesystem::file_type::directory);
	}

	// dashboard section
	isDefined(ErrorLevel::fatal, g_Keys.dashboard.section, g_Keys.dashboard.lhcount, rval);
	isDefined(ErrorLevel::fatal, g_Keys.dashboard.section, g_Keys.dashboard.refresh, rval);
	if(isDefined(ErrorLevel::fatal, g_Keys.dashboard.section, g_Keys.dashboard.showorder, rval))
	{
		std::vector<std::string> sections;
		split(GetString(g_Keys.dashboard.section, g_Keys.dashboard.showorder), ',', sections);
		if ( sections.empty()) {
			std::cerr << "ERROR: there must be at least one section specified in " << g_Keys.dashboard.showorder << std::endl;
			rval = true;
		} else {
			std::map<std::string, int> smap { { "IP", 0 }, { "LH", 0 }, { "LS", 0 }, { "MS", 0 }, { "PS", 0 }, { "SY", 0 } };
			for (const auto &sect : sections)
			{
				auto pair = smap.find(sect);
				if (smap.end() == pair) {
					std::cerr << "ERROR: " << g_Keys.dashboard.showorder << " does not have a section called '" << sect << "'" << std::endl;
					rval = true;
				}
				pair->second++;
				if (2 == pair->second) {
					std::cerr << "ERROR: " << sect << " is listed mutiple times in " << g_Keys.dashboard.showorder << std::endl;
					rval = true;
				}
			}
			smap.clear();
		}
	}

	return rval;
}

bool CConfigure::isDefined(ErrorLevel level, const std::string &section, const std::string &key, bool &rval)
{
	if (data.contains(section) and data[section].contains(key))
		return true;

	if (ErrorLevel::mild == level)
	{
		std::cout << "WARNING: [" << section << ']' << key << " is not defined" << std::endl;
		data[key] = nullptr;
	}
	else
	{
		std::cerr << "ERROR: [" << section << ']' << key << " is not defined" << std::endl;
		rval = true;
	}
	return false;
}

float CConfigure::getFloat(const std::string &valuestr, const std::string &label, float min, float max, float def) const
{
	try
	{
		auto f = std::stof(valuestr.c_str(), nullptr);
		if ( f < min || f > max )
		{
			std::cout << "WARNING: line #" << counter << ": " << label << " is out of range. Reset to " << def << std::endl;
			f = def;
		}
		return f;
	}
	catch(const std::exception &)
	{
		std::cerr << "WARNING: Line #" << counter << ": '" << valuestr << "' could not be converted to an floating point value, it will be set to " << def << std::endl;
		return def;
	}

}

unsigned CConfigure::getUnsigned(const std::string &valuestr, const std::string &label, unsigned min, unsigned max, unsigned def) const
{
	unsigned i;
	try {
		i = std::stoul(valuestr);
	} catch (const std::invalid_argument &e) {
		std::cerr << "Invalid unsigned value, '" << valuestr << "': " << e.what() << std::endl;
		i = def;
	} catch (const std::out_of_range &e) {
		std::cerr << "unsigned value out of range, '" << valuestr << "': " << e.what() << std::endl;
		i = def;
	}
	if ( i < min || i > max )
	{
		std::cout << "WARNING: line #" << counter << ": " << label << " is out of range. Reset to " << def << std::endl;
		i = def;
	}
	return i;
}

int CConfigure::getInt(const std::string &valuestr, const std::string &label, int min, int max, int def) const
{
	try
	{
		auto i = std::stoi(valuestr, nullptr, 0);
		if ( i < min || i > max )
		{
			std::cout << "WARNING: line #" << counter << ": " << label << " is out of range. Reset to " << def << std::endl;
			i = def;
		}
		return (unsigned)i;
	}
	catch(const std::exception &)
	{
		std::cerr << "WARNING: Line #" << counter << ": '" << valuestr << "' could not be converted to an unsigned value, it will be set to " << def << std::endl;
		return def;
	}
}

std::string CConfigure::getString(const std::string &value, const std::string &key, bool &rval) const
{
	if (('"' == value.at(0)) and ('"') == value.back())
		return value.substr(1, value.size()-2);

	std::cerr << "ERROR: line #" << counter << ": " << key << " is not contained in double quotes" << std::endl;
	rval = true;
	return value;
}

void CConfigure::badParam(const std::string &section, const std::string &key) const
{
	std::cout << "WARNING: line #" << counter << ": Unexpected parameter [" << section << "]" << key << std::endl;
}

void CConfigure::checkPath(const std::string &section, const std::string &key, const std::string &filepath, const std::filesystem::file_type desired_type) const
{
	std::filesystem::path p(filepath);
	std::filesystem::file_type rtype;
	// follow as many symbolic links as needed
	while (std::filesystem::file_type::symlink == (rtype = std::filesystem::status(p).type()))
		p = std::filesystem::read_symlink(p);

	if (p.is_relative())
		std::cout << "WARNING: [" << section << "]" << key << " '" << filepath << "' is a relative path" << std::endl;

	if (desired_type != rtype)
	{
		std::cout << "WARNING: [" << section << ']' << key << " '" << filepath << "' was expected to be ";
		switch(desired_type)
		{
		case std::filesystem::file_type::block:
			std::cout << "a block device";
			break;
		case std::filesystem::file_type::character:
			std::cout << "a character device";
			break;
		case std::filesystem::file_type::directory:
			std::cout << "a directory";
			break;
		case std::filesystem::file_type::fifo:
			std::cout << "a fifo";
			break;
		case std::filesystem::file_type::regular:
			std::cout << "a regular file";
			break;
		case std::filesystem::file_type::socket:
			std::cout << "a socket";
			break;
		case std::filesystem::file_type::symlink:
			std::cout << "a symbolic link";
			break;
		default:
			std::cout << "an unknown type";
			break;
		}
		std::cout << ", but it ";
		switch(rtype)
		{
		case std::filesystem::file_type::block:
			std::cout << "is a block device";
			break;
		case std::filesystem::file_type::directory:
			std::cout << "is a directory";
			break;
		case std::filesystem::file_type::character:
			std::cout << "is a modem (character) device";
			break;
		case std::filesystem::file_type::fifo:
			std::cout << "is a fifo";
			break;
		case std::filesystem::file_type::not_found:
			std::cout << "doesn't exist";
			break;
		case std::filesystem::file_type::regular:
			std::cout << "is a regular file";
			break;
		case std::filesystem::file_type::socket:
			std::cout << "a socket";
			break;
		default:
			std::cout << "ia an expected file type";
			break;
		}
		std::cout << std::endl;
	}
}

void CConfigure::Dump(bool justpublic) const
{
	nlohmann::json tmpjson = data;
	if (justpublic)
	{
		for (auto &it : data.items())
		{
			if (islower(it.key().at(0)))
				tmpjson.erase(it.key());
		}
	}
	std::cout << tmpjson.dump(4) << std::endl;
}

bool CConfigure::Contains(const std::string &section, const std::string &key) const
{
	return data.contains(section) and data[section].contains(key);
}

std::string CConfigure::GetString(const std::string &section, const std::string &key) const
{
	std::string str;
	if (data.contains(section) and data[section].contains(key))
	{
		if (data[section][key].is_null())
		{
			// null is the same thing as an empty string
			return str;
		}
		else if (data[section][key].is_string())
		{
			str.assign(data[section][key].get<std::string>());
			
		}
		else
			std::cerr << "ERROR: GetString(): [" << section << ']' << key << " is not a string" << std::endl;
	}
	else
	{
		std::cerr << "ERROR: GetString(): item [" << section << ']' << key << " is not defined" << std::endl;
	}
	return str;
}

float CConfigure::GetFloat(const std::string &section, const std::string &key) const
{
	if (data.contains(section) and data[section].contains(key))
	{
		if (data[section][key].is_number_float())
		{
			return data[section][key].get<float>();
		}
		else
			std::cerr << "ERROR: GetFloat(): [" << section << ']' << key << " is not a floating point value" << std::endl;
	}
	else
	{
		std::cerr << "ERROR: GetFloat(): item [" << section << ']' << key << " is not defined" << std::endl;
	}
	return 0.0;
}

unsigned CConfigure::GetUnsigned(const std::string &section, const std::string &key) const
{
	if (data.contains(section) and data[section].contains(key))
	{
		if (data[section][key].is_number_unsigned())
		{
			return data[section][key].get<unsigned>();
		}
		else
			std::cerr << "ERROR: GetUnsigned(): [" << section << ']' << key << " is not an unsigned value" << std::endl;
	}
	else
	{
		std::cerr << "ERROR: GetUnsigned(): item [" << section << ']' << key << " is not defined" << std::endl;
	}
	return 0u;
}

int CConfigure::GetInt(const std::string &section, const std::string &key) const
{
	if (data.contains(section) and data[section].contains(key))
	{
		if (data[section][key].is_number_integer())
		{
			return data[section][key].get<int>();
		}
		else
			std::cerr << "ERROR: GetInt(): item at [" << section << ']' << key << " is not an integet value" << std::endl;
	}
	else
	{
		std::cerr << "ERROR: GetInt(): item [" << section << ']' << key << " is not defined" << std::endl;
	}
	return 0;
}

bool CConfigure::GetBoolean(const std::string &section, const std::string &key) const
{
	if (data.contains(section) and data[section].contains(key))
	{
		if (data[section][key].is_boolean())
			return data[section][key];
		else
			std::cerr << "ERROR: GetBoolean(): item at [" << section << ']' << key << " is not a boolean" << std::endl;
	}
	else
	{
		std::cerr << "ERROR: GetBoolean(): item [" << section << ']' << key << " is not defined" << std::endl;
	}
	return false;
}

bool CConfigure::IsString(const std::string &section, const std::string &key) const
{
	if (data.contains(section) and data[section].contains(key))
	{
		return data[section][key].is_string();
	}
	return false;
}

#ifdef INICHECK

static void PrintUsage(const char *name)
{
	std::cout << "Usage: " << name << " [-l] PathToIniFile" << std::endl;
}

int main(int argc, char *argv[])
{
	bool rval = false;
	std::string arg;
	CConfigure cfg;
	switch (argc)
	{
		case 1:
			PrintUsage(argv[0]);
			break;
		case 2:
			rval = cfg.ReadData(argv[1]);
			break;
		case 3:
			if (strcmp(argv[1], "-l"))
			{
				rval = true;
				PrintUsage(argv[0]);
			}
			else
			{
				rval = cfg.ReadData(argv[2]);
				if (!rval)
					cfg.Dump(false);
			}
			break;
		default:
			rval = true;
			PrintUsage(argv[1]);
			break;
	}
	return rval ? EXIT_FAILURE : EXIT_SUCCESS;
}

#endif
