// Copyright (C) 2015-2021,2023,2024 by Jonathan Naylor G4KLX

/****************************************************************
 *                                                              *
 *             More - An M17-only Repeater/HotSpot              *
 *                                                              *
 *         Copyright (c) 2024 by Thomas A. Early N7TAE          *
 *                                                              *
 * See the LICENSE file for details about the software license. *
 *                                                              *
 ****************************************************************/

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
#include "RSSIInterpolator.h"
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
const char* HEADER5 = "Copyright(C) 2024 by Thomas A. Early, N7TAE";

CM17Host::CM17Host() :
m_mode(MODE_IDLE),
m_m17RFModeHang(10U),
m_m17NetModeHang(3U),
m_modeTimer(1000U),
m_cwIdTimer(1000U),
m_duplex(false),
m_timeout(180U),
m_cwIdTime(0U),
m_callsign(),
m_id(0U),
m_cwCallsign(),
m_fixedMode(false)
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

	m_duplex = g_Cfg.GetBoolean(g_Keys.reflector.section, g_Keys.reflector.isDuplex);
	m_callsign.assign(g_Cfg.GetString(g_Keys.reflector.section, g_Keys.reflector.callsign));
	m_timeout = g_Cfg.GetUnsigned(g_Keys.reflector.section, g_Keys.reflector.timeOut);
	const bool selfOnly = g_Cfg.GetBoolean(g_Keys.reflector.section, g_Keys.reflector.isprivate);
	const unsigned int can = g_Cfg.GetUnsigned(g_Keys.reflector.section, g_Keys.reflector.can);
	const bool allowEncryption = g_Cfg.GetBoolean(g_Keys.reflector.section, g_Keys.reflector.allowEncrypt);
	m_m17RFModeHang = g_Cfg.GetUnsigned(g_Keys.reflector.section, g_Keys.reflector.rfModeHang);
	m_m17NetModeHang = g_Cfg.GetUnsigned(g_Keys.reflector.section, g_Keys.reflector.netModeHang);

	LogInfo("Reflector Parameters");
	LogInfo("    Callsign: %s", m_callsign.c_str());
	LogInfo("    Duplex: %s", m_duplex ? "yes" : "no");
	LogInfo("    Timeout: %us", m_timeout);
	LogInfo("    Self Only: %s", selfOnly ? "true" : "false");
	LogInfo("    CAN: %u", can);
	LogInfo("    Allow Encryption: %s", allowEncryption ? "true" : "false");
	LogInfo("    RF Mode Hang: %us", m_m17RFModeHang);
	LogInfo("    Net Mode Hang: %us", m_m17NetModeHang);

	if (createModem())
		return true;

	if (not m_modem->hasM17()) {
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
	const std::string rssiMappingFile(g_Cfg.GetString(g_Keys.modem.section, g_Keys.modem.rssiMapFile));

	CRSSIInterpolator* rssi = new CRSSIInterpolator;
	if (!rssiMappingFile.empty()) {
		LogInfo("RSSI");
		LogInfo("    Mapping File: %s", rssiMappingFile.c_str());
		rssi->load(rssiMappingFile);
	}

	LogInfo("Starting protocol handlers");

	m_m17 = std::make_unique<CM17Control>(m_callsign, can, selfOnly, allowEncryption, m_m17Network, m_timeout, m_duplex, rssi);

	setMode(MODE_IDLE);

	LogInfo("M27Host-%s is running", g_Version.GetString());
	return false;
}

