// Copyright (C) 2015,2016,2020 by Jonathan Naylor G4KLX

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

#pragma once

#include <string>
#include <time.h>

#define	LogDebug(fmt, ...)   g_Log.Log(1U, fmt, ##__VA_ARGS__)
#define	LogMessage(fmt, ...) g_Log.Log(2U, fmt, ##__VA_ARGS__)
#define	LogInfo(fmt, ...)    g_Log.Log(3U, fmt, ##__VA_ARGS__)
#define	LogWarning(fmt, ...) g_Log.Log(4U, fmt, ##__VA_ARGS__)
#define	LogError(fmt, ...)   g_Log.Log(5U, fmt, ##__VA_ARGS__)
#define	LogFatal(fmt, ...)   g_Log.Log(6U, fmt, ##__VA_ARGS__)
#define LogLv(lv, fmt, ...)  g_Log.Log(lv, fmt, ##__VA_ARGS__)

class CLog
{
public:
	CLog() : LEVELS(" DDMIWEF") {}
	~CLog() {}
	void Log(unsigned level, const char* fmt, ...);
	bool Open(bool daemon, const std::string &path, const std::string &root, unsigned fl, unsigned dl, bool rotate);
	void Close();

private:
	bool logOpen();
	bool logOpenRotate();
	bool logOpenNoRotate();

	const std::string LEVELS;
	unsigned m_displayLevel,m_fileLevel;
	std::string m_filePath, m_fileRoot;
	bool m_daemon, m_fileRotate;

	FILE* m_fpLog = NULL;
	struct tm m_tm;
};

extern CLog g_Log;
