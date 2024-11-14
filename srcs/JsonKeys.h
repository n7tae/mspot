/****************************************************************
 *                                                              *
 *             More - An M17-only Repeater/HotSpot              *
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
	struct GENERAL
	{
		const std::string section, callsign, module, isdaemon, allowEncrypt, user, can, isprivate;
	}
	general
	{
		"General", "Callsign", "Module", "IsDaemon", "AllowEncrypt", "UserName", "CAN", "IsPrivate"
	};

	struct LOG
	{
		const std::string section, displayLevel, fileLevel, filePath, fileName, rotate;
	}
	log
	{
		"Log", "DisplayLevel", "FileLevel", "FilePath", "FileName", "FileRotate"
	};

	struct MODEM
	{
		const std::string section, protocol, isDuplex, uartPort, uartSpeed, i2cPort, i2cAddress, modemAddress, modemPort, localAddress, localPort, rxFreq, txFreq, rxOffset, txOffset, rxDCOffset, txDCOffset, rxInvert, txInvert, pttInvert, txHang, txDelay, txLevel, rxLevel, rfLevel, rssiMapFile, trace, debug;
	}
	modem
	{
		"Modem", "Protocol", "IsDuplex", "UartPort", "UartSpeed", "I2CPort", "I2CAddress", "ModemAddress", "ModemPort", "LocalAddress", "LocalPort", "RXFrequency", "TXFrequency", "RXOffset", "TXOffset", "RXDCOffset", "TXDCOffset", "RXInvert", "TXInvert", "PTTInvert", "TXHang", "TXDelay", "TXLevel", "RXLevel", "RFLevel", "RssiMapFilePath", "Trace", "Debug"
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
