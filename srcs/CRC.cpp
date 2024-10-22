/****************************************************************
 *                                                              *
 *             More - An M17-only Repeater/HotSpot              *
 *                                                              *
 *         Copyright (c) 2024 by Thomas A. Early N7TAE          *
 *                                                              *
 * See the LICENSE file for details about the software license. *
 *                                                              *
 ****************************************************************/

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

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

void CCRC::setCRC(uint8_t *data, unsigned size)
{
	auto crc = calcCRC(data, size);
	data[size-2] = crc/0x100u;
	data[size-1] = crc%0x100u;
}

bool CCRC::checkCRC(const uint8_t *data, unsigned size) const
{
	auto crc = calcCRC(data, size);
	return crc == uint16_t(0x100u * unsigned(data[size-2]) + unsigned(data[size-1]));
}

uint16_t CCRC::calcCRC(const uint8_t *data, unsigned size) const
{
	uint16_t crc = CRC_START_16;
	for (size_t i=0; i<size-2; i++)
	{
		crc = (crc << 8) ^ crc_tab16[ ((crc >> 8) ^ uint16_t(data[i])) & 0x00FF ];
	}
	return crc;
}
