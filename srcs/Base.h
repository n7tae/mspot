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

class CBase
{
public:
	CBase(void) {}
	virtual ~CBase(void) {}
protected:
	void printMsg(const char *tsColor, const char *txColor, const char *fmt, ...);
	void Dump(const char *title, const void *data, unsigned length);
	const char *TC_BLACK   {"\033[30m"};
	const char *TC_RED     {"\033[31m"};
	const char *TC_GREEN   {"\033[32m"};
	const char *TC_YELLOW  {"\033[33m"};
	const char *TC_BLUE    {"\033[34m"};
	const char *TC_MAGENTA {"\033[35m"};
	const char *TC_CYAN    {"\033[36m"};
	const char *TC_WHITE   {"\033[37m"};
	const char *TC_B_BLACK   {"\033[90m"};
	const char *TC_B_RED     {"\033[91m"};
	const char *TC_B_GREEN   {"\033[92m"};
	const char *TC_B_YELLOW  {"\033[83m"};
	const char *TC_B_BLUE    {"\033[94m"};
	const char *TC_B_MAGENTA {"\033[95m"};
	const char *TC_B_CYAN    {"\033[96m"};
	const char *TC_B_WHITE   {"\033[97m"};
	const char *TC_DEFAULT   {"\033[39m"};
private:
	void timeStamp(const char *color_esc);
};
