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

#include <cstdint>
#include <iostream>

class CVersion
{
public:
	// constructors
	CVersion();
	CVersion(uint8_t maj, uint8_t min, uint8_t rev);

	// get
	int GetMajor(void) const;
	int GetMinor(void) const;
	int GetRevision(void) const;
	int GetVersion(void) const;
	const char *GetString() { return strversion.c_str(); }

	// set
	void Set(uint8_t, uint8_t, uint8_t);

	// comparison operators
	bool operator ==(const CVersion &v) const;
	bool operator !=(const CVersion &v) const;
	bool operator >=(const CVersion &v) const;
	bool operator <=(const CVersion &v) const;
	bool operator  >(const CVersion &v) const;
	bool operator  <(const CVersion &v) const;

	// output
	friend std::ostream &operator <<(std::ostream &os, const CVersion &v);


protected:
	// data
	int version;
	std::string strversion;
};
