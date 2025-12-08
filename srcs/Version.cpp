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

#include <sstream>

#include "Version.h"

void CVersion::mkstring()
{
	std::stringstream ss;
	ss << unsigned(maj) << '.' << unsigned(min) << '.' << unsigned(rev);
	vstr.assign(ss.str());
}

CVersion::CVersion(const CVersion &v) : maj(v.GetMajor()), min(v.GetMinor()), rev(v.GetRevision())
{
	mkstring();
}

CVersion::CVersion(uint8_t a, uint8_t b, uint16_t c) : maj(a), min(b), rev(c)
{
	mkstring();
}

CVersion &CVersion::operator=(const CVersion &v) 
{
	maj=v.maj; 
	min=v.min; 
	rev=v.rev; 
	vstr.assign(v.vstr);
	return *this;
}


unsigned CVersion::GetMajor(void) const
{
	return maj;
}

unsigned CVersion::GetMinor(void) const
{
	return min;
}

unsigned CVersion::GetRevision(void) const
{
	return rev;
}

unsigned CVersion::GetVersion() const
{
	return (maj<<24) | (min<<16) | rev;
}

const char *CVersion::c_str() const
{
	return vstr.c_str();
}

// output
std::ostream &operator <<(std::ostream &os, const CVersion &v)
{
	os << v.c_str();
	return os;
};
