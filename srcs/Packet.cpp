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

#include "Packet.h"
#include "CRC.h"

static CCRC CRC;

// returns true if there is a problem with the data, doesn't check CRC
EPacketType CPacket::Validate(uint8_t *in, unsigned length)
{
	if (0 == memcmp(in, "M17", 3))
	{
		if (' ' == in[3] and 54u == length)
		{
			TYPE.GetFrameType(0x100u*in[18]+in[19]);
			if (EPayloadType::packet != TYPE.GetPayloadType())
			{
				ptype = EPacketType::stream;
				data.resize(length);
				memcpy(data.data(), in, length);
				return EPacketType::stream;
			}
		} else if ('P' == data[3] and length > 37 and length <= MAX_PACKET_SIZE) {
			TYPE.GetFrameType(0x100u*in[16]+in[17]);
			if (EPayloadType::packet == TYPE.GetPayloadType())
			{
				ptype = EPacketType::packet;
				data.resize(length);
				memcpy(data.data(), in, length);
				return EPacketType::packet;
			}
		}
	}
	return EPacketType::none;
}

void CPacket::Initialize(EPacketType t, unsigned length)
{
	ptype = t;
	if (EPacketType::stream == t)
	{
		memcpy(data.data(), "M17 ", 4);
		data.resize(54);
	} else if (EPacketType::packet == t) {
		memcpy(data.data(), "M17P", 4);
		data.resize(length);
	}
	data.shrink_to_fit();
}

uint8_t *CPacket::GetDstAddress()
{
	return data.data() + ((EPacketType::stream == ptype) ? 6u : 4u);
}

const uint8_t *CPacket::GetCDstAddress() const
{
	return data.data() + ((EPacketType::stream == ptype) ? 6u : 4u);
}

uint8_t *CPacket::GetSrcAddress()
{
	return data.data() + ((EPacketType::stream == ptype) ? 12u : 10u);
}

const uint8_t *CPacket::GetCSrcAddress() const
{
	return data.data() + ((EPacketType::stream == ptype) ? 12u : 10u);
}

uint8_t *CPacket::GetMetaData()
{
	return data.data() + ((EPacketType::stream == ptype) ? 20 : 18);
}

const uint8_t *CPacket::GetCMetaData() const
{
	return data.data() + ((EPacketType::stream == ptype) ? 20 : 18);
}

// returns the StreamID in host byte order
uint16_t CPacket::GetStreamId() const
{
	return (EPacketType::stream == ptype) ? get16At(4) : 0u;
}

void CPacket::SetStreamId(uint16_t sid)
{
	if ((EPacketType::stream == ptype))
	{
		set16At(4, sid);
	}
}

// returns LSD:TYPE in host byte order
uint16_t CPacket::GetFrameType() const
{
	return get16At((EPacketType::stream == ptype) ? 18 : 16);
}

void CPacket::SetFrameType(uint16_t ft)
{
	set16At((EPacketType::stream == ptype) ? 18 : 16, ft);
}

uint16_t CPacket::GetFrameNumber() const
{
	if ((EPacketType::stream == ptype))
		return get16At(34);
	else
		return 0u;
}

uint16_t CPacket::GetCRC(bool first) const
{
	uint16_t rval;
	if ((EPacketType::stream == ptype))
	{
		rval = get16At(52);
	}
	else
	{
		if (first)
			rval = get16At(32);
		else
			rval = get16At(data.size() - 2);
	}
	return rval;
}

void CPacket::SetFrameNumber(uint16_t fn)
{
	if ((EPacketType::stream == ptype))
	{
		set16At(34, fn);
	}
}

uint8_t *CPacket::GetPayload(bool firsthalf)
{
	if ((EPacketType::stream == ptype))
		return data.data() + (firsthalf ? 36u : 44u);
	else
		return data.data() + 34;
}

const uint8_t *CPacket::GetCPayload(bool firsthalf) const
{
	if ((EPacketType::stream == ptype))
		return data.data() + (firsthalf ? 36u : 44u);
	else
		return data.data() + 34;
}

bool CPacket::IsLastPacket() const
{
	if (EPacketType::stream == ptype)
		return 0x8000u == (0x8000u & GetFrameNumber());
	return false;
}

bool CPacket::CheckCRC() const
{
	if ((EPacketType::stream == ptype))
	{
		return (CRC.CheckCRC(data.data(), 54));
	} else {
		return CRC.CheckCRC(data.data()+4, 30) or CRC.CheckCRC(data.data()+34, data.size()-34);
	}
}

void CPacket::CalcCRC()
{
	if ((EPacketType::stream == ptype))
	{
		CRC.SetCRC(data.data(), 54);
	}
	else
	{	// set the CRC for the LSF
		CRC.SetCRC(data.data()+4, 30);
		// now for the payload
		CRC.SetCRC(data.data()+34, data.size()-34);
	}
}

uint16_t CPacket::get16At(size_t pos) const
{
	return 0x100u * data[pos] + data[pos + 1];
}

void CPacket::set16At(size_t pos, uint16_t val)
{
	data[pos++] = 0xffu & (val >> 8);
	data[pos]   = 0xffu & val;
}
