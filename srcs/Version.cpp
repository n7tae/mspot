/****************************************************************
 *                                                              *
 *            mspot - An M17-only Hotspot/Repeater              *
 *                                                              *
 *         Copyright (c) 2024 by Thomas A. Early N7TAE          *
 *                                                              *
 * See the LICENSE file for details about the software license. *
 *                                                              *
 ****************************************************************/

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
