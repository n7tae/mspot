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
		const std::string section, callsign, module, isduplex, isdaemon, user, txfreq, rxfreq, can, isprivate;
	}
	general
	{
		"General", "Callsign", "Module", "IsDuplex", "IsDaemon", "UserName", "TxFrequency", "RxFrequency", "CAN", "IsPrivate"
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
		const std::string section, protocol, uartPort, uartSpeed, i2cPort, i2cAddress, modemAddress, modemPort, localAddress, localPort, rxOffset, txOffset, rxDCOffset, txDCOffset, rxInvert, txInvert, pttInvert, txHang, txDelay, txLevel, rxLevel, rfLevel, rssiMapFile, trace, debug;
	}
	modem
	{
		"Modem", "Protocol", "UartPort", "UartSpeed", "I2CPort", "I2CAddress", "ModemAddress", "ModemPort", "LocalAddress", "LocalPort", "RXOffset", "TXOffset", "RXDCOffset", "TXDCOffset", "RXInvert", "TXInvert", "PTTInvert", "TXHang", "TXDelay", "TXLevel", "TXLevel", "RFLevel", "RssiMapFile", "Trace", "Debug"
	};

	struct GATEWAY
	{
		const std::string section, ipv4, ipv6, startupLink;
	}
	gateway
	{
		"Gateway", "EnableIPv4", "EnableIPv6", "StartupLink"
	};
};
