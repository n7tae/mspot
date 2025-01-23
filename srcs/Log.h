// Copyright (C) 2015,2016,2020 by Jonathan Naylor G4KLX

/****************************************************************
 *                                                              *
 *            mspot - An M17-only Hotspot/Repeater              *
 *                                                              *
 *         Copyright (c) 2024 by Thomas A. Early N7TAE          *
 *                                                              *
 * See the LICENSE file for details about the software license. *
 *                                                              *
 ****************************************************************/

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
