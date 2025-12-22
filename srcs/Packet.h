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
#include <string.h>
#include <memory>
#include <vector>

#include "Callsign.h"

using SM17RefPacket = struct __attribute__((__packed__)) reflector_tag {
	char magic[4];
	uint8_t cscode[6];
	char mod;
}; // 11 bytes (but sometimes 4 or 10 bytes)

#define MAX_PACKET_SIZE 859

enum class EPacketType { none, stream, packet };

class CPacket
{
public:
	void Initialize(EPacketType t, unsigned length = 54);
	void Initialize(EPacketType t, const uint8_t *in, unsigned length = 54);
	// get pointer to different parts
	      uint8_t *GetData()        { return data.data(); }
	const uint8_t *GetCData() const { return data.data(); }
		  uint8_t *GetDstAddress();
	const uint8_t *GetCDstAddress() const;
	      uint8_t *GetSrcAddress();
	const uint8_t *GetCSrcAddress() const;
	      uint8_t *GetMetaData();
	const uint8_t *GetCMetaData() const;
          uint8_t *GetPayload(bool firsthalf = true);
	const uint8_t *GetCPayload(bool firsthalf = true) const;

	// get various 16 bit value in host byte order
	uint16_t GetStreamId()    const;
	uint16_t GetFrameType()   const;
	uint16_t GetFrameNumber() const;
	uint16_t GetCRC(bool first = true) const;

	// set 16 bit values in network byte order
	void SetStreamId(uint16_t sid);
	void SetFrameType(uint16_t ft);
	void SetFrameNumber(uint16_t fn);

	// get the state data
	size_t          GetSize() const { return data.size(); }
	EPacketType     GetType() const { return ptype; }
	bool       IsLastPacket() const;
	bool           CheckCRC() const;

	// calculate and set CRC value(s)
	void CalcCRC();

private:
	uint16_t get16At(size_t pos) const;
	void set16At(size_t pos, uint16_t val);

	EPacketType ptype;
	std::vector<uint8_t> data;
};
