/*

         mspot - an M17-only HotSpot using an RPi CC1200 hat
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

#include <string>
#include <iostream>
#include <csignal>
#include <filesystem>
#include <thread>
#include <chrono>

#include <pwd.h>

#include "Version.h"
#include "Configure.h"
#include "Gateway.h"
#include "CC1200.h"
#include "Log.h"
#include "CRC.h"

// global defs
CVersion   g_Version(1, 0, 0);
CConfigure g_Cfg;
CCRC       g_Crc;
CGateway   g_Gate;

static int  caught_signal = 0;

static void sigHandler(int signum)
{
	caught_signal = signum;
	LogInfo("Caught signal %d", signum);
}

static void usage(const std::string &exename)
{
	std::cout << "Usage: " << exename << " [-v | --version | --help | inifilepath]" << std::endl;
}

int main(int argc, char** argv)
{
	std::signal(SIGINT,  sigHandler);
	std::signal(SIGTERM, sigHandler);
	std::signal(SIGHUP,  sigHandler);
	if (argc != 2)
	{
		usage(argv[0]);
		return EXIT_FAILURE;
	}
	const std::filesystem::path pp(argv[0]);
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

	if (g_Log.Open(g_Cfg.GetString(g_Keys.log.section, g_Keys.log.dashpath), g_Cfg.GetUnsigned(g_Keys.log.section, g_Keys.log.level)))
	{
		::fprintf(stderr, "ERROR: unable to open the dashboard log file\n");
		return EXIT_FAILURE;
	}

	LogInfo("This software is for use on amateur radio networks only,");
	LogInfo("Its use on commercial networks is strictly prohibited.");
	LogInfo("Copyright(C) 2024,2025 by Thomas A. Early, N7TAE");

	LogInfo("%s-%s is starting", pp.filename().c_str(), g_Version.c_str());
	LogInfo("Built %s %s", __TIME__, __DATE__);

	g_Gate.SetName(pp.filename());
	CCC1200 modem;
	
	do
	{
		caught_signal = 0;

		if (modem.Start())
			return EXIT_FAILURE;
		if (g_Gate.Start())
		{
			modem.Stop();
			return EXIT_FAILURE;
		}
		
		pause();	// wait for a signal

		modem.Stop();
		g_Gate.Stop();

		switch (caught_signal)
		{
			case 2:
				::LogInfo("%s exited on receipt of SIGINT", pp.filename().c_str());
				break;
			case 15:
				::LogInfo("%s exited on receipt of SIGTERM", pp.filename().c_str());
				break;
			case 1:
				::LogInfo("%s is restarting on receipt of SIGHUP", pp.filename().c_str());
				std::this_thread::sleep_for(std::chrono::seconds(5));
				break;
			default:
				::LogInfo("%s exited on receipt of an unknown signal", pp.filename().c_str());
				break;
		}
	} while (caught_signal == SIGHUP);

	g_Log.Close();

	return EXIT_SUCCESS;
}
