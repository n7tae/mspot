//  Copyright (C) 2015-2021,2023,2024 by Jonathan Naylor G4KLX
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
#include <cstdlib>
#include <thread>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <pwd.h>
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
extern CVersion g_Version;

const char* HEADER1 = "This software is for use on amateur radio networks only,";
const char* HEADER2 = "it is to be used for educational purposes only. Its use on";
const char* HEADER3 = "commercial networks is strictly prohibited.";
const char* HEADER4 = "Copyright(C) 2015-2024 by Jonathan Naylor, G4KLX and others";
const char* HEADER5 = "Copyright(C) 2024 by Thomas A. Early, N7TAE";

CM17Host::CM17Host()
	: m_mode(MODE_M17)
	, m_m17NetModeHang(3U)
	, m_duplex(false)
	, m_timeout(180U)
	, m_callsign()
	, keep_running(false)
{
}

CM17Host::~CM17Host()
{
}

void CM17Host::Stop()
{
	keep_running = false;
	if (not keep_running) LogInfo("CM17Host::keep_running is false...");
}

bool CM17Host::Run()
{
	auto isDaemon = g_Cfg.GetBoolean(g_Keys.general.isdaemon);
	if (isDaemon) {
		// Create new process
		pid_t pid = ::fork();
		if (pid == -1)
		{
			::fprintf(stderr, "Couldn't fork() , exiting\n");
			return true;
		} else if (pid != 0)
		{
			exit(EXIT_SUCCESS);
		}

		// Create new session and process group
		if (::setsid() == -1)
		{
			::fprintf(stderr, "Couldn't setsid(), exiting\n");
			return true;
		}

		// Set the working directory to the root directory
		if (::chdir("/") == -1)
		{
			::fprintf(stderr, "Couldn't cd /, exiting\n");
			return true;
		}

		// If we are currently root...
		if (getuid() == 0)
		{
			struct passwd* user = ::getpwnam(g_Cfg.GetString(g_Keys.general.user).c_str());
			if (user == NULL)
			{
				::fprintf(stderr, "Could not get the mmdvm user, exiting\n");
				return true;
			}

			uid_t mmdvm_uid = user->pw_uid;
			gid_t mmdvm_gid = user->pw_gid;

			// Set user and group ID's to mmdvm:mmdvm
			if (::setgid(mmdvm_gid) != 0)
			{
				::fprintf(stderr, "Could not set mmdvm GID, exiting\n");
				return true;
			}

			if (::setuid(mmdvm_uid) != 0)
			{
				::fprintf(stderr, "Could not set mmdvm UID, exiting\n");
				return true;
			}

			// Double check it worked (AKA Paranoia)
			if (::setuid(0) != -1)
			{
				::fprintf(stderr, "It's possible to regain root - something is wrong!, exiting\n");
				return true;
			}
		}
	}

	auto ret = ::LogInitialise(isDaemon, g_Cfg.GetString(g_Keys.log.filePath), g_Cfg.GetString(g_Keys.log.fileName), g_Cfg.GetUnsigned(g_Keys.log.fileLevel), g_Cfg.GetUnsigned(g_Keys.log.displayLevel), g_Cfg.GetBoolean(g_Keys.log.rotate));
	if (!ret) {
		::fprintf(stderr, "M17Host: unable to open the log file\n");
		return true;
	}

	if (isDaemon) {
		::close(STDIN_FILENO);
		::close(STDOUT_FILENO);
	}

	LogInfo(HEADER1);
	LogInfo(HEADER2);
	LogInfo(HEADER3);
	LogInfo(HEADER4);
	LogInfo(HEADER5);

	LogInfo("mhost-%s is starting", g_Version.GetString());
	LogInfo("Built %s %s", __TIME__, __DATE__);

	readParams();

	ret = createModem();
	if (!ret)
		return true;

	if (!m_modem->hasM17())
	{
		LogError("M17 enabled in the host but not in the modem firmware");
		return true;
	}

	LogInfo("Opening network connections");

	ret = createM17Network();
	if (!ret)
		return true;

	// For all modes we handle RSSI
	std::string rssiMappingFile;
	if (g_Cfg.Contains(g_Keys.modem.rssiMapFile))
		rssiMappingFile.assign(g_Cfg.GetString(g_Keys.modem.rssiMapFile));

	auto rssi = std::make_unique<CRSSIInterpolator>();
	if (!rssiMappingFile.empty())
	{
		LogInfo("RSSI");
		LogInfo("    Mapping File: %s", rssiMappingFile.c_str());
		rssi->load(rssiMappingFile);
	}

	LogInfo("Starting protocol handlers");

	CStopWatch stopWatch;
	stopWatch.start();

	bool selfOnly          = g_Cfg.GetBoolean(g_Keys.general.isprivate);
	unsigned int can       = g_Cfg.GetUnsigned(g_Keys.general.can);
	bool allowEncryption   = g_Cfg.GetBoolean(g_Keys.general.allowEncrypt);

	LogInfo("M17 RF Parameters");
	LogInfo("    Self Only: %s", selfOnly ? "true" : "false");
	LogInfo("    CAN: %u", can);
	LogInfo("    Allow Encryption: %s", allowEncryption ? "true" : "false");

	m_m17 = std::make_unique<CM17Control>(m_callsign, can, selfOnly, allowEncryption, m_m17Network, m_timeout, m_duplex, rssi.get());

	CTimer pocsagTimer(1000U, 30U);

	setMode(MODE_M17);

	m_gateway = std::make_unique<CM17Gateway>();
	if (m_gateway->Start())
		return true;

	LogInfo("M17Host-%s is running", g_Version.GetString());

	keep_running = true;

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

		uint8_t data[500U];
		unsigned int len;
		bool ret;

		len = m_modem->readM17Data(data);
		if (m_m17 != NULL && len > 0U)
		{
			if (m_mode == MODE_IDLE)
			{
				bool ret = m_m17->writeModem(data, len);
				if (ret)
				{
					setMode(MODE_M17);
				}
			}
			else if (m_mode == MODE_M17)
			{
				m_m17->writeModem(data, len);
			}
			else if (m_mode != MODE_LOCKOUT)
			{
				LogWarning("M17 modem data received when in mode %u", m_mode);
			}
		}

		if (m_m17 != NULL)
		{
			ret = m_modem->hasM17Space();
			if (ret)
			{
				len = m_m17->readModem(data);
				if (len > 0U)
				{
					if (m_mode == MODE_IDLE)
					{
						setMode(MODE_M17);
					}
					if (m_mode == MODE_M17)
					{
						m_modem->writeM17Data(data, len);
					}
					else if (m_mode != MODE_LOCKOUT)
					{
						LogWarning("M17 data received when in mode %u", m_mode);
					}
				}
			}
		}

		unsigned int ms = stopWatch.elapsed();
		stopWatch.start();

		m_modem->clock(ms);

		if (m_m17)
			m_m17->clock(ms);
		if (m_m17Network)
			m_m17Network->clock(ms);

		if (ms < 5U)
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}

	m_gateway->Stop();
	m_gateway.reset();

	setMode(MODE_QUIT);

	LogInfo("Closing network connections");

	if (m_m17Network != NULL)
	{
		m_m17Network->close();
		m_m17Network.reset();
	}

	LogInfo("Stopping protocol handlers");

	m_m17.reset();

	LogInfo("M17Host-%s has stopped", g_Version.GetString());

	m_modem->close();
	m_modem.reset();
	rssi.reset();

	return false;
}

