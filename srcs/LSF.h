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

#include <cstring>
#include <cstdint>
#include "CRC.h"

extern CCRC g_Crc;

struct SLSF
{
public:
	SLSF() { Clear(); }
	SLSF(uint8_t *from) { memcpy(data, from, 30); }
	SLSF(SLSF &) = delete;
	SLSF(SLSF &&) = delete;

	void Clear() { memset(data, 0, 30u); }

	// get pointer to different parts
		  uint8_t *GetData()				{ return data; }
	const uint8_t *GetCData() const 		{ return data; }
		  uint8_t *GetDstAddress()			{ return data; }
	const uint8_t *GetCDstAddress() const 	{ return data; }
	      uint8_t *GetSrcAddress()			{ return data+6; }
	const uint8_t *GetCSrcAddress() const	{ return data+6; }
	      uint8_t *GetMetaData()			{ return data+14; }
	const uint8_t *GetCMetaData() const		{ return data+14; }
 
	// get 16 bit values in host byte order
	uint16_t GetFrameType()	const { return 0x100u * data[12] + data[13]; }
	uint16_t GetCRC()		const { return 0x100u * data[28] + data[29]; }

	// set 16 bit values in network byte order
	void SetFrameType(uint16_t ft) { data[12] = ft >> 8; data[13] = ft & 0xffu; }

	// returns true if the crc is bad
	bool CheckCRC() const { return g_Crc.CheckCRC(data, 30u); }
	// calcualte and set the crc
	void CalcCRC() { g_Crc.SetCRC(data, 30u); }

private:
	uint8_t data[30];
};
