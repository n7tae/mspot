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
#include <vector>
#include <queue>

class CLineTools
{
public:
	CLineTools(void) {}
	virtual ~CLineTools(void) {}

protected:
	void ltrim(std::string &s);
	void rtrim(std::string &s);
	void trim(std::string &s);
	void split(const std::string &s, char delim, std::vector<std::string> &v);
	void split(const std::string &s, char delim, std::queue<std::string> &q);
};
