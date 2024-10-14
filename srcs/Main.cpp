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

// global defs
CVersion g_Version(0, 0, 0);
CConfigure g_Cfg;
SJsonKeys g_Keys;
CM17Host host;

static int  m_signal = 0;
static bool m_reload = false;

static void sigHandler(int signum)
{
	host.Stop();
	m_signal = signum;
}

int main(int argc, char** argv)
{
	if (argc > 1) {
 		for (int currentArg = 1; currentArg < argc; ++currentArg)
		{
			std::string arg = argv[currentArg];
			if ((arg == "-v") || (arg == "--version"))
			{
				std::cout << "More version " << g_Version << " #" << std::string(gitversion, 7) << '\n';
				return EXIT_SUCCESS;
			} else if (arg.substr(0,1) == "-") {
				::fprintf(stderr, "Usage: more [-v|--version] [filename]\n");
				return EXIT_FAILURE;
			} else {
				if (g_Cfg.ReadData(arg))
					return EXIT_FAILURE;
			}
		}
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
