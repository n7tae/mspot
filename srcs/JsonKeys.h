/****************************************************************
 *                                                              *
 *            mspot - An M17-only Hotspot/Repeater              *
 *                                                              *
 *         Copyright (c) 2024 by Thomas A. Early N7TAE          *
 *                                                              *
 * See the LICENSE file for details about the software license. *
 *                                                              *
 ****************************************************************/

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
