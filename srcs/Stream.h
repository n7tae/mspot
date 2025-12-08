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

#include <cstdint>
#include <string>

#include "Callsign.h"
#include "SteadyTimer.h"

class CStream
{
public:
	CStream() : streamid(0) {}
	~CStream() {}
	void Initialize(const std::string &n);
	bool OpenStream(const uint8_t *src, uint16_t id, const std::string &from);
	void CloseStream(bool isTimeout);
	bool IsOpen();
	double GetLastTime();
	uint16_t GetStreamID();
	uint16_t GetPreviousID();
	void CountnTouch();

private:
	uint16_t streamid, previousid;
	std::string name;
	std::string from;
	unsigned count;
	CCallsign source;
	CSteadyTimer lastPacketTime;
};
