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

#include <cstdint>
#include <string>
#include <cstring>

class CCallsign
{
public:
	CCallsign();
	CCallsign &operator=(const CCallsign &rhs);
	CCallsign(const std::string &cs);
	CCallsign(const uint8_t *code);
	void Clear();
	void CSIn(const std::string &cs);
	void CodeIn(const uint8_t *code);
	const std::string GetCS(unsigned len = 0) const;
	const char *c_str() const { return cs; }
	void CodeOut(uint8_t *out) const;
	uint64_t GetBase(void) const;
	uint64_t Hash() const { return coded; }
	bool operator==(const CCallsign &rhs) const;
	bool operator!=(const CCallsign &rhs) const;
	char GetModule(void) const;
	void SetModule(char m);
private:
	uint64_t coded;
	char cs[10];	// big enough to hold a 9-char callsign with a trailling NULL
};
