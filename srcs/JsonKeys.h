/*

         mspot - an M17-only HotSpot using an RPi CC1200 hat
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
		const std::string section, callsign, module, can, radioTypeIsV3, isprivate, debug;
	}
	repeater
	{
		"Repeater", "Callsign", "Module", "CAN", "RadioTypeIsV3", "IsPrivate", "Debug"
	};

	struct LOG
	{
		const std::string section, level, dashpath;
	}
	log
	{
		"Log", "Level", "DashboardPath"
	};

	struct MODEM
	{
		const std::string section, gpiochipDevice, uartDevice, uartBaudRate, boot0, nrst, rxFreq, txFreq, afc, freqCorr, txPower, debug;
	}
	modem
	{
		"Modem", "GpioChipDevice", "UartDevice", "UartBaudRate", "BOOT0", "nRST", "RXFrequency", "TXFrequency", "AFC", "FreqCorrection", "TXPower", "Debug"
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