bool CM17Host::createModem()
{
	std::string protocol     = g_Cfg.GetString(g_Keys.modem.protocol);
	std::string uartPort     = g_Cfg.GetString(g_Keys.modem.uartPort);
	unsigned uartSpeed       = g_Cfg.GetUnsigned(g_Keys.modem.uartSpeed);
	std::string i2cPort      = g_Cfg.GetString(g_Keys.modem.i2cPort);
	unsigned i2cAddress      = g_Cfg.GetUnsigned(g_Keys.modem.i2cAddress);
	std::string modemAddress = g_Cfg.GetString(g_Keys.modem.modemAddress);
	unsigned modemPort       = g_Cfg.GetUnsigned(g_Keys.modem.modemPort);
	std::string localAddress = g_Cfg.GetString(g_Keys.modem.localAddress);
	unsigned localPort       = g_Cfg.GetUnsigned(g_Keys.modem.localPort);
	bool rxInvert            = g_Cfg.GetBoolean(g_Keys.modem.rxInvert);
	bool txInvert            = g_Cfg.GetBoolean(g_Keys.modem.txInvert);
	bool pttInvert           = g_Cfg.GetBoolean(g_Keys.modem.pttInvert);
	bool trace               = g_Cfg.GetBoolean(g_Keys.modem.trace);
	bool debug               = g_Cfg.GetBoolean(g_Keys.modem.debug);
	unsigned txDelay         = g_Cfg.GetUnsigned(g_Keys.modem.txDelay);
	unsigned rxLevel         = g_Cfg.GetUnsigned(g_Keys.modem.rxLevel);
	unsigned txLevel         = g_Cfg.GetUnsigned(g_Keys.modem.txLevel);
	unsigned rfLevel         = g_Cfg.GetUnsigned(g_Keys.modem.rfLevel);
	unsigned txHang          = g_Cfg.GetUnsigned(g_Keys.modem.txHang);
	unsigned rxFrequency     = g_Cfg.GetUnsigned(g_Keys.modem.rxFreq);
	unsigned txFrequency     = g_Cfg.GetUnsigned(g_Keys.modem.txFreq);
	int rxOffset             = g_Cfg.GetInt(g_Keys.modem.rxOffset);
	int txOffset             = g_Cfg.GetInt(g_Keys.modem.txOffset);
	int rxDCOffset           = g_Cfg.GetInt(g_Keys.modem.rxDCOffset);
	int txDCOffset           = g_Cfg.GetInt(g_Keys.modem.txDCOffset);

	LogInfo("Modem Parameters");
	LogInfo("    Protocol: %s", protocol.c_str());

	if (protocol == "uart")
	{
		LogInfo("    UART Port: %s", uartPort.c_str());
		LogInfo("    UART Speed: %u", uartSpeed);
	} else if (protocol == "udp")
	{
		LogInfo("    Modem Address: %s", modemAddress.c_str());
		LogInfo("    Modem Port: %u", modemPort);
		LogInfo("    Local Address: %s", localAddress.c_str());
		LogInfo("    Local Port: %u", localPort);
	}
#if defined(__linux__)
	else if (protocol == "i2c")
	{
		LogInfo("    I2C Port: %s", i2cPort.c_str());
		LogInfo("    I2C Address: 0x%02x", i2cAddress);
	}
#endif

	LogInfo("    RX Invert: %s", rxInvert ? "true" : "false");
	LogInfo("    TX Invert: %s", txInvert ? "true" : "false");
	LogInfo("    PTT Invert: %s", pttInvert ? "true" : "false");
	LogInfo("    TX Delay: %u ms", txDelay);
	LogInfo("    RX Offset: %d Hz", rxOffset);
	LogInfo("    TX Offset: %d Hz", txOffset);
	LogInfo("    RX DC Offset: %d", rxDCOffset);
	LogInfo("    TX DC Offset: %d", txDCOffset);
	LogInfo("    RF Level: %u", rfLevel);
	LogInfo("    RX Level: %u", rxLevel);
	LogInfo("    TX Level: %u", txLevel);
	LogInfo("    RX Frequency: %u Hz (%u Hz)", rxFrequency, unsigned(rxFrequency + rxOffset));
	LogInfo("    TX Frequency: %u Hz (%u Hz)", txFrequency, unsigned(txFrequency + txOffset));

	m_modem = std::make_unique<CModem>(m_duplex, rxInvert, txInvert, pttInvert, txDelay, trace, debug);

	std::unique_ptr<ISerialPort> port;
	if (protocol == "uart")
		port = std::make_unique<CUARTController>(uartPort, uartSpeed, true);
	else if (protocol == "udp")
		port = std::make_unique<CUDPController>(modemAddress, modemPort, localAddress, localPort);
#if defined(__linux__)
	else if (protocol == "i2c")
		port = std::make_unique<CI2CController>(i2cPort, i2cAddress);
#endif
	else if (protocol == "null")
		port = std::make_unique<CNullController>();
	else
		return false;

	m_modem->setPort(std::move(port));
	m_modem->setLevels(rxLevel, txLevel);
	m_modem->setRFParams(rxFrequency, rxOffset, txFrequency, txOffset, txDCOffset, rxDCOffset, rfLevel);
	m_modem->setM17Params(txHang);

	bool ret = m_modem->open();
	if (!ret)
	{
		m_modem.reset();
		return false;
	}

	return true;
}

