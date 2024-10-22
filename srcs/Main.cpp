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
#include <iostream>
#include <csignal>

#include "Version.h"
#include "GitVersion.h"
#include "Configure.h"
#include "JsonKeys.h"
#include "M17Host.h"
#include "Log.h"
#include "CRC.h"

// global defs
CVersion   g_Version(0, 0, 0);
CConfigure g_Cfg;
SJsonKeys  g_Keys;
CCRC       g_Crc;
CM17Host   host;

static int  m_signal = 0;
static bool m_reload = false;

static void sigHandler(int signum)
{
	host.Stop();
	m_signal = signum;
}

static void usage(const std::string &exename)
{
	std::cout << "Usage: " << exename << " [-v | --version | --help | inifilepath]" << std::endl;
}

int main(int argc, char** argv)
{
	if (argc == 2) {
		const std::string arg(argv[1]);
		if ((arg == "-v") || (arg == "--version"))
		{
			std::cout << argv[0] << " version " << g_Version << " #" << std::string(gitversion, 7) << std::endl;
			return EXIT_SUCCESS;
		}
		else if (arg == "--help")
		{
			usage(argv[0]);
			EXIT_SUCCESS;
		}
		else
		{
			if (g_Cfg.ReadData(arg))
				return EXIT_FAILURE;
		}
	}
	else
	{
		usage(argv[0]);
		EXIT_FAILURE;
	}

	std::signal(SIGINT,  sigHandler);
	std::signal(SIGTERM, sigHandler);
	std::signal(SIGHUP,  sigHandler);

	bool ret;

	do {
		m_signal = 0;
		host.Stop();

		auto host = std::make_unique<CM17Host>();
		ret = host->Run();

		host.reset();

		switch (m_signal) {
			case 0:
				break;
			case 2:
				::LogInfo("M17Host exited on receipt of SIGINT");
				break;
			case 15:
				::LogInfo("M17Host exited on receipt of SIGTERM");
				break;
			case 1:
				::LogInfo("M17Host is restarting on receipt of SIGHUP");
				m_reload = true;
				break;
			default:
				::LogInfo("M17Host exited on receipt of an unknown signal");
				break;
		}
	} while (m_reload || (m_signal == 1));

	::LogFinalise();

	return ret ? EXIT_FAILURE : EXIT_SUCCESS;
}
