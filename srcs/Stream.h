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
