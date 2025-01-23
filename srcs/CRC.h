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

class CCRC
{
public:
	CCRC();
	void setCRC(uint8_t *data, unsigned size);
	bool checkCRC(const uint8_t *data, unsigned size) const;

private:
	uint16_t crc_tab16[256];
	uint16_t calcCRC(const uint8_t *data, unsigned size) const;
};
