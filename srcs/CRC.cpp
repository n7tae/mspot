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

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "CRC.h"

#define CRC_POLY_16 0x5935u
#define CRC_START_16 0xFFFFu

CCRC::CCRC()
{
	for (uint16_t i=0; i<256; i++)
	{
		uint16_t crc = 0;
		uint16_t c = i << 8;

		for (uint16_t j=0; j<8; j++)
		{
			if ( (crc ^ c) & 0x8000 )
				crc = ( crc << 1 ) ^ CRC_POLY_16;
			else
				crc = crc << 1;

			c = c << 1;
		}
		crc_tab16[i] = crc;
	}
}

void CCRC::SetCRC(uint8_t *data, unsigned size)
{
	assert(size > 1u);
	auto crc = calcCRC(data, size);
	data[size-2] = crc/0x100u;
	data[size-1] = crc%0x100u;
}

// returns true if the CRC is NOT valid
bool CCRC::CheckCRC(const uint8_t *data, unsigned size) const
{
	return 0u != calcCRC(data, size + 2);
}

// calculates the 16-bt CRC for the first size-2 bytes
uint16_t CCRC::calcCRC(const uint8_t *data, unsigned size) const
{
	uint16_t crc = CRC_START_16;
	for (size_t i=0; i<size-2; i++)
	{
		crc = (crc << 8) ^ crc_tab16[ ((crc >> 8) ^ uint16_t(data[i])) & 0x00FF ];
	}
	return crc;
}
