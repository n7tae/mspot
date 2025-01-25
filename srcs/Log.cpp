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

#include <sys/time.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <ctime>
#include <cassert>
#include <cstring>

#include "Log.h"

// the one and only global object
CLog g_Log;

bool CLog::logOpenRotate()
{
	bool status = false;

	if (m_fileLevel == 0U)
		return true;

	time_t now;
	time(&now);

	struct tm* tm = ::gmtime(&now);

	if (tm->tm_mday == m_tm.tm_mday && tm->tm_mon == m_tm.tm_mon && tm->tm_year == m_tm.tm_year) {
		if (m_fpLog != NULL)
		    return true;
	} else {
		if (m_fpLog != NULL)
			::fclose(m_fpLog);
	}

	char filename[200U];
	sprintf(filename, "%s/%s-%04d-%02d-%02d.log", m_filePath.c_str(), m_fileRoot.c_str(), tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday);

	if ((m_fpLog = fopen(filename, "a+t")) != NULL) {
		status = true;

		if (m_daemon)
			dup2(fileno(m_fpLog), fileno(stderr));
	}

	m_tm = *tm;

	return status;
}

bool CLog::logOpenNoRotate()
{
	bool status = false;

	if (m_fileLevel == 0U)
		return true;

	if (m_fpLog != NULL)
		return true;

	char filename[200U];
	sprintf(filename, "%s/%s.log", m_filePath.c_str(), m_fileRoot.c_str());

	if ((m_fpLog = ::fopen(filename, "a+t")) != NULL) {
		status = true;

		if (m_daemon)
			dup2(fileno(m_fpLog), fileno(stderr));
	}

	return status;
}

bool CLog::logOpen()
{
	if (m_fileRotate)
		return logOpenRotate();
	else
		return logOpenNoRotate();
}

bool CLog::Open(bool d, const std::string &path, const std::string &root, unsigned fl, unsigned dl, bool r)
{
	m_filePath     = path;
	m_fileRoot     = root;
	m_fileLevel    = fl;
	m_displayLevel = dl;
	m_daemon       = d;
	m_fileRotate   = r;

	if (m_daemon)
		m_displayLevel = 0U;

	return logOpen();
}

void CLog::Close()
{
	if (m_fpLog != NULL)
		fclose(m_fpLog);
	m_fpLog = NULL;
}

void CLog::Log(unsigned level, const char* fmt, ...)
{
	assert(fmt != NULL);

	char buffer[501U];
	struct timeval now;
	gettimeofday(&now, NULL);

	struct tm* tm = ::localtime(&now.tv_sec);

	sprintf(buffer, "%c: %02d/%02d %02d:%02d:%02d.%03lld ", LEVELS[level], tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec, now.tv_usec / 1000LL);

	va_list vl;
	va_start(vl, fmt);

	::vsnprintf(buffer + ::strlen(buffer), 500, fmt, vl);

	va_end(vl);

	if (level >= m_fileLevel && m_fileLevel != 0U)
	{
		bool ret = logOpen();
		if (!ret)
			return;

		::fprintf(m_fpLog, "%s\n", buffer);
		::fflush(m_fpLog);
	}

	if (level >= m_displayLevel && m_displayLevel != 0U)
	{
		::fprintf(stdout, "%s\n", buffer);
		::fflush(stdout);
	}

	if (level == 6U)
	{		// Fatal
		::fclose(m_fpLog);
		exit(1);
	}
}
