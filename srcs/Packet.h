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

#include <arpa/inet.h>

#define IPFRAMESIZE 54

// M17 Packet

using SLSD = struct __attribute__((__packed__)) lsd_tag
{
	uint8_t  addr_dst[6]; //48 bit int - you'll have to assemble it yourself unfortunately
	uint8_t  addr_src[6];
	uint16_t frametype;   //frametype flag field per the M17 spec
	uint8_t  meta[14];    //meta data (IVs and GNSS)
}; // 6 + 6 + 2 + 14 = 28 bytes = 224 bits

// the one and only frame
using SFrame = struct __attribute__((__packed__)) frame_tag
{
	uint8_t  magic[4];
	uint16_t streamid;
	SLSD     lsd;
	uint16_t framenumber;
	uint8_t  payload[16];
	uint16_t crc;
}; // 4 + 2 + 28 + 2 + 16 + 2 = 54 bytes = 432 bits

using SIPFrame = struct ipframe_tag
{
	SFrame data;
	uint16_t GetCRC        (void) const        { return ntohs(data.crc);            }
	uint16_t GetFrameNumber(void) const        { return ntohs(data.framenumber);    }
	uint16_t GetFrameType  (void) const        { return ntohs(data.lsd.frametype);  }
	uint16_t GetStreamID   (void) const        { return ntohs(data.streamid);       }
	void     SetCRC        (const uint16_t cr) { data.crc            = htons(cr);   }
	void     SetFrameNumber(const uint16_t fn) { data.framenumber    = htons(fn);   }
	void     SetFrameType  (const uint16_t ft) { data.lsd.frametype = htons(ft);    }
	void     SetStreamID   (const uint16_t id) { data.streamid       = htons(id);   }
}; // also 54 bytes

// reflector packet for linking, unlinking, pinging, etc
using SM17RefPacket = struct __attribute__((__packed__)) reflector_tag
{
	char magic[4];
	uint8_t cscode[6];
	char mod;
}; // 11 bytes (but sometimes 4 or 10 bytes)
