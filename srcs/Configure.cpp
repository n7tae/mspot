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
#include <sys/types.h>
#include <pwd.h>
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
#ifndef INICHECK
#include "Log.h"
#endif

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
	MoreCS  = std::regex("^[0-9]?[A-Z]{1,2}[0-9]{1,2}[A-Z]{1,4}[ ]*$", std::regex::extended);
	MrefdCS = std::regex("^M17-[A-Z0-9]{3,3}( [A-Z])?$", std::regex::extended);
	UrfdCS  = std::regex("^URF[A-Z0-9]{3,3}(  [A-Z])?$", std::regex::extended);
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
			if (0 == hname.compare(g_Keys.general.section))
				section = ESection::general;
			else if (0 == hname.compare(g_Keys.log.section))
				section = ESection::log;
			else if (0 == hname.compare(g_Keys.modem.section))
				section = ESection::modem;
			else if (0 == hname.compare(g_Keys.gateway.section))
				section = ESection::gateway;
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
			std::cout << "WARNING: line #" << counter << " '" << line << "' missing key or value, skipping" << std::endl;
			continue;
		}
		switch (section)
		{
			case ESection::general:
				if (0 == key.compare(g_Keys.general.callsign))
					data[g_Keys.general.callsign] = value;
				else if (0 == key.compare(g_Keys.general.can))
					data[g_Keys.general.can] = getUnsigned(value, "Channel Access Number", 0u, 15u, 0u);
				else if (0 == key.compare(g_Keys.general.isdaemon))
					data[g_Keys.general.isdaemon] = IS_TRUE(value[0]);
				else if (0 == key.compare(g_Keys.general.isduplex))
					data[g_Keys.general.isduplex] = IS_TRUE(value[0]);
				else if (0 == key.compare(g_Keys.general.isprivate))
					data[g_Keys.general.isprivate] = IS_TRUE(value[0]);
				else if (0 == key.compare(g_Keys.general.module))
					data[g_Keys.general.module] = value;
				else if (0 == key.compare(g_Keys.general.rxfreq))
					data[g_Keys.general.rxfreq] = getUnsigned(value, "Receive Frequency", 130000000u, 1000000000u, 446500000u);
				else if (0 == key.compare(g_Keys.general.txfreq))
					data[g_Keys.general.txfreq] = getUnsigned(value, "Transmit Frequency", 130000000u, 1000000000u, 446500000u);
				else if (0 == key.compare(g_Keys.general.user))
					data[g_Keys.general.user] = value;
				else
					badParam(key);
				break;
			case ESection::modem:
				if (0 == key.compare(g_Keys.modem.protocol))
					data[g_Keys.modem.protocol] = value;

				else if (0 == key.compare(g_Keys.modem.uartPort))
					data[g_Keys.modem.uartPort] = value;
				else if (0 == key.compare(g_Keys.modem.uartSpeed))
					data [g_Keys.modem.uartSpeed] = getUnsigned(value, "Uart Speed", 38400u, 921600u, 460800u);

				else if (0 == key.compare(g_Keys.modem.i2cAddress))
					data[g_Keys.modem.i2cAddress] = getUnsigned(value, "I2CAddress", 0x0u, 0xffu, 0x22u);
				else if (0 == key.compare(g_Keys.modem.i2cPort))
					data[g_Keys.modem.i2cPort] = value;

				else if (0 == key.compare(g_Keys.modem.localAddress))
					data[g_Keys.modem.localAddress] = value;
				else if (0 == key.compare(g_Keys.modem.localPort))
					data[g_Keys.modem.localPort] = getUnsigned(value, "UDP Local Port", 1025u, 49000u, 3335u);
				else if (0 == key.compare(g_Keys.modem.modemAddress))
					data[g_Keys.modem.modemAddress] = value;
				else if (0 == key.compare(g_Keys.modem.modemPort))
					data[g_Keys.modem.modemPort] = getUnsigned(value, "UDP Modem Port", 1025u, 49000u, 3334u);

				else if (0 == key.compare(g_Keys.modem.txHang))
					data[g_Keys.modem.txHang] = getUnsigned(value, "Transmit Hang ms 0-255", 0u, 255u, 5u);
				else if (0 == key.compare(g_Keys.modem.txDelay))
					data[g_Keys.modem.txDelay] = getUnsigned(value, "Transmit Delay (ms)", 10u, 2550u, 100u);
				else if (0 == key.compare(g_Keys.modem.pttInvert))
					data[g_Keys.modem.pttInvert] = IS_TRUE(value[0]);
				else if (0 == key.compare(g_Keys.modem.rxInvert))
					data[g_Keys.modem.rxInvert] = IS_TRUE(value[0]);
				else if (0 == key.compare(g_Keys.modem.txInvert))
					data[g_Keys.modem.txInvert] = IS_TRUE(value[0]);

				else if (0 == key.compare(g_Keys.modem.rxOffset))
					data[g_Keys.modem.rxOffset] = getInt(value, "Receive Offset (Hz)", -1000000, 1000000, 0);
				else if (0 == key.compare(g_Keys.modem.txOffset))
					data[g_Keys.modem.txOffset] = getInt(value, "Transmit Offset (Hz)", -1000000, 1000000, 0);

				else if (0 == key.compare(g_Keys.modem.rfLevel))
					data[g_Keys.modem.rfLevel] = getUnsigned(value, "RF Level 0-255", 0u, 255u, 128u);
				else if (0 == key.compare(g_Keys.modem.rxLevel))
					data[g_Keys.modem.rxLevel] = getUnsigned(value, "Receive Level 0-255", 0u, 255u, 128u);
				else if (0 == key.compare(g_Keys.modem.txLevel))
					data[g_Keys.modem.txLevel] = getUnsigned(value, "Transmit Level 0-255", 0u, 255u, 128u);

				else if (0 == key.compare(g_Keys.modem.rxDCOffset))
					data[g_Keys.modem.rxDCOffset] = getInt(value, "Receive DC Offset -128-127", -128, 127, 0);
				else if (0 == key.compare(g_Keys.modem.txDCOffset))
					data[g_Keys.modem.txDCOffset] = getInt(value, "Transmit DC Offset -128-127", -128, 127, 0);

				else if (0 == key.compare(g_Keys.modem.rssiMapFile))
					data[g_Keys.modem.rssiMapFile] = value;
				else if (0 == key.compare(g_Keys.modem.trace))
					data[g_Keys.modem.trace] = IS_TRUE(value[0]);
				else if (0 == key.compare(g_Keys.modem.debug))
					data[g_Keys.modem.debug] = IS_TRUE(value[0]);
				else
					badParam(key);
				break;
			case ESection::gateway:
				if (0 == key.compare(g_Keys.gateway.ipv4))
					data[g_Keys.gateway.ipv4] = IS_TRUE(value[0]);
				if (0 == key.compare(g_Keys.gateway.ipv6))
					data[g_Keys.gateway.ipv6] = IS_TRUE(value[0]);
				if (0 == key.compare(g_Keys.gateway.startupLink))
					data[g_Keys.gateway.startupLink] = value;
			case ESection::none:
			default:
				std::cout << "WARNING: parameter '" << line << "' defined before any [section]" << std::endl;
				break;
		}

	}
	cfgfile.close();

	////////////////////////////// check the input
	// General section
	if (isDefined(ErrorLevel::fatal, g_Keys.general.section, g_Keys.general.callsign, rval))
	{
		auto cs = GetString(g_Keys.general.callsign);
		trim(cs);
		if (not std::regex_match(cs, MoreCS))
		{
			std::cerr << "Error: Callsign '" << cs << "' does not look like a valid callsign" << std::endl;
			rval = true;
		}
		data[g_Keys.general.callsign] = cs;
	}
	if (isDefined(ErrorLevel::fatal, g_Keys.general.section, g_Keys.general.module, rval))
	{
		auto mod = GetString(g_Keys.general.module).at(0);
		if ('a' <= mod and mod <= 'z')
		{
			std::cout << "WARNING: '" << mod << "' is not uppercase, converting" << std::endl;
			mod = 'A' + (mod - 'a');
			data[g_Keys.general.module] = std::string(1, mod);
		}
		if (not('A' <= mod and mod <= 'Z'))
		{
			std::cerr << "ERROR: '" << mod << "' has to be an upper case letter A-Z" << std::endl;
			rval = true;
		}
	}
	isDefined(ErrorLevel::fatal, g_Keys.general.section, g_Keys.general.isduplex,  rval);
	if (isDefined(ErrorLevel::fatal, g_Keys.general.section, g_Keys.general.isdaemon, rval))
	{
		if (isDefined(ErrorLevel::fatal, g_Keys.general.section, g_Keys.general.user, rval))
		{
			const auto user = GetString(g_Keys.general.user);
			struct passwd *pwitem = getpwnam(user.c_str());
			if (nullptr == pwitem)
			{
				std::cerr << "ERROR: user '" << user << "' was not found in the user db" << std::endl;
				rval = true;
			}
		}
	}
	isDefined(ErrorLevel::fatal, g_Keys.general.section, g_Keys.general.rxfreq,    rval);
	isDefined(ErrorLevel::fatal, g_Keys.general.section, g_Keys.general.txfreq,    rval);
	isDefined(ErrorLevel::fatal, g_Keys.general.section, g_Keys.general.can,       rval);
	isDefined(ErrorLevel::fatal, g_Keys.general.section, g_Keys.general.isprivate, rval);

	// Modem section
	if (isDefined(ErrorLevel::fatal, g_Keys.modem.section, g_Keys.modem.protocol, rval))
	{
		const auto protocol = GetString(g_Keys.modem.protocol);
		if (protocol.compare("uart"))
		{
			if (isDefined(ErrorLevel::fatal, g_Keys.modem.section, g_Keys.modem.uartPort, rval))
			{
				const auto path = GetString(g_Keys.modem.uartPort);
				checkFile(g_Keys.modem.section, g_Keys.modem.uartPort, path);
			}
			if (isDefined(ErrorLevel::fatal, g_Keys.modem.section, g_Keys.modem.uartSpeed, rval))
			{
				const auto speed = GetUnsigned(g_Keys.modem.uartSpeed);
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
		}
		else if (0 == protocol.compare("udp"))
		{
			if (isDefined(ErrorLevel::fatal, g_Keys.modem.section, g_Keys.modem.localAddress, rval))
			{
				const auto addr = GetString(g_Keys.modem.localAddress);
				if (not(std::regex_match(addr, IPv4RegEx)) and not(std::regex_match(addr, IPv6RegEx)))
				{
					std::cerr << "ERROR: [" << g_Keys.modem.section << "]" << g_Keys.modem.localAddress << " does not look like an internet address" << std::endl;
					rval = true;
				}
			}
			isDefined(ErrorLevel::fatal, g_Keys.modem.section, g_Keys.modem.localPort, rval);

			if (isDefined(ErrorLevel::fatal, g_Keys.modem.section, g_Keys.modem.modemAddress, rval))
			{
				const auto addr = GetString(g_Keys.modem.modemAddress);
				if (not(std::regex_match(addr, IPv4RegEx)) and not(std::regex_match(addr, IPv6RegEx)))
				{
					std::cerr << "ERROR: [" << g_Keys.modem.section << "]" << g_Keys.modem.modemAddress << " does not look like an internet address" << std::endl;
					rval = true;
				}
			}
			isDefined(ErrorLevel::fatal, g_Keys.modem.section, g_Keys.modem.localPort, rval);
		}
		else if (0 == protocol.compare("i2c"))
		{
			if (isDefined(ErrorLevel::fatal, g_Keys.modem.section, g_Keys.modem.i2cAddress, rval))
			{
				const auto path = GetString(g_Keys.modem.i2cAddress);
				checkFile(g_Keys.modem.section, g_Keys.modem.i2cAddress, path);
			}
			isDefined(ErrorLevel::fatal, g_Keys.modem.section, g_Keys.modem.i2cPort, rval);
		}
		else if (protocol.compare("null"))
		{
			std::cerr << "ERROR: Unknown Protocol '" << protocol << "'" << std::endl;
			rval = true;
		}
	}
	isDefined(ErrorLevel::fatal, g_Keys.modem.section, g_Keys.modem.txHang,     rval);
	isDefined(ErrorLevel::fatal, g_Keys.modem.section, g_Keys.modem.txDelay,    rval);
	isDefined(ErrorLevel::fatal, g_Keys.modem.section, g_Keys.modem.pttInvert,  rval);
	isDefined(ErrorLevel::fatal, g_Keys.modem.section, g_Keys.modem.rxInvert,   rval);
	isDefined(ErrorLevel::fatal, g_Keys.modem.section, g_Keys.modem.txInvert,   rval);

	isDefined(ErrorLevel::fatal, g_Keys.modem.section, g_Keys.modem.rxOffset,   rval);
	isDefined(ErrorLevel::fatal, g_Keys.modem.section, g_Keys.modem.txOffset,   rval);

	isDefined(ErrorLevel::fatal, g_Keys.modem.section, g_Keys.modem.rfLevel,    rval);
	isDefined(ErrorLevel::fatal, g_Keys.modem.section, g_Keys.modem.rxLevel,    rval);
	isDefined(ErrorLevel::fatal, g_Keys.modem.section, g_Keys.modem.txLevel,    rval);

	isDefined(ErrorLevel::fatal, g_Keys.modem.section, g_Keys.modem.rxDCOffset, rval);
	isDefined(ErrorLevel::fatal, g_Keys.modem.section, g_Keys.modem.txDCOffset, rval);

	if (isDefined(ErrorLevel::mild, g_Keys.modem.section, g_Keys.modem.rssiMapFile, rval))
	{
		const auto path = GetString(g_Keys.modem.rssiMapFile);
		checkFile(g_Keys.modem.section, g_Keys.modem.rssiMapFile, path);
	}
	isDefined(ErrorLevel::fatal, g_Keys.modem.section, g_Keys.modem.trace, rval);
	isDefined(ErrorLevel::fatal, g_Keys.modem.section, g_Keys.modem.debug, rval);

	// Gateway section
	isDefined(ErrorLevel::fatal, g_Keys.gateway.section, g_Keys.gateway.ipv4, rval);
	isDefined(ErrorLevel::fatal, g_Keys.gateway.section, g_Keys.gateway.ipv6, rval);
	if (data.contains(g_Keys.gateway.startupLink))
	{
		const auto ref = GetString(g_Keys.gateway.startupLink);
		if (not(std::regex_match(ref, MrefdCS) and not(std::regex_match(ref, UrfdCS))))
		{
			std::cout << "WARNING: [" << g_Keys.gateway.section << "]" << g_Keys.gateway.startupLink << "doesn't look like a reflector designator with module" << std::endl;
		}
	}

	return rval;
}

bool CConfigure::isDefined(ErrorLevel level, const std::string &section, const std::string &key, bool &rval)
{
	if (data.contains(key))
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

unsigned CConfigure::getUnsigned(const std::string &valuestr, const std::string &label, unsigned min, unsigned max, unsigned def) const
{
	auto i = unsigned(std::stoul(valuestr, nullptr, 0));
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
