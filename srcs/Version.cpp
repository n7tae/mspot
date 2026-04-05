/*
	A usable Version class
	Copyright (C) 2026 Thomas A. Early, N7TAE

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

#include <sstream>

#include "Version.h"

/************************************
 * The range for major:    0 - 428  *
 * The range for minor:    0 - 9999 *
 * The range for revision: 0 - 9999 *
 ************************************/
CVersion::CVersion(uint16_t maj, uint16_t min, uint16_t rev)
{
	checkInput(major,    "major",    maj,  428u);
	checkInput(minor,    "minor",    min, 9999u);
	checkInput(revision, "revision", rev, 9999u);
}

unsigned CVersion::GetVersion() const
{
	return 100000000u * major + 10000u * minor + revision;
}

void CVersion::checkInput(uint16_t &val, const std::string &label, unsigned proposed, unsigned maximum)
{
	if (proposed > maximum)
	{
		std::cout << "CVersion WARNING: Value for " << label << ", " << proposed << ", is too large. Resetting to " << maximum << std::endl;
		proposed = maximum;
	}
	val = proposed;
}

const char *CVersion::c_str() const
{
	std::stringstream ss;
	ss << major << '.' << minor << '.' << revision;
#ifdef DHT
	ss << "-dht";
#endif
#ifdef DVR
	ss << "-dvref";
#endif
#ifdef DEBUG
	ss << "-debug";
#endif
	static std::string vstr(ss.str());
	return vstr.c_str();
}

// output
std::ostream &operator <<(std::ostream &os, const CVersion &v)
{
	os << v.major << '.' << v.minor << '.' << v.revision;
#ifdef DHT
	os << "-dht";
#endif
#ifdef DVR
	os << "-dvref";
#endif
#ifdef DEBUG
	os << "-debug";
#endif
	return os;
};
