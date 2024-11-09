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

#include <arpa/inet.h>

#define IPFRAMESIZE 54

// M17 Packet

using SMLich = struct __attribute__((__packed__)) lich_tag
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
	SMLich   lich;
	uint16_t framenumber;
	uint8_t  payload[16];
	uint16_t crc;
}; // 4 + 2 + 28 + 2 + 16 + 2 = 54 bytes = 432 bits

using SIPFrame = struct ipframe_tag
{
	SFrame data;
	uint16_t GetCRC        (void) const        { return ntohs(data.crc);            }
	uint16_t GetFrameNumber(void) const        { return ntohs(data.framenumber);    }
	uint16_t GetFrameType  (void) const        { return ntohs(data.lich.frametype); }
	uint16_t GetStreamID   (void) const        { return ntohs(data.streamid);       }
	void     SetCRC        (const uint16_t cr) { data.crc            = htons(cr);   }
	void     SetFrameNumber(const uint16_t fn) { data.framenumber    = htons(fn);   }
	void     SetFrameType  (const uint16_t ft) { data.lich.frametype = htons(ft);   }
	void     SetStreamID   (const uint16_t id) { data.streamid       = htons(id);   }
}; // also 54 bytes

// reflector packet for linking, unlinking, pinging, etc
using SM17RefPacket = struct __attribute__((__packed__)) reflector_tag
{
	char magic[4];
	uint8_t cscode[6];
	char mod;
}; // 11 bytes (but sometimes 4 or 10 bytes)
