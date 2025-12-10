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

#include <cassert>
#include "FrameType.h";

uint16_t CFrameType::GetFrameType(bool isv3)
{
	if (isv3)
	{
		if (not m_v3type)
			buildV3();
		return m_v3type;
	} else {
		if (not m_legacytype)
			buildLegacy();
		return m_legacytype;
	}
}

void CFrameType::SetFrameType(uint16_t t)
{
	if (0xf000u & t)	// this has to be V#3 TYPE
	{
		// internalize the V#3 TYPE
		m_legacytype = 0u;
		m_isV3 = true;
		m_v3type = t;
		switch (t >> 12)	// 1. Do the payload
		{
		case 1u:
			m_payload = EPayloadType::dataonly;
			break;
		default:
		case 2u:
			m_payload = EPayloadType::c2_3200;
			break;
		case 3u:
			m_payload = EPayloadType::c2_1600;
		case 15u:
			m_payload = EPayloadType::packet;
			break;
		}
		switch ((t >> 9) & 0x3u)	// 2. Do the encryption
		{
		default:
		case 0u:
			m_encrypt = EEncryptType::none;
			break;
		case 1u:
			m_encrypt = EEncryptType::scram8;
			break;
		case 2u:
			m_encrypt = EEncryptType::scram16;
			break;
		case 3u:
			m_encrypt = EEncryptType::scram24;
			break;
		case 4u:
			m_encrypt = EEncryptType::aes128;
			break;
		case 5u:
			m_encrypt = EEncryptType::aes192;
			break;
		case 6u:
			m_encrypt = EEncryptType::aes256;
			break;
		}
		m_isSigned = (t & 0x10000u) ? true : false;	// 3. The digital signing
		switch ((t >> 4) & 0xfu)					// 4. What's in the META
		{
		default:
		case 0u:
			m_metatype = EMetaDatType::none;
			break;
		case 1u:
			m_metatype = EMetaDatType::gnss;
			break;
		case 2u:
			m_metatype = EMetaDatType::ecd;
			break;
		case 3u:
			m_metatype = EMetaDatType::text;
			break;
		case 4u:
			m_metatype = EMetaDatType::aes;
			break;
		}
		m_can = t & 0xfu;	// 5. Get the CAN
	} else {	// and this had to be a legacy TYPE
		m_isV3 = false;
		m_legacytype = t;
		m_v3type = 0;
		// 1 Get the payload type
		if (t & 1u)
		{
			m_payload = EPayloadType::packet;
		} else {
			switch ((t >> 1) & 0x3u)
			{
			case 1u:
				m_payload = EPayloadType::dataonly;
				break;
			default:
			case 2u:
				m_payload = EPayloadType::c2_3200;
				break;
			case 3u:
				m_payload = EPayloadType::c2_1600;
				break;
			}
		}
		uint8_t subtype = (t >> 5) & 0x3u;	// get the subtype field
		switch ((t >> 3) & 0x3u) // get the encrypt field
		{
		case 0u:	// no encryption
			switch (subtype)
			{
			default:
			case 0u:
				m_metatype = EMetaDatType::none;
				break;
			case 1u:
				m_metatype = EMetaDatType::gnss;
				break;
			case 2u:
				m_metatype = EMetaDatType::ecd;
				break;
			}
			break;
		case 1u: // scrambler
			switch (subtype)
			{
			default:
			case 0u:
				m_encrypt = EEncryptType::scram8;
				break;
			case 1u:
				m_encrypt = EEncryptType::scram16;
				break;
			case 2u:
				m_encrypt = EEncryptType::scram24;
				break;
			}
			break;
		case 2u: // aes
			switch (subtype)
			{
			default:
			case 0u:
				m_encrypt = EEncryptType::aes128;
				break;
			case 1u:
				m_encrypt = EEncryptType::aes192;
				break;
			case 2u:
				m_encrypt = EEncryptType::aes256;
				break;
			}
			break;
		}
		m_isSigned = (t & 0x800u) ? true : false;
		m_can = (t >> 7) & 0xfu;
	}
}

void CFrameType::buildLegacy()
{
	// payload type
	switch (m_payload)
	{
	case EPayloadType::packet:
		m_legacytype = 1u;
		break;
	case EPayloadType::dataonly:
		m_legacytype = 2u;
		break;
	case EPayloadType::c2_3200:
		m_legacytype = 4u;
		break;
	case EPayloadType::c2_1600:
		m_legacytype = 6u;
		break;
	}

	switch (m_encrypt)
	{
	case EEncryptType::none:
		break;
	case EEncryptType::scram8:
		m_legacytype |= 0x8u;
		break;
	case EEncryptType::scram16:
		m_legacytype |= 0x28u;
		break;
	case EEncryptType::scram24:
		m_legacytype |= 0x48u;
		break;
	case EEncryptType::aes128:
		m_legacytype |= 0x18u;
		break;
	case EEncryptType::aes192:
		m_legacytype |= 0x38u;
		break;
	case EEncryptType::aes256:
		m_legacytype |= 0x58u;
		break;
	}

	switch (m_metatype)
	{
	default:
		break;
	case EMetaDatType::gnss:
		m_legacytype |= 0x20;
		break;
	case EMetaDatType::ecd:
		m_legacytype |= 0x40u;
	}

	m_legacytype |= (m_can << 7);

	if (m_isSigned)
		m_legacytype |= 0x800u;
}

void CFrameType::buildV3()
{
	switch (m_payload)
	{
		case EPayloadType::packet:
			m_v3type = 0xfu;
			break;
		case EPayloadType::dataonly:
			m_v3type = 0x1u;
			break;
		default:
		case EPayloadType::c2_3200:
			m_v3type = 0x2u;
			break;
		case EPayloadType::c2_1600:
			m_v3type = 0x3u;
	}
	m_v3type <<= 3;
	switch (m_encrypt)
	{
	case EEncryptType::none:
		break;
	case EEncryptType::scram8:
		m_v3type |= 1u;
		break;
	case EEncryptType::scram16:
		m_v3type |= 2u;
		break;
	case EEncryptType::scram24:
		m_v3type |= 3u;
		break;
	case EEncryptType::aes128:
		m_v3type |= 4u;
		break;
	case EEncryptType::aes192:
		m_v3type |= 5u;
		break;
	case EEncryptType::aes256:
		m_v3type |= 6u;
	}
	m_v3type <<= 1;
	if (m_isSigned)
		m_v3type |= 1u;
	m_v3type <<= 4;
	switch (m_metatype)
	{
	case EMetaDatType::none:
		break;
	case EMetaDatType::gnss:
		m_v3type |= 1u;
		break;
	case EMetaDatType::ecd:
		m_v3type |= 2u;
		break;
	case EMetaDatType::text:
		m_v3type |= 3u;
		break;
	case EMetaDatType::aes:
		m_v3type |= 0xf;
		break;
	}
	m_v3type <<= 4;
	m_v3type |= m_can;
}
