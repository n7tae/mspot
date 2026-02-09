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
#include <iostream>
#include <iomanip>
#include <cstdio>
#include <cstdarg>
#include <ctime>

#include "Base.h"

#ifndef NO_TS
void CBase::printMsg(const char *tsColor, const char *txColor, const char* fmt, ...) const
#else
void CBase::printMsg(const char */*tsColor*/, const char *txColor, const char *fmt, ...) const
#endif
{
	if (nullptr == fmt)
		return;	// this should never happen
	
	// find the end of the format string
	const char *endc = fmt;
	while(*(endc+1))
		endc++;

#ifndef NO_TS
	// print a timeStame if a color esc sequence was provided
	if (tsColor)
		timeStamp(tsColor);
#endif

	// now print a variable list of things
	char str[1000];	// plenty of room
	va_list ap;

	va_start(ap, fmt);
	vsprintf(str, fmt, ap);
	va_end(ap);

	if(txColor != nullptr)
	{
		fputs(txColor, stdout);
		fputs(str, stdout);
		fputs(TC_DEFAULT, stdout);
	}
	else
	{
		fputs(str, stdout);
	}
	// flush the buffer if the last character is a newline
	if ('\n' == *endc)
		fflush(stdout);
}

void CBase::timeStamp(const char *color_esc) const
{
	struct timeval now;
	gettimeofday(&now, nullptr);
	struct tm *tms = localtime(&now.tv_sec);
	printf("%s[%02d/%02d %02d:%02d:%02d.%03ld] ", color_esc, tms->tm_mon + 1, tms->tm_mday, tms->tm_hour, tms->tm_min, tms->tm_sec, now.tv_usec / 1000u);
}


void CBase::Dump(const char *title, const void *pointer, unsigned length) const
{
	const unsigned char *data = (const unsigned char *)pointer;

	if (nullptr != title)
		std::cout << title << std::endl;

	unsigned int offset = 0U;

	while (length > 0) {

		unsigned int bytes = (length > 16) ? 16U : length;

		for (unsigned i = 0U; i < bytes; i++) {
			if (i)
				std::cout << " ";
			std::cout << std::hex << std::setw(2) << std::right << std::setfill('0') << int(data[offset + i]);
		}

		for (unsigned int i = bytes; i < 16U; i++)
			std::cout << "   ";

		std::cout << "   *";

		for (unsigned i = 0U; i < bytes; i++) {
			unsigned char c = data[offset + i];

			if (::isprint(c))
				std::cout << c;
			else
				std::cout << '.';
		}

		std::cout << '*' << std::endl;

		offset += 16U;

		if (length >= 16)
			length -= 16;
		else
			length = 0;
	}
}
