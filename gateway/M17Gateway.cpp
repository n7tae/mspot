/*
*   Copyright (C) 2016,2017,2018,2020,2021,2024 by Jonathan Naylor G4KLX
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

#include "M17Gateway.h"
#include "RptNetwork.h"
#include "Reflectors.h"
#include "StopWatch.h"
#include "M17Utils.h"
#include "Version.h"
#include "M17LSF.h"
#include "Timer.h"
#include "Voice.h"
#include "Utils.h"
#include "Echo.h"
#include "Log.h"
#include "../GitVersion.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <pwd.h>
#include <thread>


const char* DEFAULT_INI_FILE = "/etc/M17Gateway.ini";

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <ctime>
#include <cstring>

static bool m_killed = false;
static int  m_signal = 0;

static void sigHandler(int signum)
{
	m_killed = true;
	m_signal = signum;
}

int main(int argc, char** argv)
{
	const char* iniFile = DEFAULT_INI_FILE;
	if (argc > 1) {
		for (int currentArg = 1; currentArg < argc; ++currentArg) {
			std::string arg = argv[currentArg];
			if ((arg == "-v") || (arg == "--version")) {
				::fprintf(stdout, "M17Gateway version %s git #%.7s\n", VERSION, gitversion);
				return 0;
			} else if (arg.substr(0, 1) == "-") {
				::fprintf(stderr, "Usage: M17Gateway [-v|--version] [filename]\n");
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

		CM17Gateway* gateway = new CM17Gateway(std::string(iniFile));
		ret = gateway->run();

		delete gateway;

		switch (m_signal) {
			case 0:
				break;
			case 2:
				::LogInfo("M17Gateway-%s exited on receipt of SIGINT", VERSION);
				break;
			case 15:
				::LogInfo("M17Gateway-%s exited on receipt of SIGTERM", VERSION);
				break;
			case 1:
				::LogInfo("M17Gateway-%s is restarting on receipt of SIGHUP", VERSION);
				break;
			default:
				::LogInfo("M17Gateway-%s exited on receipt of an unknown signal", VERSION);
				break;
		}
	} while (m_signal == 1);

	::LogFinalise();

	return ret;
}

CM17Gateway::CM17Gateway(const std::string& file) :
m_conf(file),
m_status(M17S_NOTLINKED),
m_oldStatus(M17S_NOTLINKED),
m_network(NULL),
m_timer(1000U, 5U),
m_reflector(),
m_addrLen(0U),
m_addr(),
m_module(' '),
m_writer(NULL),
m_gps(NULL)
{
	CUDPSocket::startup();
}

CM17Gateway::~CM17Gateway()
{
	CUDPSocket::shutdown();
}

int CM17Gateway::run()
{
	bool ret = m_conf.read();
	if (!ret) {
		::fprintf(stderr, "M17Gateway: cannot read the .ini file\n");
		return 1;
	}

	bool m_daemon = m_conf.getDaemon();
	if (m_daemon) {
		// Create new process
		pid_t pid = ::fork();
		if (pid == -1) {
			::fprintf(stderr, "Couldn't fork() , exiting\n");
			return 1;
		} else if (pid != 0) {
			exit(EXIT_SUCCESS);
		}

		// Create new session and process group
		if (::setsid() == -1) {
			::fprintf(stderr, "Couldn't setsid(), exiting\n");
			return 1;
		}

		// Set the working directory to the root directory
		if (::chdir("/") == -1) {
			::fprintf(stderr, "Couldn't cd /, exiting\n");
			return 1;
		}

		// If we are currently root...
		if (getuid() == 0) {
			struct passwd* user = ::getpwnam("mmdvm");
			if (user == NULL) {
				::fprintf(stderr, "Could not get the mmdvm user, exiting\n");
				return 1;
			}

			uid_t mmdvm_uid = user->pw_uid;
			gid_t mmdvm_gid = user->pw_gid;

			// Set user and group ID's to mmdvm:mmdvm
			if (setgid(mmdvm_gid) != 0) {
				::fprintf(stderr, "Could not set mmdvm GID, exiting\n");
				return 1;
			}

			if (setuid(mmdvm_uid) != 0) {
				::fprintf(stderr, "Could not set mmdvm UID, exiting\n");
				return 1;
			}

			// Double check it worked (AKA Paranoia)
			if (setuid(0) != -1) {
				::fprintf(stderr, "It's possible to regain root - something is wrong!, exiting\n");
				return 1;
			}
		}
	}

    ret = ::LogInitialise(m_daemon, m_conf.getLogFilePath(), m_conf.getLogFileRoot(), m_conf.getLogFileLevel(), m_conf.getLogDisplayLevel(), m_conf.getLogFileRotate());

	if (!ret) {
		::fprintf(stderr, "M17Gateway: unable to open the log file\n");
		return -1;
	}

	if (m_daemon) {
		::close(STDIN_FILENO);
		::close(STDOUT_FILENO);
		::close(STDERR_FILENO);
	}

	createGPS();

	CRptNetwork* localNetwork = new CRptNetwork(m_conf.getMyPort(), m_conf.getRptAddress(), m_conf.getRptPort(), m_conf.getDebug());
	ret = localNetwork->open();
	if (!ret)
		return 1;

	m_network = new CM17Network(m_conf.getCallsign(), m_conf.getSuffix(), m_conf.getNetworkLocalPort(), m_conf.getNetworkDebug());

	CUDPSocket* remoteSocket = NULL;
	if (m_conf.getRemoteCommandsEnabled()) {
		remoteSocket = new CUDPSocket(m_conf.getRemoteCommandsPort());
		ret = remoteSocket->open();
		if (!ret) {
			delete remoteSocket;
			remoteSocket = NULL;
		}
	}

	CReflectors reflectors(m_conf.getNetworkHosts1(), m_conf.getNetworkHosts2(), m_conf.getNetworkReloadTime());
	reflectors.load();

	bool triggerVoice = false;
	CVoice* voice = NULL;
	if (m_conf.getVoiceEnabled()) {
		voice = new CVoice(m_conf.getVoiceDirectory(), m_conf.getVoiceLanguage(), m_conf.getCallsign());
		bool ok = voice->open();
		if (!ok) {
			delete voice;
			voice = NULL;
		}
	}

	CEcho echo(240U);

	CTimer hangTimer(1000U, m_conf.getNetworkHangTime());

	CStopWatch stopWatch;
	stopWatch.start();

	LogMessage("M17Gateway-%s is starting", VERSION);
	LogMessage("Built %s %s (GitID #%.7s)", __TIME__, __DATE__, gitversion);

	std::string startupReflector = m_conf.getNetworkStartup();
	bool revert = m_conf.getNetworkRevert();

	if (voice != NULL)
		voice->unlinked();

	if (!startupReflector.empty()) {
		CM17Reflector* refl = reflectors.find(startupReflector);
		if (refl != NULL) {
			char module = startupReflector.at(M17_CALLSIGN_LENGTH - 1U);
			if (module >= 'A' && module <= 'Z') {
				m_reflector = startupReflector;
				m_addr      = refl->m_addr;
				m_addrLen   = refl->m_addrLen;
				m_module    = module;

				m_status = m_oldStatus = M17S_LINKING;

				LogInfo("Linked at startup to %s", m_reflector.c_str());

				if (voice != NULL)
					voice->linkedTo(m_reflector);

				m_timer.start();
			}
		} else {
			startupReflector.clear();
		}
	}

	unsigned int n = 0U;

	while (!m_killed) {
		unsigned char buffer[100U];

		if (m_status == M17S_LINKED) {
			// From the reflector to the MMDVM
			bool ret = m_network->read(buffer);
			if (ret) {
				CM17LSF lsf;
				lsf.setNetwork(buffer + 6U);

				if (n > 40U) {
					// Change the type to show that it's callsign data
					lsf.setEncryptionType(M17_ENCRYPTION_TYPE_NONE);
					lsf.setEncryptionSubType(M17_ENCRYPTION_SUB_TYPE_CALLSIGNS);

					// Copy the encoded source and the reflector into the META field
					unsigned char meta[M17_META_LENGTH_BYTES];
					::memset(meta, 0x00U, M17_META_LENGTH_BYTES);
					::memcpy(meta + 0U, buffer + 12U, 6U);
					CM17Utils::encodeCallsign(m_reflector, meta + 6U);
					lsf.setMeta(meta);

					if (n > 45U)
						n = 0U;
				}

				n++;

				// Replace the destination callsign with the broadcast callsign
				lsf.setDest("ALL");
				lsf.getNetwork(buffer + 6U);

				localNetwork->write(buffer);

				uint16_t fn = (buffer[34U] << 8) + (buffer[35U] << 0);
				if ((fn & 0x8000U) == 0x8000U)
					n = 0U;

				hangTimer.start();
			}
		} else if (m_status == M17S_ECHO) {
			// From the echo unit to the MMDVM
			ECHO_STATE est = echo.read(buffer);
			switch (est) {
				case EST_DATA:
					if (n > 40U) {
						CM17LSF lsf;
						lsf.setNetwork(buffer + 6U);

						// Change the type to show that it's callsign data
						lsf.setEncryptionType(M17_ENCRYPTION_TYPE_NONE);
						lsf.setEncryptionSubType(M17_ENCRYPTION_SUB_TYPE_CALLSIGNS);

						// Copy the encoded source into the META field
						unsigned char meta[M17_META_LENGTH_BYTES];
						::memset(meta, 0x00U, M17_META_LENGTH_BYTES);
						::memcpy(meta + 0U, buffer + 12U, 6U);
						lsf.setMeta(meta);

						lsf.getNetwork(buffer + 6U);

						if (n > 45U)
							n = 0U;
					}

					n++;

					localNetwork->write(buffer);

					hangTimer.start();
					break;

				case EST_EOF:
					// End of the message, restore the original status
					m_status = m_oldStatus;
					n = 0U;
					break;

				default:
					break;
			}
		}

		// From the MMDVM to the reflector or control data
		bool ret = localNetwork->read(buffer);
		if (ret) {
			CM17LSF lsf;
			lsf.setNetwork(buffer + 6U);

			std::string src = lsf.getSource();
			std::string dst = lsf.getDest();

			if (m_gps != NULL)
				m_gps->process(lsf);

			if (dst == "ECHO") {
				if (m_status != M17S_ECHO) {
					m_oldStatus = m_status;
					echo.clear();
				}

				echo.write(buffer);
				m_status = M17S_ECHO;
				hangTimer.start();

				uint16_t fn = (buffer[34U] << 8) + (buffer[35U] << 0);
				if ((fn & 0x8000U) == 0x8000U)
					echo.end();
			} else if (dst == "INFO") {
				hangTimer.start();
				triggerVoice = true;
			} else if (dst == "UNLINK") {
				if (m_status == M17S_LINKED || m_status == M17S_LINKING) {
					LogMessage("Unlinking from reflector %s triggered by %s", m_reflector.c_str(), src.c_str());
					m_network->stop();
					m_network->unlink();

					if (voice != NULL)
						voice->unlinked();

					m_status = m_oldStatus = M17S_UNLINKING;
					m_timer.start();
				}

				triggerVoice = true;
				hangTimer.stop();
			} else if (dst.size() == M17_CALLSIGN_LENGTH) {
				std::string reflector = dst;
				char module = reflector.at(M17_CALLSIGN_LENGTH - 1U);

				if (reflector != m_reflector && module >= 'A' && module <= 'Z') {
					if (m_status == M17S_LINKED || m_status == M17S_LINKING) {
						LogMessage("Unlinking from reflector %s triggered by %s", m_reflector.c_str(), src.c_str());
						m_network->stop();
						m_network->unlink();

						m_timer.start();
					}

					triggerVoice = true;

					CM17Reflector* refl = reflectors.find(reflector);
					if (refl != NULL) {
						m_reflector = reflector;
						m_addr      = refl->m_addr;
						m_addrLen   = refl->m_addrLen;
						m_module    = module;

						// Link to the new reflector
						LogMessage("Linking to reflector %s triggered by %s", m_reflector.c_str(), src.c_str());

						m_status = m_oldStatus = M17S_LINKING;

						if (voice != NULL)
							voice->linkedTo(m_reflector);

						hangTimer.start();
						m_timer.start();
					} else {
						if (m_status == M17S_LINKED || m_status == M17S_LINKING)
							m_status = m_oldStatus = M17S_UNLINKING;

						if (voice != NULL)
							voice->unlinked();

						hangTimer.stop();
					}
				}
			}

			if (m_status == M17S_LINKED) {
				// If the link has failed, try and relink
				M17NET_STATUS netStatus = m_network->getStatus();
				if (netStatus == M17N_FAILED) {
					LogMessage("Relinking to reflector %s", m_reflector.c_str());
					m_status = M17S_LINKING;
				}

				// If we're linked and we have a network, send it on
				if (m_status == M17S_LINKED) {
					// Replace the destination callsign with the reflector name and module
					CM17Utils::encodeCallsign(m_reflector, buffer + 6U);
					m_network->write(buffer);
					hangTimer.start();
				}
			}
		}

		if (voice != NULL) {
			if (triggerVoice) {
				uint16_t fn = (buffer[34U] << 8) + (buffer[35U] << 0);
				if ((fn & 0x8000U) == 0x8000U) {
					voice->eof();
					triggerVoice = false;
				}
			}

			ret = voice->read(buffer);
			if (ret)
				localNetwork->write(buffer);
		}

		if (remoteSocket != NULL) {
			sockaddr_storage addr;
			unsigned int addrLen;
			int res = remoteSocket->read(buffer, 200U, addr, addrLen);
			if (res > 0) {
				buffer[res] = '\0';
				if (::memcmp(buffer + 0U, "Reflector", 9U) == 0) {
					std::string reflector = ((strlen((char*)buffer + 0U) > 10) ? std::string((char*)(buffer + 10U)) : "");
					std::replace(reflector.begin(), reflector.end(), '_', ' ');
					reflector.resize(M17_CALLSIGN_LENGTH, ' ');

					if (reflector != m_reflector) {
						if (m_status == M17S_LINKED || m_status == M17S_LINKING) {
							LogMessage("Unlinked from reflector %s by remote command", m_reflector.c_str());

							m_network->stop();
							m_network->unlink();

							hangTimer.stop();
							m_timer.start();
						}

						CM17Reflector* refl = reflectors.find(reflector);
						if (refl != NULL) {
							char module = reflector.at(M17_CALLSIGN_LENGTH - 1U);
							if (module >= 'A' && module <= 'Z') {
								m_reflector = reflector;
								m_addr      = refl->m_addr;
								m_addrLen   = refl->m_addrLen;
								m_module    = module;

								// Link to the new reflector
								LogMessage("Switched to reflector %s by remote command", m_reflector.c_str());

								m_status = m_oldStatus = M17S_LINKING;

								if (voice != NULL) {
									voice->linkedTo(m_reflector);
									voice->eof();
								}

								hangTimer.start();
								m_timer.start();
							}
						} else {
							m_reflector.clear();
							if (m_status == M17S_LINKED || m_status == M17S_LINKING) {
								m_status = m_oldStatus = M17S_UNLINKING;

								if (voice != NULL) {
									voice->unlinked();
									voice->eof();
								}
							}

							hangTimer.stop();
						}
					}
				} else if (::memcmp(buffer + 0U, "status", 6U) == 0) {
					std::string state = std::string("m17:") + ((m_network == NULL) ? "n/a" : ((m_network->getStatus() == M17N_LINKED) ? "conn" : "disc"));
					remoteSocket->write((unsigned char*)state.c_str(), (unsigned int)state.length(), addr, addrLen);
				} else if (::memcmp(buffer + 0U, "host", 4U) == 0) {
					std::string ref(m_reflector);
					std::replace(ref.begin(), ref.end(), ' ', '_');
					std::string host = std::string("m17:\"") + (((m_network == NULL) || (ref.length() == 0)) ? "NONE" : ref) + "\"";
					remoteSocket->write((unsigned char*)host.c_str(), (unsigned int)host.length(), addr, addrLen);
				} else {
					CUtils::dump("Invalid remote command received", buffer, res);
				}
			}
		}

		unsigned int ms = stopWatch.elapsed();
		stopWatch.start();

		if (voice != NULL)
			voice->clock(ms);

		if (m_writer != NULL)
			m_writer->clock(ms);

		m_timer.clock(ms);
		linking();
		unlinking();

		reflectors.clock(ms);

		localNetwork->clock(ms);

		m_network->clock(ms);

		echo.clock(ms);

		hangTimer.clock(ms);
		if (hangTimer.isRunning() && hangTimer.hasExpired()) {
			if (revert && !startupReflector.empty() && m_reflector != startupReflector) {
				if (m_status == M17S_LINKED || m_status == M17S_LINKING) {
					m_network->stop();
					m_network->unlink();
				}

				LogMessage("Relinked from %s to %s due to inactivity", m_reflector.c_str(), startupReflector.c_str());

				CM17Reflector* refl = reflectors.find(startupReflector);
				m_reflector = startupReflector;
				m_addr      = refl->m_addr;
				m_addrLen   = refl->m_addrLen;
				m_module    = startupReflector.at(M17_CALLSIGN_LENGTH - 1U);

				m_status = m_oldStatus = M17S_LINKING;

				if (voice != NULL) {
					voice->linkedTo(startupReflector);
					voice->eof();
				}

				hangTimer.start();
				m_timer.start();
			} else if (revert && startupReflector.empty() && (m_status == M17S_LINKED || m_status == M17S_LINKING)) {
				LogMessage("Unlinking from %s due to inactivity", m_reflector.c_str());

				m_network->stop();
				m_network->unlink();

				m_status = m_oldStatus = M17S_UNLINKING;

				if (voice != NULL) {
					voice->unlinked();
					voice->eof();
				}

				m_reflector.clear();

				hangTimer.stop();
				m_timer.start();
			}
		}

		if (ms < 5U)
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}

	delete voice;

	localNetwork->close();
	delete localNetwork;

	if (remoteSocket != NULL) {
		remoteSocket->close();
		delete remoteSocket;
	}

	if (m_status == M17S_LINKED || m_status == M17S_LINKING)
		m_network->unlink();
	m_network->close();
	delete m_network;

	if (m_gps != NULL) {
		m_writer->close();
		delete m_writer;
		delete m_gps;
	}

	return 0;
}

void CM17Gateway::linking()
{
	if (m_status != M17S_LINKING)
		return;

	M17NET_STATUS status = m_network->getStatus();
	if (status == M17N_NOTLINKED) {
		m_timer.start();
		m_network->link(m_reflector, m_addr, m_addrLen, m_module);
	} else if (status == M17N_LINKED) {
		m_timer.stop();
		m_status = m_oldStatus = M17S_LINKED;
	} else if (status == M17N_REJECTED) {
		m_timer.stop();
		m_status = m_oldStatus = M17S_NOTLINKED;
	} else if (status == M17N_LINKING) {
		if (m_timer.isRunning() && m_timer.hasExpired()) {
			m_network->stop();
			m_timer.stop();
			m_status = m_oldStatus = M17S_NOTLINKED;
			m_reflector.clear();
		}
	} else if (status == M17N_UNLINKING) {
		if (m_timer.isRunning() && m_timer.hasExpired()) {
			m_network->stop();
			m_timer.start();
			m_network->link(m_reflector, m_addr, m_addrLen, m_module);
		}
	}
}

void CM17Gateway::unlinking()
{
	if (m_status != M17S_UNLINKING)
		return;

	M17NET_STATUS status = m_network->getStatus();
	if (status == M17N_NOTLINKED) {
		m_timer.stop();
		m_status = m_oldStatus = M17S_NOTLINKED;
		m_reflector.clear();
	} else if (status == M17N_UNLINKING) {
		if (m_timer.isRunning() && m_timer.hasExpired()) {
			m_network->stop();
			m_timer.stop();
			m_status = m_oldStatus = M17S_NOTLINKED;
			m_reflector.clear();
		}
	}
}

void CM17Gateway::createGPS()
{
	if (!m_conf.getAPRSEnabled())
		return;

	std::string callsign  = m_conf.getCallsign();
	std::string rptSuffix = m_conf.getSuffix();
	std::string address   = m_conf.getAPRSAddress();
	unsigned int port     = m_conf.getAPRSPort();
	std::string suffix    = m_conf.getAPRSSuffix();
	bool debug            = m_conf.getDebug();

	m_writer = new CAPRSWriter(callsign, rptSuffix, address, port, debug);

	unsigned int txFrequency = m_conf.getTxFrequency();
	unsigned int rxFrequency = m_conf.getRxFrequency();
	std::string desc         = m_conf.getAPRSDescription();
	std::string symbol  	 = m_conf.getAPRSSymbol();

	m_writer->setInfo(txFrequency, rxFrequency, desc, symbol);

	// bool enabled = m_conf.getGPSDEnabled();
	// if (enabled) {
	//        std::string address = m_conf.getGPSDAddress();
	//        std::string port    = m_conf.getGPSDPort();
	//
	//        m_writer->setGPSDLocation(address, port);
	// } else {
	        float latitude  = m_conf.getLatitude();
                float longitude = m_conf.getLongitude();
                int height      = m_conf.getHeight();

                m_writer->setStaticLocation(latitude, longitude, height);
	// }

	bool ret = m_writer->open();
	if (!ret) {
		delete m_writer;
		m_writer = NULL;
		return;
	}

	m_gps = new CGPSHandler(callsign, rptSuffix, m_writer);
}
