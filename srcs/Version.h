/*

         mspot - an M17-only HotSpot using an RPi CC1200 hat
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

#include <cstdint>
#include <iostream>
#include <string>

class CVersion
{
public:
	// constructor
	CVersion() = delete;
	CVersion(const CVersion &v);
	CVersion(uint8_t a, uint8_t b, uint16_t c);
	CVersion &operator=(const CVersion &v);
	~CVersion() {}

	// get
	unsigned GetMajor(void) const;
	unsigned GetMinor(void) const;
	unsigned GetRevision(void) const;
	unsigned GetVersion(void) const;
	const char *c_str(void) const;

	// output
	friend std::ostream &operator <<(std::ostream &os, const CVersion &v);


protected:
	void mkstring();
	// data
	std::string vstr;
	uint8_t maj, min;
	uint16_t rev;
};
