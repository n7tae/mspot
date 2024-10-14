/****************************************************************
 *                                                              *
 *             More - An M17-only Repeater/HotSpot              *
 *                                                              *
 *         Copyright (c) 2024 by Thomas A. Early N7TAE          *
 *                                                              *
 * See the LICENSE file for details about the software license. *
 *                                                              *
 ****************************************************************/

#pragma once

#include <cstdint>
#include <string>
#include <cstring>

class CCallsign
{
public:
	CCallsign();
	CCallsign(const std::string &cs);
	CCallsign(const uint8_t *code);
	void CSIn(const std::string &cs);
	void CodeIn(const uint8_t *code);
	const std::string GetCS(unsigned len = 0) const;
	void CodeOut(uint8_t *out) const { memcpy(out, code, 6); };
	bool operator==(const CCallsign &rhs) const;
	char GetModule(void) const;

private:
	uint8_t code[6];
	char cs[10];
};
