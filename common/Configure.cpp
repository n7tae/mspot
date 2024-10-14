/****************************************************************
 *                                                              *
 *             More - An M17-only Repeater/HotSpot              *
 *                                                              *
 *         Copyright (c) 2024 by Thomas A. Early N7TAE          *
 *                                                              *
 * See the LICENSE file for details about the software license. *
 *                                                              *
 ****************************************************************/

#include <sys/stat.h>
#include <stdlib.h>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <cstring>
#include <vector>
#include <list>
#include <algorithm>
#include <regex>

#include "Configure.h"
#include "JsonKeys.h"

extern SJsonKeys g_Keys;

static inline void split(const std::string &s, char delim, std::vector<std::string> &v)
{
	std::istringstream iss(s);
	std::string item;
	while (std::getline(iss, item, delim))
		v.push_back(item);
}

// trim from start (in place)
static inline void ltrim(std::string &s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](int ch) {
        return !std::isspace(ch);
    }));
}

// trim from end (in place)
static inline void rtrim(std::string &s) {
    s.erase(std::find_if(s.rbegin(), s.rend(), [](int ch) {
        return !std::isspace(ch);
    }).base(), s.end());
}

// trim from both ends (in place)
static inline void trim(std::string &s) {
    ltrim(s);
    rtrim(s);
}

