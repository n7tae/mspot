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

void CStream::Initialize(const std::string &n)
{
	name.assign(n);
}

// returns false on success
bool CStream::OpenStream(const uint8_t *src, uint16_t sid, const std::string &f)
{
	source.CodeIn(src);
	previousid = streamid = sid;
	count = 0u;
	from.assign(f);
	printMsg(TC_BLUE, TC_GREEN, "Open %s stream id=0x%04x from %s at %s\n", name.c_str(), streamid, source.GetCS().c_str(), from.c_str());
	return false;
}

void CStream::CloseStream(bool istimeout)
{
	printMsg(TC_BLUE, TC_GREEN, "%s stream %s id=0x%04x, duration=%.2f sec\n", name.c_str(), istimeout ? "timeout" : "closed", streamid, 0.04f * count);
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
