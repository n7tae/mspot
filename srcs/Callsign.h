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

#include <cstdint>
#include <string>
#include <cstring>

#include "Base.h"

class CCallsign : public CBase
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
	bool IsReflector(void) const;
private:
	uint64_t coded;
	char cs[10];	// big enough to hold a 9-char callsign with a trailling nullptr
};
