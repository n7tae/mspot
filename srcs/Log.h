// Copyright (C) 2015,2016,2020 by Jonathan Naylor G4KLX

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

#pragma once

#include <string>
#include <time.h>

#define LogDash(fmt, ...)    g_Log.Log(0U, fmt, ##__VA_ARGS__)
#define	LogDebug(fmt, ...)   g_Log.Log(1U, fmt, ##__VA_ARGS__)
#define	LogMessage(fmt, ...) g_Log.Log(2U, fmt, ##__VA_ARGS__)
#define	LogInfo(fmt, ...)    g_Log.Log(3U, fmt, ##__VA_ARGS__)
#define	LogWarning(fmt, ...) g_Log.Log(4U, fmt, ##__VA_ARGS__)
#define	LogError(fmt, ...)   g_Log.Log(5U, fmt, ##__VA_ARGS__)

class CLog
{
public:
	CLog() : LEVELS("-DMIWE") {}
	~CLog() {}
	void Log(unsigned level, const char* fmt, ...);
	bool Open(const std::string &dashpath, unsigned l);
	void Close();

private:

	const std::string LEVELS;
	unsigned m_level;

	FILE *m_fp = nullptr;
	struct tm m_tm;
};

extern CLog g_Log;
