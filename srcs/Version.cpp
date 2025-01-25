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
#include "Version.h"

CVersion::CVersion() : version(0) {}

CVersion::CVersion(uint8_t maj, uint8_t min, uint8_t rev)
{
	Set(maj, min, rev);
}

int CVersion::GetMajor(void) const
{
	return version / 0x10000;
}

int CVersion::GetMinor(void) const
{
	return version / 0x100 % 0x100;
}

int CVersion::GetRevision(void) const
{
	return version % 0x100;
}

int CVersion::GetVersion(void)  const
{
	return version;
}

void CVersion::Set(uint8_t maj, uint8_t min, uint8_t rev)
{
	version = 0x10000*maj + 0x100*min + rev;
	strversion.assign(std::to_string(maj) + '.' + std::to_string(min) + '.' + std::to_string(rev));
}

bool CVersion::operator ==(const CVersion &v) const
{
	return v.version == version;
};

bool CVersion::operator !=(const CVersion &v) const
{
	return v.version != version;
};

bool CVersion::operator >=(const CVersion &v) const
{
	return v.version >= version;
}

bool CVersion::operator <=(const CVersion &v) const
{
	return v.version <= version;
}

bool CVersion::operator >(const CVersion &v) const
{
	return v.version  > version;
}

bool CVersion::operator <(const CVersion &v) const
{
	return v.version  < version;
}

// output
std::ostream &operator <<(std::ostream &os, const CVersion &v)
{
	os << v.GetMajor() << '.' << v.GetMinor() << '.' << v.GetRevision();
	return os;
};
