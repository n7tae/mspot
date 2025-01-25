/*

         mspot - an M17-only HotSpot using an MMDVM device
            Copyright (C) 2025 Thomas A. Early N7TAE

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

*/

#include <string>

#pragma once

// configuration key names
// the string values have to be unique within struct SJsonKeys
struct SJsonKeys
{
	struct Repeater
	{
		const std::string section, callsign, module, timeOut, isDuplex, isDaemon, allowEncrypt, user, can, isprivate, debug;
	}
	repeater
	{
		"Repeater", "Callsign", "Module", "Timeout", "IsDuplex", "IsDaemon", "AllowEncrypt", "UserName", "CAN", "IsPrivate", "Debug"
	};

	struct LOG
	{
		const std::string section, displayLevel, fileLevel, filePath, fileName, rotate;
	}
	log
	{
		"Log", "DisplayLevel", "FileLevel", "FilePath", "FileName", "FileRotate"
	};

	struct CWID
	{
		const std::string section, enable, time, message;
	}
	cwid
	{
		"CW Id", "Enable", "Time", "Message"
	};

	struct MODEM
	{
		const std::string section, protocol, uartPort, uartSpeed, i2cPort, i2cAddress, modemAddress, modemPort, localAddress, localPort, rxFreq, txFreq, rxOffset, txOffset, rxDCOffset, txDCOffset, rxInvert, txInvert, pttInvert, txHang, txDelay, txLevel, rxLevel, rfLevel, cwLevel, rssiMapFile, trace, debug;
	}
	modem
	{
		"Modem", "Protocol", "UartPort", "UartSpeed", "I2CPort", "I2CAddress", "ModemAddress", "ModemPort", "LocalAddress", "LocalPort", "RXFrequency", "TXFrequency", "RXOffset", "TXOffset", "RXDCOffset", "TXDCOffset", "RXInvert", "TXInvert", "PTTInvert", "TXHang", "TXDelay", "TXLevel", "RXLevel", "RFLevel", "CWLevel", "RssiMapFilePath", "Trace", "Debug"
	};

	struct GATEWAY
	{
		const std::string section, ipv4, ipv6, startupLink, maintainLink, hostPath, myHostPath, allowNotTranscoded, audioFolder;
	}
	gateway
	{
		"Gateway", "EnableIPv4", "EnableIPv6", "StartupLink", "MaintainLink", "HostPath", "MyHostPath", "AllowNotTranscoded", "AudioFolderPath"
	};
};
