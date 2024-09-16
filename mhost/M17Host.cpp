/*
 *   Copyright (C) 2015-2021,2023,2024 by Jonathan Naylor G4KLX
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

#include "M17Host.h"
#include "RSSIInterpolator.h"
#include "NullController.h"
#include "UARTController.h"
#if defined(__linux__)
#include "I2CController.h"
#endif
#include "UDPController.h"
#include "Version.h"
#include "StopWatch.h"
#include "Defines.h"
#include "Log.h"
#include "GitVersion.h"

#include <cstdio>
#include <cstdlib>
#include <thread>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <pwd.h>


const char* DEFAULT_INI_FILE = "/etc/MMDVM.ini";

static bool m_killed = false;
static int  m_signal = 0;
static bool m_reload = false;

static void sigHandler(int signum)
{
	m_killed = true;
	m_signal = signum;
}

const char* HEADER1 = "This software is for use on amateur radio networks only,";
const char* HEADER2 = "it is to be used for educational purposes only. Its use on";
const char* HEADER3 = "commercial networks is strictly prohibited.";
const char* HEADER4 = "Copyright(C) 2015-2024 by Jonathan Naylor, G4KLX and others";

int main(int argc, char** argv)
{
	const char* iniFile = DEFAULT_INI_FILE;
	if (argc > 1) {
 		for (int currentArg = 1; currentArg < argc; ++currentArg) {
			std::string arg = argv[currentArg];
			if ((arg == "-v") || (arg == "--version")) {
				::fprintf(stdout, "M17Host version %s git #%.7s\n", VERSION, gitversion);
				return 0;
			} else if (arg.substr(0,1) == "-") {
				::fprintf(stderr, "Usage: M17Host [-v|--version] [filename]\n");
				return 1;
			} else {
				iniFile = argv[currentArg];
			}
		}
	}

	::signal(SIGINT,  sigHandler);
	::signal(SIGTERM, sigHandler);
	::signal(SIGHUP,  sigHandler);

	int ret = 0;

	do {
		m_signal = 0;
		m_killed = false;

		CM17Host* host = new CM17Host(std::string(iniFile));
		ret = host->run();

		delete host;

		switch (m_signal) {
			case 0:
				break;
			case 2:
				::LogInfo("M17Host-%s exited on receipt of SIGINT", VERSION);
				break;
			case 15:
				::LogInfo("M17Host-%s exited on receipt of SIGTERM", VERSION);
				break;
			case 1:
				::LogInfo("M17Host-%s is restarting on receipt of SIGHUP", VERSION);
				m_reload = true;
				break;
			default:
				::LogInfo("M17Host-%s exited on receipt of an unknown signal", VERSION);
				break;
		}
	} while (m_reload || (m_signal == 1));

	::LogFinalise();

	return ret;
}

CM17Host::CM17Host(const std::string& confFile) :
m_conf(confFile),
m_modem(NULL),
m_m17(NULL),
m_m17Network(NULL),
m_mode(MODE_IDLE),
m_m17RFModeHang(10U),
m_m17NetModeHang(3U),
m_modeTimer(1000U),
m_duplex(false),
m_timeout(180U),
m_callsign(),
m_fixedMode(false)
{
	CUDPSocket::startup();
}

CM17Host::~CM17Host()
{
	CUDPSocket::shutdown();
}

int CM17Host::run()
{
	bool ret = m_conf.read();
	if (!ret) {
		::fprintf(stderr, "M17Host: cannot read the .ini file\n");
		return 1;
	}

	bool m_daemon = m_conf.getDaemon();
	if (m_daemon) {
		// Create new process
		pid_t pid = ::fork();
		if (pid == -1) {
			::fprintf(stderr, "Couldn't fork() , exiting\n");
			return -1;
		} else if (pid != 0) {
			exit(EXIT_SUCCESS);
		}

		// Create new session and process group
		if (::setsid() == -1){
			::fprintf(stderr, "Couldn't setsid(), exiting\n");
			return -1;
		}

		// Set the working directory to the root directory
		if (::chdir("/") == -1){
			::fprintf(stderr, "Couldn't cd /, exiting\n");
			return -1;
		}

		// If we are currently root...
		if (getuid() == 0) {
			struct passwd* user = ::getpwnam("mmdvm");
			if (user == NULL) {
				::fprintf(stderr, "Could not get the mmdvm user, exiting\n");
				return -1;
			}

			uid_t mmdvm_uid = user->pw_uid;
			gid_t mmdvm_gid = user->pw_gid;

			// Set user and group ID's to mmdvm:mmdvm
			if (::setgid(mmdvm_gid) != 0) {
				::fprintf(stderr, "Could not set mmdvm GID, exiting\n");
				return -1;
			}

			if (::setuid(mmdvm_uid) != 0) {
				::fprintf(stderr, "Could not set mmdvm UID, exiting\n");
				return -1;
			}

			// Double check it worked (AKA Paranoia)
			if (::setuid(0) != -1){
				::fprintf(stderr, "It's possible to regain root - something is wrong!, exiting\n");
				return -1;
			}
		}
	}

	ret = ::LogInitialise(m_daemon, m_conf.getLogFilePath(), m_conf.getLogFileRoot(), m_conf.getLogFileLevel(), m_conf.getLogDisplayLevel(), m_conf.getLogFileRotate());
	if (!ret) {
		::fprintf(stderr, "M17Host: unable to open the log file\n");
		return 1;
	}

	if (m_daemon) {
		::close(STDIN_FILENO);
		::close(STDOUT_FILENO);
	}

	LogInfo(HEADER1);
	LogInfo(HEADER2);
	LogInfo(HEADER3);
	LogInfo(HEADER4);

	LogInfo("M17Host-%s is starting", VERSION);
	LogInfo("Built %s %s (GitID #%.7s)", __TIME__, __DATE__, gitversion);

	readParams();

	ret = createModem();
	if (!ret)
		return 1;

	if (!m_modem->hasM17()) {
		LogError("M17 enabled in the host but not in the modem firmware");
		return 1;
	}

	LogInfo("Opening network connections");

	ret = createM17Network();
	if (!ret)
		return 1;

	// For all modes we handle RSSI
	std::string rssiMappingFile = m_conf.getModemRSSIMappingFile();

	CRSSIInterpolator* rssi = new CRSSIInterpolator;
	if (!rssiMappingFile.empty()) {
		LogInfo("RSSI");
		LogInfo("    Mapping File: %s", rssiMappingFile.c_str());
		rssi->load(rssiMappingFile);
	}

	LogInfo("Starting protocol handlers");

	CStopWatch stopWatch;
	stopWatch.start();

	bool selfOnly          = m_conf.getM17SelfOnly();
	unsigned int can       = m_conf.getM17CAN();
	bool allowEncryption   = m_conf.getM17AllowEncryption();
	unsigned int txHang    = m_conf.getM17TXHang();
	m_m17RFModeHang        = m_conf.getM17ModeHang();

	LogInfo("M17 RF Parameters");
	LogInfo("    Self Only: %s", selfOnly ? "yes" : "no");
	LogInfo("    CAN: %u", can);
	LogInfo("    Allow Encryption: %s", allowEncryption ? "yes" : "no");
	LogInfo("    TX Hang: %us", txHang);
	LogInfo("    Mode Hang: %us", m_m17RFModeHang);

	m_m17 = new CM17Control(m_callsign, can, selfOnly, allowEncryption, m_m17Network, m_timeout, m_duplex, rssi);

	CTimer pocsagTimer(1000U, 30U);

	setMode(MODE_IDLE);

	LogInfo("M17Host-%s is running", VERSION);

	while (!m_killed) {
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

		if (ms < 5U)
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}

	setMode(MODE_QUIT);

	LogInfo("Closing network connections");

	if (m_m17Network != NULL) {
		m_m17Network->close();
		delete m_m17Network;
	}

	LogInfo("Stopping protocol handlers");

	delete m_m17;

	LogInfo("M17Host-%s has stopped", VERSION);

	m_modem->close();
	delete m_modem;

	return 0;
}

bool CM17Host::createModem()
{
	std::string protocol         = m_conf.getModemProtocol();
	std::string uartPort         = m_conf.getModemUARTPort();
	unsigned int uartSpeed       = m_conf.getModemUARTSpeed();
	std::string i2cPort          = m_conf.getModemI2CPort();
	unsigned int i2cAddress      = m_conf.getModemI2CAddress();
	std::string modemAddress     = m_conf.getModemModemAddress();
	unsigned short modemPort     = m_conf.getModemModemPort();
	std::string localAddress     = m_conf.getModemLocalAddress();
	unsigned short localPort     = m_conf.getModemLocalPort();
	bool rxInvert                = m_conf.getModemRXInvert();
	bool txInvert                = m_conf.getModemTXInvert();
	bool pttInvert               = m_conf.getModemPTTInvert();
	unsigned int txDelay         = m_conf.getModemTXDelay();
	float rxLevel                = m_conf.getModemRXLevel();
	float m17TXLevel             = m_conf.getModemM17TXLevel();
	bool trace                   = m_conf.getModemTrace();
	bool debug                   = m_conf.getModemDebug();
	unsigned int m17TXHang       = m_conf.getM17TXHang();
	unsigned int rxFrequency     = m_conf.getRXFrequency();
	unsigned int txFrequency     = m_conf.getTXFrequency();
	int rxOffset                 = m_conf.getModemRXOffset();
	int txOffset                 = m_conf.getModemTXOffset();
	int rxDCOffset               = m_conf.getModemRXDCOffset();
	int txDCOffset               = m_conf.getModemTXDCOffset();
	float rfLevel                = m_conf.getModemRFLevel();

	LogInfo("Modem Parameters");
	LogInfo("    Protocol: %s", protocol.c_str());

	if (protocol == "uart") {
		LogInfo("    UART Port: %s", uartPort.c_str());
		LogInfo("    UART Speed: %u", uartSpeed);
	} else if (protocol == "udp") {
		LogInfo("    Modem Address: %s", modemAddress.c_str());
		LogInfo("    Modem Port: %hu", modemPort);
		LogInfo("    Local Address: %s", localAddress.c_str());
		LogInfo("    Local Port: %hu", localPort);
	}
#if defined(__linux__)
	else if (protocol == "i2c") {
		LogInfo("    I2C Port: %s", i2cPort.c_str());
		LogInfo("    I2C Address: %02X", i2cAddress);
	}
#endif

	LogInfo("    RX Invert: %s", rxInvert ? "yes" : "no");
	LogInfo("    TX Invert: %s", txInvert ? "yes" : "no");
	LogInfo("    PTT Invert: %s", pttInvert ? "yes" : "no");
	LogInfo("    TX Delay: %ums", txDelay);
	LogInfo("    RX Offset: %dHz", rxOffset);
	LogInfo("    TX Offset: %dHz", txOffset);
	LogInfo("    RX DC Offset: %d", rxDCOffset);
	LogInfo("    TX DC Offset: %d", txDCOffset);
	LogInfo("    RF Level: %.1f%%", rfLevel);
	LogInfo("    RX Level: %.1f%%", rxLevel);
	LogInfo("    M17 TX Level: %.1f%%", m17TXLevel);
	LogInfo("    TX Frequency: %uHz (%uHz)", txFrequency, txFrequency + txOffset);

	m_modem = new CModem(m_duplex, rxInvert, txInvert, pttInvert, txDelay, trace, debug);

	IModemPort* port = NULL;
	if (protocol == "uart")
		port = new CUARTController(uartPort, uartSpeed, true);
	else if (protocol == "udp")
		port = new CUDPController(modemAddress, modemPort, localAddress, localPort);
#if defined(__linux__)
	else if (protocol == "i2c")
		port = new CI2CController(i2cPort, i2cAddress);
#endif
	else if (protocol == "null")
		port = new CNullController;
	else
		return false;

	m_modem->setPort(port);
	m_modem->setLevels(rxLevel, m17TXLevel);
	m_modem->setRFParams(rxFrequency, rxOffset, txFrequency, txOffset, txDCOffset, rxDCOffset, rfLevel);
	m_modem->setM17Params(m17TXHang);

	bool ret = m_modem->open();
	if (!ret) {
		delete m_modem;
		m_modem = NULL;
		return false;
	}

	return true;
}

bool CM17Host::createM17Network()
{
	std::string gatewayAddress = m_conf.getM17GatewayAddress();
	unsigned short gatewayPort = m_conf.getM17GatewayPort();
	std::string localAddress   = m_conf.getM17LocalAddress();
	unsigned short localPort   = m_conf.getM17LocalPort();
	m_m17NetModeHang           = m_conf.getM17NetworkModeHang();
	bool debug                 = m_conf.getM17NetworkDebug();

	LogInfo("M17 Network Parameters");
	LogInfo("    Gateway Address: %s", gatewayAddress.c_str());
	LogInfo("    Gateway Port: %hu", gatewayPort);
	LogInfo("    Local Address: %s", localAddress.c_str());
	LogInfo("    Local Port: %hu", localPort);
	LogInfo("    Mode Hang: %us", m_m17NetModeHang);

	m_m17Network = new CM17Network(localAddress, localPort, gatewayAddress, gatewayPort, debug);
	bool ret = m_m17Network->open();
	if (!ret) {
		delete m_m17Network;
		m_m17Network = NULL;
		return false;
	}

	m_m17Network->enable(true);

	return true;
}

void CM17Host::readParams()
{
	m_duplex        = m_conf.getDuplex();
	m_callsign      = m_conf.getCallsign();
	m_timeout       = m_conf.getTimeout();

	LogInfo("General Parameters");
	LogInfo("    Callsign: %s", m_callsign.c_str());
	LogInfo("    Duplex: %s", m_duplex ? "yes" : "no");
	LogInfo("    Timeout: %us", m_timeout);
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
		LogMessage("Mode set to Error");
		break;

	default:
		if (m_m17Network != NULL)
			m_m17Network->enable(true);
		if (m_m17 != NULL)
			m_m17->enable(true);
		m_modem->setMode(MODE_IDLE);
		m_mode = MODE_IDLE;
		m_modeTimer.stop();
		LogMessage("Mode set to Idle");
		break;
	}
}