bool CM17Host::createM17Network()
{
	bool debug = g_Cfg.GetBoolean(g_Keys.modem.debug);

	LogInfo("M17 Network Parameters");
	LogInfo("    Mode Hang: %us", m_m17NetModeHang);

	m_m17Network = std::make_shared<CM17Network>(debug);
	bool ret = m_m17Network->open();
	if (!ret) {
		m_m17Network.reset();
		return false;
	}

	m_m17Network->enable(true);

	return true;
}

void CM17Host::readParams()
{
	m_duplex   = g_Cfg.GetBoolean(g_Keys.modem.isDuplex);
	m_callsign = g_Cfg.GetString(g_Keys.general.callsign);

	LogInfo("General Parameters");
	LogInfo("    Callsign: %s", m_callsign.c_str());
	LogInfo("    Duplex: %s", m_duplex ? "true" : "false");
	LogInfo("    Timeout: %us", m_timeout);
}

void CM17Host::setMode(uint8_t mode)
{
	assert(m_modem != NULL);

	switch (mode)
	{
	default:
	case MODE_M17:
		if (m_m17Network != NULL)
			m_m17Network->enable(true);
		if (m_m17 != NULL)
			m_m17->enable(true);
		m_modem->setMode(MODE_M17);
		m_mode = MODE_M17;
		LogMessage("Mode set to M17");
		break;
	case MODE_LOCKOUT:
		if (m_m17Network != NULL)
			m_m17Network->enable(false);
		if (m_m17 != NULL)
			m_m17->enable(false);
		m_modem->setMode(MODE_IDLE);
		m_mode = MODE_LOCKOUT;
		LogMessage("Mode set to Lockout");
		break;

	case MODE_ERROR:
		LogMessage("Mode set to Error");
		if (m_m17Network != NULL)
			m_m17Network->enable(false);
		if (m_m17 != NULL)
			m_m17->enable(false);
		m_mode = MODE_ERROR;
		LogMessage("Mode set to Error");
		break;
	}
}
