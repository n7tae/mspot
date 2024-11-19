/****************************************************************
 *                                                              *
 *             More - An M17-only Repeater/HotSpot              *
 *                                                              *
 *         Copyright (c) 2024 by Thomas A. Early N7TAE          *
 *                                                              *
 * See the LICENSE file for details about the software license. *
 *                                                              *
 ****************************************************************/

#include <filesystem>
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
			if (0 == hname.compare(g_Keys.reflector.section))
				section = ESection::reflector;
			else if (0 == hname.compare(g_Keys.log.section))
				section = ESection::log;
			else if (0 == hname.compare(g_Keys.cwid.section))
				section = ESection::cwid;
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
			case ESection::reflector:
				if (0 == key.compare(g_Keys.reflector.callsign))
					data[g_Keys.reflector.section][g_Keys.reflector.callsign] = value;
				else if (0 == key.compare(g_Keys.reflector.can))
					data[g_Keys.reflector.section][g_Keys.reflector.can] = getUnsigned(value, "Channel Access Number", 0u, 15u, 0u);
				else if (0 == key.compare(g_Keys.reflector.timeOut))
					data[g_Keys.reflector.section][g_Keys.reflector.timeOut] = getUnsigned(value, "RF and Net timeouts", 50u, 500u, 180u);
				else if (0 == key.compare(g_Keys.reflector.isDaemon))
					data[g_Keys.reflector.section][g_Keys.reflector.isDaemon] = IS_TRUE(value[0]);
				else if (0 == key.compare(g_Keys.reflector.isDuplex))
					data[g_Keys.reflector.section][g_Keys.reflector.isDuplex] = IS_TRUE(value[0]);
				else if (0 == key.compare(g_Keys.reflector.allowEncrypt))
					data[g_Keys.reflector.section][g_Keys.reflector.allowEncrypt] = IS_TRUE(value[0]);
				else if (0 == key.compare(g_Keys.reflector.isprivate))
					data[g_Keys.reflector.section][g_Keys.reflector.isprivate] = IS_TRUE(value[0]);
				else if (0 == key.compare(g_Keys.reflector.module))
					data[g_Keys.reflector.section][g_Keys.reflector.module] = value;
				else if (0 == key.compare(g_Keys.reflector.user))
					data[g_Keys.reflector.section][g_Keys.reflector.user] = value;
				else if (0 == key.compare(g_Keys.reflector.netModeHang))
					data[g_Keys.reflector.section][g_Keys.reflector.netModeHang] = getUnsigned(value, "Net Mode Hang time", 1u, 60u, 3u);
				else if (0 == key.compare(g_Keys.reflector.rfModeHang))
					data[g_Keys.reflector.section][g_Keys.reflector.rfModeHang] = getUnsigned(value, "RF Mode Hang time", 1u, 60u, 10u);
				else if (0 == key.compare(g_Keys.reflector.debug))
					data[g_Keys.reflector.section][g_Keys.reflector.debug] = IS_TRUE(value[0]);
				else
					badParam(g_Keys.reflector.section, key);
				break;
			case ESection::modem:
				if (0 == key.compare(g_Keys.modem.protocol))
					data[g_Keys.modem.section][g_Keys.modem.protocol] = value;

				else if (0 == key.compare(g_Keys.modem.uartPort))
					data[g_Keys.modem.section][g_Keys.modem.uartPort] = value;
				else if (0 == key.compare(g_Keys.modem.uartSpeed))
					data[g_Keys.modem.section][g_Keys.modem.uartSpeed] = getUnsigned(value, "Uart Speed", 38400u, 921600u, 460800u);

				else if (0 == key.compare(g_Keys.modem.i2cAddress))
					data[g_Keys.modem.section][g_Keys.modem.i2cAddress] = getUnsigned(value, "I2CAddress", 0x0u, 0xffu, 0x22u);
				else if (0 == key.compare(g_Keys.modem.i2cPort))
					data[g_Keys.modem.section][g_Keys.modem.i2cPort] = value;

				else if (0 == key.compare(g_Keys.modem.localAddress))
					data[g_Keys.modem.section][g_Keys.modem.localAddress] = value;
				else if (0 == key.compare(g_Keys.modem.localPort))
					data[g_Keys.modem.section][g_Keys.modem.localPort] = getUnsigned(value, "UDP Local Port", 1025u, 49000u, 3335u);
				else if (0 == key.compare(g_Keys.modem.modemAddress))
					data[g_Keys.modem.section][g_Keys.modem.modemAddress] = value;
				else if (0 == key.compare(g_Keys.modem.modemPort))
					data[g_Keys.modem.section][g_Keys.modem.modemPort] = getUnsigned(value, "UDP Modem Port", 1025u, 49000u, 3334u);

				else if (0 == key.compare(g_Keys.modem.txHang))
					data[g_Keys.modem.section][g_Keys.modem.txHang] = getUnsigned(value, "Transmit Hang ms 0-255", 0u, 255u, 5u);
				else if (0 == key.compare(g_Keys.modem.txDelay))
					data[g_Keys.modem.section][g_Keys.modem.txDelay] = getUnsigned(value, "Transmit Delay (ms)", 10u, 2550u, 100u);
				else if (0 == key.compare(g_Keys.modem.pttInvert))
					data[g_Keys.modem.section][g_Keys.modem.pttInvert] = IS_TRUE(value[0]);
				else if (0 == key.compare(g_Keys.modem.rxInvert))
					data[g_Keys.modem.section][g_Keys.modem.rxInvert] = IS_TRUE(value[0]);
				else if (0 == key.compare(g_Keys.modem.txInvert))
					data[g_Keys.modem.section][g_Keys.modem.txInvert] = IS_TRUE(value[0]);

				else if (0 == key.compare(g_Keys.modem.rxFreq))
					data[g_Keys.modem.section][g_Keys.modem.rxFreq] = getUnsigned(value, "Receive Frequency", 130000000u, 1000000000u, 446500000u);
				else if (0 == key.compare(g_Keys.modem.txFreq))
					data[g_Keys.modem.section][g_Keys.modem.txFreq] = getUnsigned(value, "Transmit Frequency", 130000000u, 1000000000u, 446500000u);

				else if (0 == key.compare(g_Keys.modem.rxOffset))
					data[g_Keys.modem.section][g_Keys.modem.rxOffset] = getInt(value, "Receive Offset (Hz)", -1000000, 1000000, 0);
				else if (0 == key.compare(g_Keys.modem.txOffset))
					data[g_Keys.modem.section][g_Keys.modem.txOffset] = getInt(value, "Transmit Offset (Hz)", -1000000, 1000000, 0);

				else if (0 == key.compare(g_Keys.modem.rfLevel))
					data[g_Keys.modem.section][g_Keys.modem.rfLevel] = getFloat(value, "RF Level 0 - 100%", 0.0f, 100.0f, 50.0f);
				else if (0 == key.compare(g_Keys.modem.rxLevel))
					data[g_Keys.modem.section][g_Keys.modem.rxLevel] = getFloat(value, "Receive Level  0 - 100%", 0.0f, 100.0f, 50.0f);
				else if (0 == key.compare(g_Keys.modem.txLevel))
					data[g_Keys.modem.section][g_Keys.modem.txLevel] = getFloat(value, "Transmit Level  0 - 100%", 0.0f, 100.0f, 50.0f);
				else if (0 == key.compare(g_Keys.modem.cwLevel))
					data[g_Keys.modem.section][g_Keys.modem.cwLevel] = getFloat(value, "CW Transmit Level  0 - 100%", 0.0f, 100.0f, 50.0f);

				else if (0 == key.compare(g_Keys.modem.rxDCOffset))
					data[g_Keys.modem.section][g_Keys.modem.rxDCOffset] = getInt(value, "Receive DC Offset -128-127", -128, 127, 0);
				else if (0 == key.compare(g_Keys.modem.txDCOffset))
					data[g_Keys.modem.section][g_Keys.modem.txDCOffset] = getInt(value, "Transmit DC Offset -128-127", -128, 127, 0);

				else if (0 == key.compare(g_Keys.modem.rssiMapFile))
					data[g_Keys.modem.section][g_Keys.modem.rssiMapFile] = value;
				else if (0 == key.compare(g_Keys.modem.trace))
					data[g_Keys.modem.section][g_Keys.modem.trace] = IS_TRUE(value[0]);
				else if (0 == key.compare(g_Keys.modem.debug))
					data[g_Keys.modem.section][g_Keys.modem.debug] = IS_TRUE(value[0]);
				else
					badParam(g_Keys.modem.section, key);
				break;
			case ESection::cwid:
				if (0 == key.compare(g_Keys.cwid.enable))
					data[g_Keys.cwid.section][g_Keys.cwid.enable] = GetBoolean(g_Keys.cwid.enable, g_Keys.cwid.enable);
				else if (0 == key.compare(g_Keys.cwid.time))
					data[g_Keys.cwid.section][g_Keys.cwid.time] = getUnsigned(g_Keys.cwid.time, "CW Id Time", 0, 100, 10);
				else if (0 == key.compare(g_Keys.cwid.callsign))
					data[g_Keys.cwid.section][g_Keys.cwid.callsign] = value;
				else
					badParam(g_Keys.cwid.section, key);
				break;
			case ESection::log:
				if (0 == key.compare(g_Keys.log.displayLevel))
					data[g_Keys.log.section][g_Keys.log.displayLevel] = getUnsigned(value, "Display Level 0-6", 0u, 6u, 2u);
				else if (0 == key.compare(g_Keys.log.fileLevel))
					data[g_Keys.log.section][g_Keys.log.fileLevel] = getUnsigned(value, "File Level 0-6", 0u, 6u, 2u);
				else if (0 == key.compare(g_Keys.log.fileName))
					data[g_Keys.log.section][g_Keys.log.fileName] = value;
				else if (0 == key.compare(g_Keys.log.filePath))
					data[g_Keys.log.section][g_Keys.log.filePath] = value;
				else if (0 == key.compare(g_Keys.log.rotate))
					data[g_Keys.log.section][g_Keys.log.rotate] = IS_TRUE(value[0]);
				else
					badParam(g_Keys.log.section, key);
				break;
			case ESection::gateway:
				if (0 == key.compare(g_Keys.gateway.ipv4))
					data[g_Keys.gateway.section][g_Keys.gateway.ipv4] = IS_TRUE(value[0]);
				else if (0 == key.compare(g_Keys.gateway.ipv6))
					data[g_Keys.gateway.section][g_Keys.gateway.ipv6] = IS_TRUE(value[0]);
				else if (0 == key.compare(g_Keys.gateway.startupLink))
					data[g_Keys.gateway.section][g_Keys.gateway.startupLink] = value;
				else if (0 == key.compare(g_Keys.gateway.maintainLink))
					data[g_Keys.gateway.section][g_Keys.gateway.maintainLink] = IS_TRUE(value[0]);
				else if (0 == key.compare(g_Keys.gateway.hostPath))
					data[g_Keys.gateway.section][g_Keys.gateway.hostPath] = value;
				else if (0 == key.compare(g_Keys.gateway.myHostPath))
					data[g_Keys.gateway.section][g_Keys.gateway.myHostPath] = value;
				else if (0 == key.compare(g_Keys.gateway.allowNotTranscoded))
					data[g_Keys.gateway.section][g_Keys.gateway.allowNotTranscoded] = IS_TRUE(value[0]);
				else if (0 == key.compare(g_Keys.gateway.audioFolder))
					data[g_Keys.gateway.section][g_Keys.gateway.audioFolder] = value;
				else
					badParam(g_Keys.gateway.section, key);
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
	if (isDefined(ErrorLevel::fatal, g_Keys.reflector.section, g_Keys.reflector.callsign, rval))
	{
		auto cs = GetString(g_Keys.reflector.section, g_Keys.reflector.callsign);
		if (not std::regex_match(cs, MoreCS) or cs.size()>8)
		{
			std::cerr << "ERROR: Callsign '" << cs << "' does not look like a valid callsign" << std::endl;
			rval = true;
		}
		data[g_Keys.reflector.callsign] = cs;
	}
	if (isDefined(ErrorLevel::fatal, g_Keys.reflector.section, g_Keys.reflector.module, rval))
	{
		auto mod = GetString(g_Keys.reflector.section, g_Keys.reflector.module).at(0);
		if ('a' <= mod and mod <= 'z')
		{
			std::cout << "WARNING: '" << mod << "' is not uppercase, converting" << std::endl;
			mod = 'A' + (mod - 'a');
		}
		if (not('A' <= mod and mod <= 'Z'))
		{
			std::cerr << "ERROR: '" << mod << "' has to be an upper case letter A-Z" << std::endl;
			rval = true;
		}
		data[g_Keys.reflector.module] = std::string(1, mod);
	}
	if (isDefined(ErrorLevel::fatal, g_Keys.reflector.section, g_Keys.reflector.isDaemon, rval))
	{
		if (isDefined(ErrorLevel::fatal, g_Keys.reflector.section, g_Keys.reflector.user, rval))
		{
			const auto user = GetString(g_Keys.reflector.section, g_Keys.reflector.user);
			struct passwd *pwitem = getpwnam(user.c_str());
			if (nullptr == pwitem)
			{
				std::cerr << "ERROR: user '" << user << "' was not found in the user db" << std::endl;
				rval = true;
			}
		}
	}
	isDefined(ErrorLevel::fatal, g_Keys.reflector.section, g_Keys.reflector.isDuplex,     rval);
	isDefined(ErrorLevel::fatal, g_Keys.reflector.section, g_Keys.reflector.timeOut,      rval);
	isDefined(ErrorLevel::fatal, g_Keys.reflector.section, g_Keys.reflector.can,          rval);
	isDefined(ErrorLevel::fatal, g_Keys.reflector.section, g_Keys.reflector.isprivate,    rval);
	isDefined(ErrorLevel::fatal, g_Keys.reflector.section, g_Keys.reflector.allowEncrypt, rval);
	isDefined(ErrorLevel::fatal, g_Keys.reflector.section, g_Keys.reflector.netModeHang,  rval);
	isDefined(ErrorLevel::fatal, g_Keys.reflector.section, g_Keys.reflector.rfModeHang,   rval);
	isDefined(ErrorLevel::fatal, g_Keys.reflector.section, g_Keys.reflector.debug,        rval);

	// Modem section
	if (isDefined(ErrorLevel::fatal, g_Keys.modem.section, g_Keys.modem.protocol, rval))
	{
		const auto protocol = GetString(g_Keys.modem.section, g_Keys.modem.protocol);
		if (0 == protocol.compare("uart"))
		{
			if (isDefined(ErrorLevel::fatal, g_Keys.modem.section, g_Keys.modem.uartPort, rval))
			{
				const auto path = GetString(g_Keys.modem.section, g_Keys.modem.uartPort);
				checkPath(g_Keys.modem.section, g_Keys.modem.uartPort, path, std::filesystem::file_type::character);
			}
			if (isDefined(ErrorLevel::fatal, g_Keys.modem.section, g_Keys.modem.uartSpeed, rval))
			{
				const auto speed = GetUnsigned(g_Keys.modem.section, g_Keys.modem.uartSpeed);
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
				const auto addr = GetString(g_Keys.modem.section, g_Keys.modem.localAddress);
				if (not(std::regex_match(addr, IPv4RegEx)) and not(std::regex_match(addr, IPv6RegEx)))
				{
					std::cerr << "ERROR: [" << g_Keys.modem.section << "]" << g_Keys.modem.localAddress << " does not look like an internet address" << std::endl;
					rval = true;
				}
			}
			isDefined(ErrorLevel::fatal, g_Keys.modem.section, g_Keys.modem.localPort, rval);

			if (isDefined(ErrorLevel::fatal, g_Keys.modem.section, g_Keys.modem.modemAddress, rval))
			{
				const auto addr = GetString(g_Keys.modem.section, g_Keys.modem.modemAddress);
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
				const auto path = GetString(g_Keys.modem.section, g_Keys.modem.i2cAddress);
				checkPath(g_Keys.modem.section, g_Keys.modem.i2cAddress, path, std::filesystem::file_type::character);
			}
			isDefined(ErrorLevel::fatal, g_Keys.modem.section, g_Keys.modem.i2cPort, rval);
		}
		else if (0 == protocol.compare("null"))
		{
			;
		}
		else
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

	isDefined(ErrorLevel::fatal, g_Keys.modem.section, g_Keys.modem.rxFreq,     rval);
	isDefined(ErrorLevel::fatal, g_Keys.modem.section, g_Keys.modem.txFreq,     rval);

	isDefined(ErrorLevel::fatal, g_Keys.modem.section, g_Keys.modem.rxOffset,   rval);
	isDefined(ErrorLevel::fatal, g_Keys.modem.section, g_Keys.modem.txOffset,   rval);

	isDefined(ErrorLevel::fatal, g_Keys.modem.section, g_Keys.modem.rfLevel,    rval);
	isDefined(ErrorLevel::fatal, g_Keys.modem.section, g_Keys.modem.rxLevel,    rval);
	isDefined(ErrorLevel::fatal, g_Keys.modem.section, g_Keys.modem.txLevel,    rval);
	isDefined(ErrorLevel::fatal, g_Keys.modem.section, g_Keys.modem.cwLevel,    rval);

	isDefined(ErrorLevel::fatal, g_Keys.modem.section, g_Keys.modem.rxDCOffset, rval);
	isDefined(ErrorLevel::fatal, g_Keys.modem.section, g_Keys.modem.txDCOffset, rval);

	if (Contains(g_Keys.modem.rssiMapFile))
	{
		const auto path = GetString(g_Keys.modem.section, g_Keys.modem.rssiMapFile);
		checkPath(g_Keys.modem.section, g_Keys.modem.rssiMapFile, path, std::filesystem::file_type::regular);
	}
	isDefined(ErrorLevel::fatal, g_Keys.modem.section, g_Keys.modem.trace, rval);
	isDefined(ErrorLevel::fatal, g_Keys.modem.section, g_Keys.modem.debug, rval);

	// Log section
	isDefined(ErrorLevel::fatal, g_Keys.log.section, g_Keys.log.displayLevel, rval);
	isDefined(ErrorLevel::fatal, g_Keys.log.section, g_Keys.log.fileLevel,    rval);
	isDefined(ErrorLevel::fatal, g_Keys.log.section, g_Keys.log.fileName,     rval);
	if(isDefined(ErrorLevel::fatal, g_Keys.log.section, g_Keys.log.filePath,  rval))
	{
		const std::string path = GetString(g_Keys.log.section, g_Keys.log.filePath);
		checkPath(g_Keys.log.section, g_Keys.log.filePath, path, std::filesystem::file_type::directory);
	}
	isDefined(ErrorLevel::fatal, g_Keys.log.section, g_Keys.log.rotate,       rval);

	// Gateway section
	isDefined(ErrorLevel::fatal, g_Keys.gateway.section, g_Keys.gateway.ipv4, rval);
	isDefined(ErrorLevel::fatal, g_Keys.gateway.section, g_Keys.gateway.ipv6, rval);
	isDefined(ErrorLevel::fatal, g_Keys.gateway.section, g_Keys.gateway.maintainLink, rval);
	if (data.contains(g_Keys.gateway.startupLink))
	{
		const auto ref = GetString(g_Keys.gateway.section, g_Keys.gateway.startupLink);
		if (not(std::regex_match(ref, MrefdCS) and not(std::regex_match(ref, UrfdCS))))
		{
			std::cout << "WARNING: [" << g_Keys.gateway.section << "]" << g_Keys.gateway.startupLink << "doesn't look like a reflector module" << std::endl;
		}
	}
	if (isDefined(ErrorLevel::mild, g_Keys.gateway.section, g_Keys.gateway.hostPath,   rval))
	{
		const auto path = GetString(g_Keys.gateway.section, g_Keys.gateway.hostPath);
		checkPath(g_Keys.gateway.section, g_Keys.gateway.hostPath, path, std::filesystem::file_type::regular);
	}
	if (isDefined(ErrorLevel::mild, g_Keys.gateway.section, g_Keys.gateway.myHostPath,    rval))
	{
		const auto path = GetString(g_Keys.gateway.section, g_Keys.gateway.myHostPath);
		checkPath(g_Keys.gateway.section, g_Keys.gateway.myHostPath, path, std::filesystem::file_type::regular);
	}
	isDefined(ErrorLevel::fatal, g_Keys.gateway.section, g_Keys.gateway.allowNotTranscoded, rval);
	if (isDefined(ErrorLevel::fatal, g_Keys.gateway.section, g_Keys.gateway.audioFolder, rval))
	{
		const auto path = GetString(g_Keys.gateway.section, g_Keys.gateway.audioFolder);
		checkPath(g_Keys.gateway.section, g_Keys.gateway.audioFolder, path, std::filesystem::file_type::directory);
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

float CConfigure::getFloat(const std::string &valuestr, const std::string &label, float min, float max, float def) const
{
	auto f = std::strtof(valuestr.c_str(), nullptr);
	if ( f < min || f > max )
	{
		std::cout << "WARNING: line #" << counter << ": " << label << " is out of range. Reset to " << def << std::endl;
		f = def;
	}
	return f;
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
			std::cout << "a block device";
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

bool CConfigure::Contains(const std::string &key) const
{
	return data.contains(key);
}

std::string CConfigure::GetString(const std::string &section, const std::string &key) const
{
	std::string str;
	if (data.contains(section) && data[section].contains(key))
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
	if (data.contains(section) && data[section].contains(key))
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
	if (data.contains(section) && data[section].contains(key))
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
	if (data.contains(section) && data[section].contains(key))
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
	if (data.contains(section) && data[section].contains(key))
	{
		if (data[key].is_boolean())
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
	if (data.contains(section) && data[section].contains(key))
	{
		return data[key].is_string();
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
