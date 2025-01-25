// Copyright (C) 2015-2021,2023,2024 by Jonathan Naylor G4KLX

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

#include <cstdio>
#include <vector>
#include <cstdlib>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <pwd.h>
#include <thread>
#include <chrono>

#include "M17Host.h"
#include "NullController.h"
#include "UARTController.h"
#include "I2CController.h"
#include "UDPController.h"
#include "Version.h"
#include "StopWatch.h"
#include "Defines.h"
#include "Log.h"
#include "Configure.h"
#include "Version.h"

extern CConfigure g_Cfg;
extern CVersion   g_Version;

const char* HEADER1 = "This software is for use on amateur radio networks only,";
const char* HEADER2 = "it is to be used for educational purposes only. Its use on";
const char* HEADER3 = "commercial networks is strictly prohibited.";
const char* HEADER4 = "Copyright(C) 2015-2024 by Jonathan Naylor, G4KLX and others";
const char* HEADER5 = "Copyright(C) 2024,2025 by Thomas A. Early, N7TAE";

CM17Host::CM17Host() :
m_cwIdTimer(1000U),
m_duplex(false),
m_timeout(180U),
m_cwIdTime(0U),
m_callsign(),
m_id(0U),
m_cwCallsign()
{
}

CM17Host::~CM17Host()
{
}

bool CM17Host::Start()
{
	LogInfo(HEADER1);
	LogInfo(HEADER2);
	LogInfo(HEADER3);
	LogInfo(HEADER4);
	LogInfo(HEADER5);

	LogInfo("MMDVMHost-%s is starting", g_Version.GetString());
	LogInfo("Built %s %s", __TIME__, __DATE__);

	m_duplex = g_Cfg.GetBoolean(g_Keys.repeater.section, g_Keys.repeater.isDuplex);
	m_callsign.assign(g_Cfg.GetString(g_Keys.repeater.section, g_Keys.repeater.callsign));
	m_timeout = g_Cfg.GetUnsigned(g_Keys.repeater.section, g_Keys.repeater.timeOut);
	const bool selfOnly = g_Cfg.GetBoolean(g_Keys.repeater.section, g_Keys.repeater.isprivate);
	const unsigned int can = g_Cfg.GetUnsigned(g_Keys.repeater.section, g_Keys.repeater.can);
	const bool allowEncryption = g_Cfg.GetBoolean(g_Keys.repeater.section, g_Keys.repeater.allowEncrypt);

	LogInfo("Reflector Parameters");
	LogInfo("    Callsign: %s", m_callsign.c_str());
	LogInfo("    Duplex: %s", m_duplex ? "yes" : "no");
	LogInfo("    Timeout: %us", m_timeout);
	LogInfo("    Self Only: %s", selfOnly ? "true" : "false");
	LogInfo("    CAN: %u", can);
	LogInfo("    Allow Encryption: %s", allowEncryption ? "true" : "false");

	if (createModem())
		return true;

	if (not m_modem->hasM17())
	{
		LogError("M17 enabled in the host but not in the modem firmware, disabling");
		return true;
	}

	LogInfo("Opening network connections");

	if (createM17Network())
		return true;

	if (g_Cfg.GetBoolean(g_Keys.cwid.section, g_Keys.cwid.enable))
	{
		const unsigned int time = g_Cfg.GetUnsigned(g_Keys.cwid.section, g_Keys.cwid.time);
		m_cwCallsign.assign(g_Cfg.GetString(g_Keys.cwid.section, g_Keys.cwid.message));

		LogInfo("CW Id Parameters");
		LogInfo("    Time: %u mins", time);
		LogInfo("    Callsign: %s", m_cwCallsign.c_str());

		m_cwIdTime = time * 60U;

		m_cwIdTimer.setTimeout(m_cwIdTime / 4U);
		m_cwIdTimer.start();
	}

	// For all modes we handle RSSI
	std::string rssiMappingFile;
	if (g_Cfg.IsString(g_Keys.modem.section, g_Keys.modem.rssiMapFile))
	{
		rssiMappingFile.assign(g_Cfg.GetString(g_Keys.modem.section, g_Keys.modem.rssiMapFile));

		m_rssi = std::make_unique<CRSSIInterpolator>();
		if (m_rssi->load(rssiMappingFile))
		{
			LogInfo("RSSI");
			LogInfo("    Mapping File: %s", rssiMappingFile.c_str());
		}
		else
			m_rssi.reset();
	}

	LogInfo("Starting protocol handler");

	m_m17 = std::make_unique<CM17Control>(m_callsign, can, selfOnly, allowEncryption, m_m17Network, m_timeout, m_duplex, m_rssi.get());

	setMode(MODE_M17);

	hostFuture = std::async(std::launch::async, &CM17Host::Run, this);
	if (not hostFuture.valid())
	{
		LogError("Cannot start M17Host thread");
		return true;
	}

	LogInfo("Starting gateway");
	m_m17Gateway = std::make_unique<CM17Gateway>();
	if (m_m17Gateway->Start())
		return true;

	LogInfo("M17Host-%s is running", g_Version.GetString());
	return false;
}

