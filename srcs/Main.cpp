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
#include "Configure.h"
#include "M17Host.h"
#include "Log.h"
#include "CRC.h"

// global defs
CVersion   g_Version(0, 0, 0);
CConfigure g_Cfg;
CCRC       g_Crc;
CM17Host   host;

static int  m_signal = 0;
static bool m_reload = false;

static void sigHandler(int signum)
{
	std::cout << "Caught signal #" << signum << std::endl;
	host.Stop();
	m_signal = signum;
}

static void usage(const std::string &exename)
{
	std::cout << "Usage: " << exename << " [-v | --version | --help | inifilepath]" << std::endl;
}

int main(int argc, char** argv)
{
	if (argc != 2)
	{
		usage(argv[0]);
		return EXIT_FAILURE;
	}
	const std::string arg(argv[1]);
	if (0 == arg.compare("-v") || 0 == arg.compare("--version"))
	{
		std::cout << argv[0] << " version " << g_Version << std::endl;
		return EXIT_SUCCESS;
	}
	if (0 == arg.compare("--help"))
	{
		usage(argv[0]);
		return EXIT_SUCCESS;
	}

	if (g_Cfg.ReadData(arg))
		return EXIT_FAILURE;

	std::signal(SIGINT,  sigHandler);
	std::signal(SIGTERM, sigHandler);
	std::signal(SIGHUP,  sigHandler);

	bool ret;

	std::unique_ptr<CM17Host> host;

	do
	{
		m_signal = 0;

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

	host.reset();

	::LogFinalise();

	return ret ? EXIT_FAILURE : EXIT_SUCCESS;
}
