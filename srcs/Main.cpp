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

#include <string>
#include <iostream>
#include <csignal>

#include <pwd.h>

#include "Version.h"
#include "Configure.h"
#include "M17Host.h"
#include "Log.h"
#include "CRC.h"

// global defs
CVersion   g_Version(0, 1, 2);
CConfigure g_Cfg;
CCRC       g_Crc;

static int  caught_signal = 0;

static void sigHandler(int signum)
{
	std::cout << "Caught signal #" << signum << std::endl;
	caught_signal = signum;
}

static bool daemonize()
{
	// Create new process
	pid_t pid = fork();
	if (pid == -1)
	{
		fprintf(stderr, "Couldn't fork() , exiting\n");
		return true;
	} else if (pid != 0)
	{
		exit(EXIT_SUCCESS);
	}

	// Create new session and process group
	if (setsid() == -1)
	{
		fprintf(stderr, "Couldn't setsid(), exiting\n");
		return true;
	}

	// Set the working directory to the root directory
	if (chdir("/") == -1)
	{
		fprintf(stderr, "Couldn't cd /, exiting\n");
		return true;
	}

	// If we are currently root...
	if (getuid() == 0)
	{
		struct passwd* user = ::getpwnam(g_Cfg.GetString(g_Keys.repeater.section, g_Keys.repeater.user).c_str());
		if (user == NULL)
		{
			fprintf(stderr, "Could not get the mmdvm user, exiting\n");
			return true;
		}

		uid_t mmdvm_uid = user->pw_uid;
		gid_t mmdvm_gid = user->pw_gid;

		// Set user and group ID's to mmdvm:mmdvm
		if (setgid(mmdvm_gid) != 0)
		{
			fprintf(stderr, "Could not set mmdvm GID, exiting\n");
			return true;
		}

		if (setuid(mmdvm_uid) != 0)
		{
			fprintf(stderr, "Could not set mmdvm UID, exiting\n");
			return true;
		}

		// Double check it worked (AKA Paranoia)
		if (setuid(0) != -1)
		{
			fprintf(stderr, "It's possible to regain root - something is wrong!, exiting\n");
			return true;
		}
	}
	close(STDIN_FILENO);
	close(STDOUT_FILENO);
	return false;
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

	auto isDaemon = g_Cfg.GetBoolean(g_Keys.repeater.section, g_Keys.repeater.isDaemon);

	auto ret = g_Log.Open(isDaemon,
		g_Cfg.GetString(g_Keys.log.section, g_Keys.log.filePath),
		g_Cfg.GetString(g_Keys.log.section, g_Keys.log.fileName),
		g_Cfg.GetUnsigned(g_Keys.log.section, g_Keys.log.fileLevel),
		g_Cfg.GetUnsigned(g_Keys.log.section, g_Keys.log.displayLevel),
		g_Cfg.GetBoolean(g_Keys.log.section, g_Keys.log.rotate));
	if (!ret)
	{
		::fprintf(stderr, "M17Host: unable to open the log file\n");
		return EXIT_FAILURE;
	}

	if (isDaemon)
	{
		if (daemonize())
			return EXIT_FAILURE;
	}

	do
	{
		caught_signal = 0;

		CM17Host host;

		if (host.Start())
			return EXIT_FAILURE;

		pause();

		host.Stop();


		switch (caught_signal) {
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
				break;
			default:
				::LogInfo("M17Host exited on receipt of an unknown signal");
				break;
		}
	} while (caught_signal == SIGHUP);

	g_Log.Close();

	return ret ? EXIT_FAILURE : EXIT_SUCCESS;
}
