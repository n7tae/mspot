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
	char cs[10];
};