void CM17Host::Run()
{
	keep_running = true;
	CStopWatch stopWatch;
	stopWatch.start();

	while (keep_running) {
		bool lockout = m_modem->hasLockout();

		if (lockout && m_mode != MODE_LOCKOUT)
			setMode(MODE_LOCKOUT);
		else if (!lockout && m_mode == MODE_LOCKOUT)
			setMode(MODE_M17);

		bool error = m_modem->hasError();
		if (error && m_mode != MODE_ERROR)
			setMode(MODE_ERROR);
		else if (!error && m_mode == MODE_ERROR)
			setMode(MODE_M17);

		unsigned char data[500U];
		unsigned int len;
		bool ret;

		len = m_modem->readM17Data(data);
		if (m_m17 != NULL && len > 0U) {
			if (m_mode == MODE_M17) {
				m_m17->writeModem(data, len);
			} else if (m_mode != MODE_LOCKOUT) {
				LogWarning("M17 modem data received when in mode %u", m_mode);
			}
		}

		if (m_m17 != NULL) {
			ret = m_modem->hasM17Space();
			if (ret) {
				len = m_m17->readModem(data);
				if (len > 0U) {
					if (m_mode == MODE_M17) {
						m_modem->writeM17Data(data, len);
					} else if (m_mode != MODE_LOCKOUT) {
						LogWarning("M17 data received when in mode %u", m_mode);
					}
				}
			}
		}

		unsigned int ms = stopWatch.elapsed();
		stopWatch.start();

		m_modem->clock(ms);

		if (m_m17 != NULL)
			m_m17->clock(ms);
		if (m_m17Network != NULL)
			m_m17Network->clock(ms);

		m_cwIdTimer.clock(ms);
		if (m_cwIdTimer.isRunning() && m_cwIdTimer.hasExpired()) {
			if (!m_modem->hasTX()){
				LogDebug("sending CW ID");
				m_modem->sendCWId(m_cwCallsign);

				m_cwIdTimer.setTimeout(m_cwIdTime);
				m_cwIdTimer.start();
			}
		}

		if (ms < 5U)
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
}

void CM17Host::Stop()
{
	keep_running = false;

	LogInfo("Closing the gateway");
	if (m_m17Gateway)
	{
		m_m17Gateway->Stop();
		m_m17Gateway.reset();
	}

	if (hostFuture.valid())
		hostFuture.get();

	setMode(MODE_QUIT);

	LogInfo("Closing network");
	if (m_m17Network != NULL) {
		m_m17Network->close();
		m_m17Network.reset();
	}

	LogInfo("Closing protocol handler");
	m_m17.release();

	LogInfo("Closing the modem");
	if (m_modem)
	{
		m_modem->close();
		m_modem.reset();
	}
	m_rssi.reset();

	LogInfo("M17Host-%s has stopped", g_Version.GetString());
}

bool CM17Host::createModem()
{
	const std::string     protocol(g_Cfg.GetString(g_Keys.modem.section, g_Keys.modem.protocol));
	const std::string     uartPort(g_Cfg.GetString(g_Keys.modem.section, g_Keys.modem.uartPort));
	const std::string      i2cPort(g_Cfg.GetString(g_Keys.modem.section, g_Keys.modem.i2cPort));
	const std::string modemAddress(g_Cfg.GetString(g_Keys.modem.section, g_Keys.modem.modemAddress));
	const std::string localAddress(g_Cfg.GetString(g_Keys.modem.section, g_Keys.modem.localAddress));
	const int rxOffset   = g_Cfg.GetInt(g_Keys.modem.section, g_Keys.modem.rxOffset);
	const int txOffset   = g_Cfg.GetInt(g_Keys.modem.section, g_Keys.modem.txOffset);
	const int rxDCOffset = g_Cfg.GetInt(g_Keys.modem.section, g_Keys.modem.rxDCOffset);
	const int txDCOffset = g_Cfg.GetInt(g_Keys.modem.section, g_Keys.modem.txDCOffset);
	const bool rxInvert  = g_Cfg.GetBoolean(g_Keys.modem.section, g_Keys.modem.rxInvert);
	const bool txInvert  = g_Cfg.GetBoolean(g_Keys.modem.section, g_Keys.modem.txInvert);
	const bool pttInvert = g_Cfg.GetBoolean(g_Keys.modem.section, g_Keys.modem.pttInvert);
	const bool trace     = g_Cfg.GetBoolean(g_Keys.modem.section, g_Keys.modem.trace);
	const bool debug     = g_Cfg.GetBoolean(g_Keys.modem.section, g_Keys.modem.debug);
	const float rfLevel     = g_Cfg.GetFloat(g_Keys.modem.section, g_Keys.modem.rfLevel);
	const float rxLevel     = g_Cfg.GetFloat(g_Keys.modem.section, g_Keys.modem.rxLevel);
	const float m17TXLevel  = g_Cfg.GetFloat(g_Keys.modem.section, g_Keys.modem.txLevel);
	const float cwIdTXLevel = g_Cfg.GetFloat(g_Keys.modem.section, g_Keys.modem.cwLevel);
	const unsigned int   uartSpeed = g_Cfg.GetUnsigned(g_Keys.modem.section, g_Keys.modem.uartSpeed);
	const unsigned int  i2cAddress = g_Cfg.GetUnsigned(g_Keys.modem.section, g_Keys.modem.i2cAddress);
	const unsigned int     txDelay = g_Cfg.GetUnsigned(g_Keys.modem.section, g_Keys.modem.txDelay);
	const unsigned int   m17TXHang = g_Cfg.GetUnsigned(g_Keys.modem.section, g_Keys.modem.txHang);
	const unsigned int rxFrequency = g_Cfg.GetUnsigned(g_Keys.modem.section, g_Keys.modem.rxFreq);
	const unsigned int txFrequency = g_Cfg.GetUnsigned(g_Keys.modem.section, g_Keys.modem.txFreq);
	const unsigned short modemPort = g_Cfg.GetUnsigned(g_Keys.modem.section, g_Keys.modem.modemPort);
	const unsigned short localPort = g_Cfg.GetUnsigned(g_Keys.modem.section, g_Keys.modem.localPort);

	LogInfo("Modem Parameters");
	LogInfo("    Protocol: %s", protocol.c_str());

	if (protocol == "uart")
	{
		LogInfo("    UART Port: %s", uartPort.c_str());
		LogInfo("    UART Speed: %u", uartSpeed);
	}
	else if (protocol == "udp")
	{
		LogInfo("    Modem Address: %s", modemAddress.c_str());
		LogInfo("    Modem Port: %hu", modemPort);
		LogInfo("    Local Address: %s", localAddress.c_str());
		LogInfo("    Local Port: %hu", localPort);
	}
	else if (protocol == "i2c")
	{
		LogInfo("    I2C Port: %s", i2cPort.c_str());
		LogInfo("    I2C Address: %02X", i2cAddress);
	}

	LogInfo("    RX Invert: %s", rxInvert ? "true" : "false");
	LogInfo("    TX Invert: %s", txInvert ? "true" : "false");
	LogInfo("    PTT Invert: %s", pttInvert ? "true" : "false");
	LogInfo("    TX Delay: %ums", txDelay);
	LogInfo("    RX Offset: %dHz", rxOffset);
	LogInfo("    TX Offset: %dHz", txOffset);
	LogInfo("    RX DC Offset: %d", rxDCOffset);
	LogInfo("    TX DC Offset: %d", txDCOffset);
	LogInfo("    RF Level: %.1f%%", rfLevel);
	LogInfo("    RX Level: %.1f%%", rxLevel);
	LogInfo("    CW Id TX Level: %.1f%%", cwIdTXLevel);
	LogInfo("    M17 TX Level: %.1f%%", m17TXLevel);
	LogInfo("    TX Frequency: %uHz (%uHz)", txFrequency, txFrequency + txOffset);
	LogInfo("    RX Frequency: %uHz (%uHz)", rxFrequency, rxFrequency + rxOffset);

	m_modem = std::make_unique<CModem>(m_duplex, rxInvert, txInvert, pttInvert, txDelay, trace, debug);
	std::unique_ptr<CBasePort> port;
	if (protocol == "uart")
		port = std::make_unique<CUARTController>(uartPort, uartSpeed, true);
	else if (protocol == "udp")
		port = std::make_unique<CUDPController>(modemAddress, modemPort, localAddress, localPort);
	else if (protocol == "i2c")
		port = std::make_unique<CI2CController>(i2cPort, i2cAddress);
	else if (protocol == "null")
		port = std::make_unique<CNullController>();
	else
		return true;

	m_modem->setPort(std::move(port));
	m_modem->setModeParams(false, false, false, false, false, true, false, false, false);
	m_modem->setLevels(rxLevel, cwIdTXLevel, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, m17TXLevel, 0.0f, 0.0f, 0.0f);
	m_modem->setRFParams(rxFrequency, rxOffset, txFrequency, txOffset, txDCOffset, rxDCOffset, rfLevel, 0);
	m_modem->setM17Params(m17TXHang);

	bool ret = m_modem->open();
	if (!ret) {
		m_modem.reset();
		m_modem = NULL;
		return true;
	}

	return false;
}

bool CM17Host::createM17Network()
{
	bool debug = g_Cfg.GetBoolean(g_Keys.repeater.section, g_Keys.repeater.debug);

	m_m17Network = std::make_shared<CM17Network>(debug);
	bool ret = m_m17Network->open();
	if (!ret) {
		m_m17Network.reset();
		return true;
	}

	m_m17Network->enable(true);

	return false;
}

void CM17Host::setMode(unsigned char mode)
{
	assert(m_modem != NULL);

	switch (mode) {
	case MODE_M17:
		if (m_m17Network != NULL)
			m_m17Network->enable(true);
		if (m_m17 != NULL)
			m_m17->enable(true);
		m_modem->setMode(MODE_M17);
		m_mode = MODE_M17;
		m_cwIdTimer.stop();
		LogMessage("Mode set to M17");
		break;

	case MODE_LOCKOUT:
		if (m_m17Network != NULL)
			m_m17Network->enable(false);
		if (m_m17 != NULL)
			m_m17->enable(false);
		m_modem->setMode(0);
		m_mode = MODE_LOCKOUT;
		m_cwIdTimer.stop();
		LogMessage("Mode set to Lockout");
		break;

	case MODE_ERROR:
		LogMessage("Mode set to Error");
		if (m_m17Network != NULL)
			m_m17Network->enable(false);
		if (m_m17 != NULL)
			m_m17->enable(false);
		m_mode = MODE_ERROR;
		m_cwIdTimer.stop();
		LogMessage("Mode set to Error");
		break;

	default: // for MODE_IDLE and MODE_QUIT
		if (m_m17Network != NULL)
			m_m17Network->enable(true);
		if (m_m17 != NULL)
			m_m17->enable(true);
		m_modem->setMode(0);
		if (m_mode == MODE_ERROR) {
			m_modem->sendCWId(m_callsign);
			m_cwIdTimer.setTimeout(m_cwIdTime);
			m_cwIdTimer.start();
		} else {
			m_cwIdTimer.setTimeout(m_cwIdTime / 4U);
			m_cwIdTimer.start();
		}
		m_mode = 0;
		LogMessage("Mode set to Idle");
		break;
	}
}
