/*
	mspot - an M17 hot-spot using an  M17 CC1200 Raspberry Pi Hat
				Copyright (C) 2026 Thomas A. Early

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include <string>
#include <iostream>
#include <csignal>
#include <filesystem>
#include <thread>
#include <chrono>

#include <pwd.h>

#include "Configure.h"
#include "Version.h"
#include "Gateway.h"
#include "CC1200.h"
#include "CRC.h"

// global defs
CVersion   g_Version(1, 0, 0);
CConfigure g_Cfg;
CCRC       g_Crc;
CGateway   g_Gateway;

static int  caught_signal = 0;

static void sigHandler(int signum)
{
	caught_signal = signum;
	printf("Caught signal %d\n", signum);
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

	printf("\n\nThis software is for use on amateur radio networks only,\n");
	printf("Its use on commercial networks is strictly prohibited.\n");
	printf("Copyright (C) 2026 by Thomas A. Early, N7TAE\n");
	printf("Built %s %s\n", __TIME__, __DATE__);
	printf("%s-%s is starting  \n\n", pp.filename().c_str(), g_Version.c_str());

	g_Gateway.SetName(pp.filename());
	CCC1200 modem;
	
	do
	{
		caught_signal = 0;

		if (modem.Start())
			return EXIT_FAILURE;
		if (g_Gateway.Start())
		{
			modem.Stop();
			return EXIT_FAILURE;
		}
		
		pause();	// wait for a signal

		g_Gateway.Stop();
		modem.Stop();

		switch (caught_signal)
		{
			case 2:
				printf("%s exited on receipt of SIGINT\n", pp.filename().c_str());
				break;
			case 15:
				printf("%s exited on receipt of SIGTERM\n", pp.filename().c_str());
				break;
			case 1:
				printf("%s is restarting on receipt of SIGHUP", pp.filename().c_str());
				std::this_thread::sleep_for(std::chrono::seconds(5));
				break;
			default:
				printf("%s exited on receipt of an unknown signal\n", pp.filename().c_str());
				break;
		}
	} while (caught_signal == SIGHUP);

	return EXIT_SUCCESS;
}
