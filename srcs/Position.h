/*
	mspot - an M17 hot-spot using an  M17 CC1200 Raspberry Pi Hat
				Copyright (C) 2026 Thomas A. Early

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

#include <cstdint>
#include <string>

class CPosition
{
public:
	CPosition() = delete;
	CPosition(const uint8_t *data);
	virtual ~CPosition() {}

	const char *GetPosition(double &lat, double &lon);

private:
	void Get(const uint8_t *data);
	// Meta data
	double latitude, longitude;
	unsigned station, source;
	bool positionValid;
	char maidenhead[7];
};
