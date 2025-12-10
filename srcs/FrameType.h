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

#include <cstring>
#include <cstdint>

enum class EPayloadType { dataonly, c2_3200, c2_1600, packet };
enum class EEncryptType { none, scram8, scram16, scram24, aes128, aes192, aes256 };
enum class EMetaDatType { none, gnss, ecd, text, aes };

class CFrameType
{
public:
	CFrameType() : m_isSigned(false), m_isV3(false), m_can(0) { buildLegacy(); }
	void SetFrameType(uint16_t t);
	uint16_t GetFrameType(bool wantV3);
	EPayloadType GetPayloadType()  const { return m_payload;  }
	EEncryptType GetEncryptType()  const { return m_encrypt;  }
	EMetaDatType GetMetaDataType() const { return m_metatype; }
	bool GetIsSigned() const { return m_isSigned; }
	uint8_t GetCan() const { return m_can; }
	void SetPayloadType(EPayloadType t) { m_payload = t; m_isV3 ? buildV3() : buildLegacy(); }
	void SetEncryptType(EEncryptType t) { m_encrypt = t; m_isV3 ? buildV3() : buildLegacy(); }
	void SetMetaDataType(EMetaDatType t) { m_metatype = t; m_isV3 ? buildV3() : buildLegacy(); }
	void SetSigned(bool issigned) { m_isSigned = issigned; m_isV3 ? buildV3() : buildLegacy(); }
	void SetCan(uint8_t can) { m_can = can;}

private:
	bool m_isSigned, m_isV3;
	uint16_t m_can;
	EPayloadType m_payload;
	EEncryptType m_encrypt;
	EMetaDatType m_metatype;
	uint16_t m_legacytype, m_v3type;

	void buildLegacy();
	void buildV3();
};
