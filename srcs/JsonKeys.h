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

#include <string>

#pragma once

// configuration key names
// the string values have to be unique within struct SJsonKeys
struct SJsonKeys
{
	struct Repeater
	{
		const std::string section, callsign, module, can, radioTypeIsV3, debug;
	}
	repeater
	{
		"Repeater", "Callsign", "Module", "CAN", "RadioTypeIsV3", "Debug"
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
		const std::string section, ipv4, ipv6, startupLink, maintainLink, hostPath, myHostPath, dbPath, allowNotTranscoded, audioFolder;
	}
	gateway
	{
		"Gateway", "EnableIPv4", "EnableIPv6", "StartupLink", "MaintainLink", "HostPath", "MyHostPath", "DBPath", "AllowNotTranscoded", "AudioFolderPath"
	};

	struct DASHBOARD
	{
		const std::string section, refresh, lhcount, showorder;
	}
	dashboard
	{
		"Dashboard", "RefreshPeriod", "LastHeardSize", "ShowOrder"
	};
};