CConfigure::CConfigure()
{
	//IPv4RegEx = std::regex("^((25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[1-9][0-9]|[0-9])\\.){3,3}(25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[1-9][0-9]|[0-9]){1,1}$", std::regex::extended);
	//IPv6RegEx = std::regex("^(([0-9a-fA-F]{1,4}:){7,7}[0-9a-fA-F]{1,4}|([0-9a-fA-F]{1,4}:){1,7}:|([0-9a-fA-F]{1,4}:){1,6}(:[0-9a-fA-F]{1,4}){1,1}|([0-9a-fA-F]{1,4}:){1,5}(:[0-9a-fA-F]{1,4}){1,2}|([0-9a-fA-F]{1,4}:){1,4}(:[0-9a-fA-F]{1,4}){1,3}|([0-9a-fA-F]{1,4}:){1,3}(:[0-9a-fA-F]{1,4}){1,4}|([0-9a-fA-F]{1,4}:){1,2}(:[0-9a-fA-F]{1,4}){1,5}|([0-9a-fA-F]{1,4}:){1,1}(:[0-9a-fA-F]{1,4}){1,6}|:((:[0-9a-fA-F]{1,4}){1,7}|:))$", std::regex::extended);
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
	if (! cfgfile.is_open()) {
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
		if ('#' == line.at(0))
			continue;	// skip comments

		// check for next section
		if ('[' == line.at(0))
		{
			std::string hname(line.substr(1));
			auto pos = hname.find(']');
			if (std::string::npos != pos)
				hname.resize(pos);
			section = ESection::none;
			if (0 == hname.compare(JMODEM))
				section = ESection::modem;
			else
			{
				std::cerr << "WARNING: unknown ini file section: " << line << std::endl;
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
		auto pos = tokens[1].find('#');
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
			std::cout << "WARNING: line #" << counter << ": missing key or value: '" << line << "'" << std::endl;
			continue;
		}
		switch (section)
		{
			case ESection::modem:
				if (0 == key.compare(JPORT))
					data[g_Keys.modem.port] = value;
				else if (0 == key.compare(JTXFREQ))
					data[g_Keys.modem.txfreq] = getUnsigned(value, "Transmitter Frequency", 130000000, 1000000000, 446500000);
				else if (0 == key.compare(JRXFREQ))
					data[g_Keys.modem.rxfreq] = getUnsigned(value, "Receiver Frequency", 130000000, 1000000000, 446500000);
				else if (0 == key.compare(JTXDELAY))
					data[g_Keys.modem.txdelay] = getUnsigned(value, "Transmitter Delay (ms)", 0, 2550, 100);
				else if (0 == key.compare(JTXLEVEL))
					data[g_Keys.modem.txlevel] = getUnsigned(value, "Transmitter Level 0-255", 0, 255, 128);
				else if (0 == key.compare(JRXLEVEL))
					data[g_Keys.modem.rxlevel] = getUnsigned(value, "Receiver Level 0-255", 0, 255, 129);
				else if (0 == key.compare(JRFLEVEL))
					data[g_Keys.modem.rflevel] = getUnsigned(value, "RF Level 0-255", 0, 255, 255);
				else if (0 == key.compare(JTXOFFSET))
					data[g_Keys.modem.txoffset] = getInt(value, "Transmitter Offset (Hz)", -1000000, 1000000, 0);
				else if (0 == key.compare(JRXOFFSET))
					data[g_Keys.modem.rxoffset] = getInt(value, "Receiver Offset (Hz)", -1000000, 1000000, 0);
				else if (0 == key.compare(JDUPLEX))
					data[g_Keys.modem.duplex] = IS_TRUE(value[0]);
				else if (0 == key.compare(JTXINVERT))
					data[g_Keys.modem.txinvert] = IS_TRUE(value[0]);
				else if (0 == key.compare(JRXINVERT))
					data[g_Keys.modem.rxinvert] = IS_TRUE(value[0]);
				else if (0 == key.compare(JPTTINVERT))
					data[g_Keys.modem.pttinvert] = IS_TRUE(value[0]);
				else
					badParam(key);
				break;
			default:
				std::cout << "WARNING: parameter '" << line << "' defined before any [section]" << std::endl;
		}

	}
	cfgfile.close();

	////////////////////////////// check the input
	// Modem section
	isDefined(ErrorLevel::fatal, JMODEM, JTXFREQ,    g_Keys.modem.txfreq,    rval);
	isDefined(ErrorLevel::fatal, JMODEM, JRXFREQ,    g_Keys.modem.rxfreq,    rval);
	isDefined(ErrorLevel::fatal, JMODEM, JTXOFFSET,  g_Keys.modem.txoffset,  rval);
	isDefined(ErrorLevel::fatal, JMODEM, JRXOFFSET,  g_Keys.modem.rxoffset,  rval);
	isDefined(ErrorLevel::fatal, JMODEM, JTXDELAY,   g_Keys.modem.txdelay,   rval);
	isDefined(ErrorLevel::fatal, JMODEM, JTXLEVEL,   g_Keys.modem.txlevel,   rval);
	isDefined(ErrorLevel::fatal, JMODEM, JRXLEVEL,   g_Keys.modem.rxlevel,   rval);
	isDefined(ErrorLevel::fatal, JMODEM, JRFLEVEL,   g_Keys.modem.rflevel,   rval);
	isDefined(ErrorLevel::fatal, JMODEM, JDUPLEX,    g_Keys.modem.duplex,    rval);
	isDefined(ErrorLevel::fatal, JMODEM, JTXINVERT,  g_Keys.modem.txinvert,  rval);
	isDefined(ErrorLevel::fatal, JMODEM, JRXINVERT,  g_Keys.modem.rxinvert,  rval);
	isDefined(ErrorLevel::fatal, JMODEM, JPTTINVERT, g_Keys.modem.pttinvert, rval);
	return rval;
}

bool CConfigure::isDefined(ErrorLevel level, const std::string &section, const std::string &pname, const std::string &key, bool &rval)
{
	if (data.contains(key))
		return true;

	if (ErrorLevel::mild == level)
	{
		std::cout << "WARNING: [" << section << ']' << pname << " is not defined" << std::endl;
		data[key] = nullptr;
	}
	else
	{
		std::cerr << "ERROR: [" << section << ']' << pname << " is not defined" << std::endl;
		rval = true;
	}
	return false;
}

unsigned CConfigure::getUnsigned(const std::string &valuestr, const std::string &label, unsigned min, unsigned max, unsigned def) const
{
	auto i = unsigned(std::stoul(valuestr.c_str()));
	if ( i < min || i > max )
	{
		std::cout << "WARNING: line #" << counter << ": " << label << " is out of range. Reset to " << def << std::endl;
		i = def;
	}
	return (unsigned)i;
}

int CConfigure::getInt(const std::string &valuestr, const std::string &label, int min, int max, int def) const
{
	auto i = std::stoi(valuestr.c_str());
	if ( i < min || i > max )
	{
		std::cout << "WARNING: line #" << counter << ": " << label << " is out of range. Reset to " << def << std::endl;
		i = def;
	}
	return (unsigned)i;
}

void CConfigure::badParam(const std::string &key) const
{
	std::cout << "WARNING: line #" << counter << ": Unexpected parameter: '" << key << "'" << std::endl;
}

void CConfigure::checkFile(const std::string &section, const std::string &key, const std::string &filepath) const
{
	struct stat sstat;
	auto rval = stat(filepath.c_str(), &sstat);
	if (rval)
	{
		std::cout << "WARNING: [" << section << ']' << key << " \"" << filepath << "\": " << strerror(errno) << std::endl;
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

bool CConfigure::Contains(const std::string &key) const
{
	return data.contains(key);
}

std::string CConfigure::GetString(const std::string &key) const
{
	std::string str;
	if (data.contains(key))
	{
		if (data[key].is_null())
		{
			// null is the same thing as an empty string
			return str;
		}
		else if (data[key].is_string())
		{
			str.assign(data[key].get<std::string>());
		}
		else
			std::cerr << "ERROR: GetString(): '" << key << "' is not a string" << std::endl;
	}
	else
	{
		std::cerr << "ERROR: GetString(): item at '" << key << "' is not defined" << std::endl;
	}
	return str;
}

unsigned CConfigure::GetUnsigned(const std::string &key) const
{
	unsigned u = 0;
	if (data.contains(key))
	{
		if (data[key].is_number_unsigned())
		{
			u = data[key].get<unsigned>();
		}
		else
			std::cerr << "ERROR: GetUnsigned(): '" << key << "' is not an unsigned value" << std::endl;
	}
	else
	{
		std::cerr << "ERROR: GetUnsigned(): item at '" << key << "' is not defined" << std::endl;
	}
	return u;
}

int CConfigure::GetInt(const std::string &key) const
{
	int i = 0;
	if (data.contains(key))
	{
		if (data[key].is_number_integer())
		{
			i = data[key].get<int>();
		}
		else
			std::cerr << "ERROR: GetInt(): item at '" << key << "' is not an integet value" << std::endl;
	}
	else
	{
		std::cerr << "ERROR: GetInt(): item at '" << key << "' is not defined" << std::endl;
	}
	return i;
}

bool CConfigure::GetBoolean(const std::string &key) const
{
	if (data[key].is_boolean())
		return data[key];
	else
		return false;
}

bool CConfigure::IsString(const std::string &key) const
{
	if (data.contains(key))
		return data[key].is_string();
	return false;
}

#ifdef INICHECK
SJsonKeys g_Keys;
int main(int argc, char *argv[])
{
	if (3 == argc && strlen(argv[1]) > 1 && '-' == argv[1][0])
	{
		CConfigure d;
		auto rval = d.ReadData(argv[2]);
		if ('q' != argv[1][1])
			d.Dump(('n' == argv[1][1]) ? true : false);
		return rval ? EXIT_FAILURE : EXIT_SUCCESS;
	}
	std::cerr << "Usage: " << argv[0] << " -(q|n|v) FILENAME\nWhere:\n\t-q just prints warnings and errors.\n\t-n also prints keys that begin with an uppercase letter.\n\t-v prints all keys, warnings and errors." << std::endl;
	return EXIT_SUCCESS;
}
#endif
