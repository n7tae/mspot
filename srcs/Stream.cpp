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

#include "Stream.h"

void CStream::Initialize(EStreamType t)
{
	type = t;
}

// returns false on success
void CStream::OpenStream(const uint8_t *src, uint16_t sid, const std::string &f)
{
	source.CodeIn(src);
	previousid = streamid = sid;
	count = 0u;
	from.assign(f);
	if ((EStreamType::gate == type))
		printMsg(TC_BLUE, TC_DEFAULT, "Open gateway stream id=%04x from %s at %s\n", streamid, source.c_str(), from.c_str());
	else
		printMsg(TC_BLUE, TC_DEFAULT, "Open modem stream id=%04x from %s\n", sid, source.c_str());
}

void CStream::CloseStream(bool istimeout)
{
	const std::string name((EStreamType::gate == type) ? "gateway" : "modem");
	printMsg(TC_BLUE, TC_DEFAULT, "%s %s stream id=%04x, duration=%.2f sec\n", (istimeout ? "Timeout" : "Close"), name.c_str(), streamid, 0.04f * count);
	streamid = 0u;
}

bool CStream::IsOpen()
{
	return streamid != 0u;
}

double CStream::GetLastTime()
{
	return lastPacketTime.time();
}

uint16_t CStream::GetStreamID()
{
	return streamid;
}

uint16_t CStream::GetPreviousID()
{
	return previousid;
}

void CStream::CountnTouch()
{
	count++;
	lastPacketTime.start();
}