void CM17Host::Run()
{
	CStopWatch stopWatch;
	stopWatch.start();

	while (keep_running) {
		bool lockout = m_modem->hasLockout();

		if (lockout && m_mode != MODE_LOCKOUT)
			setMode(MODE_LOCKOUT);
		else if (!lockout && m_mode == MODE_LOCKOUT)
			setMode(MODE_IDLE);

		bool error = m_modem->hasError();
		if (error && m_mode != MODE_ERROR)
			setMode(MODE_ERROR);
		else if (!error && m_mode == MODE_ERROR)
			setMode(MODE_IDLE);

		unsigned char data[500U];
		unsigned int len;
		bool ret;

		len = m_modem->readM17Data(data);
		if (m_m17 != NULL && len > 0U) {
			if (m_mode == MODE_IDLE) {
				bool ret = m_m17->writeModem(data, len);
				if (ret) {
					m_modeTimer.setTimeout(m_m17RFModeHang);
					setMode(MODE_M17);
				}
			} else if (m_mode == MODE_M17) {
				bool ret = m_m17->writeModem(data, len);
				if (ret)
					m_modeTimer.start();
			} else if (m_mode != MODE_LOCKOUT) {
				LogWarning("M17 modem data received when in mode %u", m_mode);
			}
		}

		if (!m_fixedMode) {
			if (m_modeTimer.isRunning() && m_modeTimer.hasExpired())
				setMode(MODE_IDLE);
		}

		if (m_m17 != NULL) {
			ret = m_modem->hasM17Space();
			if (ret) {
				len = m_m17->readModem(data);
				if (len > 0U) {
					if (m_mode == MODE_IDLE) {
						m_modeTimer.setTimeout(m_m17NetModeHang);
						setMode(MODE_M17);
					}
					if (m_mode == MODE_M17) {
						m_modem->writeM17Data(data, len);
						m_modeTimer.start();
					} else if (m_mode != MODE_LOCKOUT) {
						LogWarning("M17 data received when in mode %u", m_mode);
					}
				}
			}
		}

		unsigned int ms = stopWatch.elapsed();
		stopWatch.start();

		m_modem->clock(ms);

		if (!m_fixedMode)
			m_modeTimer.clock(ms);

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
	setMode(MODE_QUIT);

	LogInfo("Closing network connections");

	if (m_m17Network != NULL) {
		m_m17Network->close();
		m_m17Network.reset();
	}

	LogInfo("Stopping protocol handlers");

	m_m17.release();

	LogInfo("M17Host-%s has stopped", g_Version.GetString());

	m_modem->close();
	m_modem.reset();
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
	bool debug = g_Cfg.GetBoolean(g_Keys.reflector.section, g_Keys.reflector.debug);

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
		m_modeTimer.start();
		m_cwIdTimer.stop();
		LogMessage("Mode set to M17");
		break;

	case MODE_LOCKOUT:
		if (m_m17Network != NULL)
			m_m17Network->enable(false);
		if (m_m17 != NULL)
			m_m17->enable(false);
		m_modem->setMode(MODE_IDLE);
		m_mode = MODE_LOCKOUT;
		m_modeTimer.stop();
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
		m_modeTimer.stop();
		m_cwIdTimer.stop();
		LogMessage("Mode set to Error");
		break;

	default:
		if (m_m17Network != NULL)
			m_m17Network->enable(true);
		if (m_m17 != NULL)
			m_m17->enable(true);
		m_modem->setMode(MODE_IDLE);
		if (m_mode == MODE_ERROR) {
			m_modem->sendCWId(m_callsign);
			m_cwIdTimer.setTimeout(m_cwIdTime);
			m_cwIdTimer.start();
		} else {
			m_cwIdTimer.setTimeout(m_cwIdTime / 4U);
			m_cwIdTimer.start();
		}
		m_mode = MODE_IDLE;
		m_modeTimer.stop();
		LogMessage("Mode set to Idle");
		break;
	}
}

void CM17Host::processModeCommand(unsigned char mode, unsigned int timeout)
{
	m_fixedMode = false;
	m_modeTimer.setTimeout(timeout);

	setMode(mode);
}

void CM17Host::processEnableCommand(bool& mode, bool enabled)
{
	LogDebug("Setting mode current=%s new=%s",mode ? "true" : "false",enabled ? "true" : "false");

	mode = enabled;

	m_modem->setModeParams(false, false, false, false, false, true, false, false, false);
	if (!m_modem->writeConfig())
		LogError("Cannot write Config to MMDVM");
}
