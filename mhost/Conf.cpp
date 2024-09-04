/*
 *   Copyright (C) 2015-2023 by Jonathan Naylor G4KLX
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "Conf.h"
#include "Log.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>

const int BUFFER_SIZE = 500;

enum SECTION {
	SECTION_NONE,
	SECTION_GENERAL,
	SECTION_INFO,
	SECTION_LOG,
	SECTION_MODEM,
	SECTION_M17,
	SECTION_M17_NETWORK
};

CConf::CConf(const std::string& file) :
m_file(file),
m_callsign(),
m_timeout(120U),
m_duplex(true),
m_display(),
m_daemon(false),
m_rxFrequency(0U),
m_txFrequency(0U),
m_logDisplayLevel(0U),
m_logFileLevel(0U),
m_logFilePath(),
m_logFileRoot(),
m_logFileRotate(true),
m_modemProtocol("uart"),
m_modemUARTPort(),
m_modemUARTSpeed(115200U),
m_modemI2CPort(),
m_modemI2CAddress(0x22U),
m_modemModemAddress(),
m_modemModemPort(0U),
m_modemLocalAddress(),
m_modemLocalPort(0U),
m_modemRXInvert(false),
m_modemTXInvert(false),
m_modemPTTInvert(false),
m_modemTXDelay(100U),
m_modemTXOffset(0),
m_modemRXOffset(0),
m_modemRXDCOffset(0),
m_modemTXDCOffset(0),
m_modemRFLevel(100.0F),
m_modemRXLevel(50.0F),
m_modemM17TXLevel(50.0F),
m_modemTrace(false),
m_modemDebug(false),
m_m17CAN(0U),
m_m17SelfOnly(false),
m_m17AllowEncryption(false),
m_m17TXHang(5U),
m_m17ModeHang(10U),
m_m17GatewayAddress(),
m_m17GatewayPort(0U),
m_m17LocalAddress(),
m_m17LocalPort(0U),
m_m17NetworkModeHang(3U),
m_m17NetworkDebug(false)
{
}

CConf::~CConf()
{
}

bool CConf::read()
{
	FILE* fp = ::fopen(m_file.c_str(), "rt");
	if (fp == NULL) {
		::fprintf(stderr, "Couldn't open the .ini file - %s\n", m_file.c_str());
		return false;
	}

	SECTION section = SECTION_NONE;

	char buffer[BUFFER_SIZE];
	while (::fgets(buffer, BUFFER_SIZE, fp) != NULL) {
		if (buffer[0U] == '#')
			continue;

		if (buffer[0U] == '[') {
			if (::strncmp(buffer, "[General]", 9U) == 0)
				section = SECTION_GENERAL;
			else if (::strncmp(buffer, "[Info]", 6U) == 0)
				section = SECTION_INFO;
			else if (::strncmp(buffer, "[Log]", 5U) == 0)
				section = SECTION_LOG;
			else if (::strncmp(buffer, "[Modem]", 7U) == 0)
				section = SECTION_MODEM;
			else if (::strncmp(buffer, "[M17]", 5U) == 0)
				section = SECTION_M17;
			else if (::strncmp(buffer, "[M17 Network]", 13U) == 0)
				section = SECTION_M17_NETWORK;
			else
				section = SECTION_NONE;

			continue;
		}

		char* key = ::strtok(buffer, " \t=\r\n");
		if (key == NULL)
			continue;

		char* value = ::strtok(NULL, "\r\n");
		if (value == NULL)
			continue;

		// Remove quotes from the value
		size_t len = ::strlen(value);
		if (len > 1U && *value == '"' && value[len - 1U] == '"') {
			value[len - 1U] = '\0';
			value++;
		} else {
			char *p;

			// if value is not quoted, remove after # (to make comment)
			if ((p = strchr(value, '#')) != NULL)
				*p = '\0';

			// remove trailing tab/space
			for (p = value + strlen(value) - 1U; p >= value && (*p == '\t' || *p == ' '); p--)
				*p = '\0';
		}

		if (section == SECTION_GENERAL) {
			if (::strcmp(key, "Callsign") == 0) {
				// Convert the callsign to upper case
				for (unsigned int i = 0U; value[i] != 0; i++)
					value[i] = ::toupper(value[i]);
				m_callsign = value;
			}
			else if (::strcmp(key, "Timeout") == 0)
				m_timeout = (unsigned int)::atoi(value);
			else if (::strcmp(key, "Duplex") == 0)
				m_duplex = ::atoi(value) == 1;
			else if (::strcmp(key, "ModeHang") == 0)
				m_m17NetworkModeHang = m_m17ModeHang = (unsigned int)::atoi(value);
			else if (::strcmp(key, "RFModeHang") == 0)
				m_m17ModeHang = (unsigned int)::atoi(value);
			else if (::strcmp(key, "NetModeHang") == 0)
				m_m17NetworkModeHang = (unsigned int)::atoi(value);
			else if (::strcmp(key, "Display") == 0)
				m_display = value;
			else if (::strcmp(key, "Daemon") == 0)
				m_daemon = ::atoi(value) == 1;
		} else if (section == SECTION_INFO) {
			if (::strcmp(key, "TXFrequency") == 0)
				m_txFrequency = (unsigned int)::atoi(value);
			else if (::strcmp(key, "RXFrequency") == 0)
				m_rxFrequency = (unsigned int)::atoi(value);
		} else if (section == SECTION_LOG) {
			if (::strcmp(key, "FilePath") == 0)
				m_logFilePath = value;
			else if (::strcmp(key, "FileRoot") == 0)
				m_logFileRoot = value;
			else if (::strcmp(key, "FileLevel") == 0)
				m_logFileLevel = (unsigned int)::atoi(value);
			else if (::strcmp(key, "DisplayLevel") == 0)
				m_logDisplayLevel = (unsigned int)::atoi(value);
			else if (::strcmp(key, "FileRotate") == 0)
				m_logFileRotate = ::atoi(value) == 1;
		} else if (section == SECTION_MODEM) {
			if (::strcmp(key, "Protocol") == 0)
				m_modemProtocol = value;
			else if (::strcmp(key, "UARTPort") == 0)
				m_modemUARTPort = value;
			else if (::strcmp(key, "UARTSpeed") == 0)
				m_modemUARTSpeed = (unsigned int)::atoi(value);
			else if (::strcmp(key, "I2CPort") == 0)
				m_modemI2CPort = value;
			else if (::strcmp(key, "I2CAddress") == 0)
				m_modemI2CAddress = (unsigned int)::strtoul(value, NULL, 16);
			else if (::strcmp(key, "ModemAddress") == 0)
				m_modemModemAddress = value;
			else if (::strcmp(key, "ModemPort") == 0)
				m_modemModemPort = (unsigned short)::atoi(value);
			else if (::strcmp(key, "LocalAddress") == 0)
				m_modemLocalAddress = value;
			else if (::strcmp(key, "LocalPort") == 0)
				m_modemLocalPort = (unsigned short)::atoi(value);
			else if (::strcmp(key, "RXInvert") == 0)
				m_modemRXInvert = ::atoi(value) == 1;
			else if (::strcmp(key, "TXInvert") == 0)
				m_modemTXInvert = ::atoi(value) == 1;
			else if (::strcmp(key, "PTTInvert") == 0)
				m_modemPTTInvert = ::atoi(value) == 1;
			else if (::strcmp(key, "TXDelay") == 0)
				m_modemTXDelay = (unsigned int)::atoi(value);
			else if (::strcmp(key, "RXOffset") == 0)
				m_modemRXOffset = ::atoi(value);
			else if (::strcmp(key, "TXOffset") == 0)
				m_modemTXOffset = ::atoi(value);
			else if (::strcmp(key, "RXDCOffset") == 0)
				m_modemRXDCOffset = ::atoi(value);
			else if (::strcmp(key, "TXDCOffset") == 0)
				m_modemTXDCOffset = ::atoi(value);
			else if (::strcmp(key, "RFLevel") == 0)
				m_modemRFLevel = float(::atof(value));
			else if (::strcmp(key, "RXLevel") == 0)
				m_modemRXLevel = float(::atof(value));
			else if (::strcmp(key, "TXLevel") == 0)
				m_modemM17TXLevel = float(::atof(value));
			else if (::strcmp(key, "M17TXLevel") == 0)
				m_modemM17TXLevel = float(::atof(value));
			else if (::strcmp(key, "Trace") == 0)
				m_modemTrace = ::atoi(value) == 1;
			else if (::strcmp(key, "Debug") == 0)
				m_modemDebug = ::atoi(value) == 1;
		} else if (section == SECTION_M17) {
			if (::strcmp(key, "CAN") == 0)
				m_m17CAN = (unsigned int)::atoi(value);
			else if (::strcmp(key, "SelfOnly") == 0)
				m_m17SelfOnly = ::atoi(value) == 1;
			else if (::strcmp(key, "AllowEncryption") == 0)
				m_m17AllowEncryption = ::atoi(value) == 1;
			else if (::strcmp(key, "TXHang") == 0)
				m_m17TXHang = (unsigned int)::atoi(value);
			else if (::strcmp(key, "ModeHang") == 0)
				m_m17ModeHang = (unsigned int)::atoi(value);
		} else if (section == SECTION_M17_NETWORK) {
			if (::strcmp(key, "LocalAddress") == 0)
				m_m17LocalAddress = value;
			else if (::strcmp(key, "LocalPort") == 0)
				m_m17LocalPort = (unsigned short)::atoi(value);
			else if (::strcmp(key, "GatewayAddress") == 0)
				m_m17GatewayAddress = value;
			else if (::strcmp(key, "GatewayPort") == 0)
				m_m17GatewayPort = (unsigned short)::atoi(value);
			else if (::strcmp(key, "ModeHang") == 0)
				m_m17NetworkModeHang = (unsigned int)::atoi(value);
			else if (::strcmp(key, "Debug") == 0)
				m_m17NetworkDebug = ::atoi(value) == 1;
		}
	}

	::fclose(fp);

	return true;
}

std::string CConf::getCallsign() const
{
	return m_callsign;
}

unsigned int CConf::getTimeout() const
{
	return m_timeout;
}

bool CConf::getDuplex() const
{
	return m_duplex;
}

std::string CConf::getDisplay() const
{
	return m_display;
}

bool CConf::getDaemon() const
{
	return m_daemon;
}

unsigned int CConf::getRXFrequency() const
{
	return m_rxFrequency;
}

unsigned int CConf::getTXFrequency() const
{
	return m_txFrequency;
}

unsigned int CConf::getLogDisplayLevel() const
{
	return m_logDisplayLevel;
}

unsigned int CConf::getLogFileLevel() const
{
	return m_logFileLevel;
}

std::string CConf::getLogFilePath() const
{
	return m_logFilePath;
}

std::string CConf::getLogFileRoot() const
{
	return m_logFileRoot;
}

bool CConf::getLogFileRotate() const
{
	return m_logFileRotate;
}

std::string CConf::getModemProtocol() const
{
	return m_modemProtocol;
}

std::string CConf::getModemUARTPort() const
{
	return m_modemUARTPort;
}

unsigned int CConf::getModemUARTSpeed() const
{
	return m_modemUARTSpeed;
}

std::string CConf::getModemI2CPort() const
{
	return m_modemI2CPort;
}

unsigned int CConf::getModemI2CAddress() const
{
	return m_modemI2CAddress;
}

std::string CConf::getModemModemAddress() const
{
	return m_modemModemAddress;
}

unsigned short CConf::getModemModemPort() const
{
	return m_modemModemPort;
}

std::string CConf::getModemLocalAddress() const
{
	return m_modemLocalAddress;
}

unsigned short CConf::getModemLocalPort() const
{
	return m_modemLocalPort;
}

bool CConf::getModemRXInvert() const
{
	return m_modemRXInvert;
}

bool CConf::getModemTXInvert() const
{
	return m_modemTXInvert;
}

bool CConf::getModemPTTInvert() const
{
	return m_modemPTTInvert;
}

unsigned int CConf::getModemTXDelay() const
{
	return m_modemTXDelay;
}

int CConf::getModemRXOffset() const
{
	return m_modemRXOffset;
}

int CConf::getModemTXOffset() const
{
	return m_modemTXOffset;
}

int CConf::getModemRXDCOffset() const
{
	return m_modemRXDCOffset;
}

int CConf::getModemTXDCOffset() const
{
	return m_modemTXDCOffset;
}

float CConf::getModemRFLevel() const
{
	return m_modemRFLevel;
}

float CConf::getModemRXLevel() const
{
	return m_modemRXLevel;
}

float CConf::getModemM17TXLevel() const
{
	return m_modemM17TXLevel;
}

bool CConf::getModemTrace() const
{
	return m_modemTrace;
}

std::string CConf::getModemRSSIMappingFile() const
{
	return m_modemRSSIMappingFile;
}

bool CConf::getModemDebug() const
{
	return m_modemDebug;
}

unsigned int CConf::getM17CAN() const
{
	return m_m17CAN;
}

bool CConf::getM17SelfOnly() const
{
	return m_m17SelfOnly;
}

bool CConf::getM17AllowEncryption() const
{
	return m_m17AllowEncryption;
}

unsigned int CConf::getM17TXHang() const
{
	return m_m17TXHang;
}

unsigned int CConf::getM17ModeHang() const
{
	return m_m17ModeHang;
}

std::string CConf::getM17GatewayAddress() const
{
	return m_m17GatewayAddress;
}

unsigned short CConf::getM17GatewayPort() const
{
	return m_m17GatewayPort;
}

std::string CConf::getM17LocalAddress() const
{
	return m_m17LocalAddress;
}

unsigned short CConf::getM17LocalPort() const
{
	return m_m17LocalPort;
}

unsigned int CConf::getM17NetworkModeHang() const
{
	return m_m17NetworkModeHang;
}

bool CConf::getM17NetworkDebug() const
{
	return m_m17NetworkDebug;
}
