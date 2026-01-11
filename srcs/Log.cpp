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

#include <sys/time.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <ctime>
#include <cassert>
#include <cstring>
#include <signal.h>

#include "Log.h"

// the one and only global object
CLog g_Log;

bool CLog::Open(const std::string &dashpath, unsigned l)
{
	if (l > 6U) l = 6U;
	m_level = l;

	if (dashpath.empty())
		return true;

	m_fp = ::fopen(dashpath.c_str(), "a+t");
	if (nullptr == m_fp)
	{
		fprintf(stderr, "Couldn't open %s: %s", dashpath.c_str(), strerror(errno));
		return true;
	}
	return false;
}

void CLog::Close()
{
	if (m_fp != nullptr)
		fclose(m_fp);
	m_fp = nullptr;
}

void CLog::Log(unsigned level, const char *fmt, ...)
{
	assert(fmt != nullptr);
	char buffer[501U];
	struct timeval now;
	gettimeofday(&now, nullptr);

	struct tm* tm = ::localtime(&now.tv_sec);

	sprintf(buffer, "%c: %02d/%02d %02d:%02d:%02d.%03lld ", LEVELS[level], tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec, now.tv_usec / 1000LL);

	va_list vl;
	va_start(vl, fmt);

	vsnprintf(buffer + ::strlen(buffer), 500, fmt, vl);

	va_end(vl);

	if (0 == level)
	{
		fprintf(m_fp, "%s\n", buffer);
		fflush(m_fp);
	}
	else if (m_level and level >= m_level)
	{
		fprintf(stdout, "%s\n", buffer);
		fflush(stdout);
	}
}
