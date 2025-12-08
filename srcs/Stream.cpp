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

#include "Log.h"
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
	LogInfo("Open %s stream id=0x%04x from %s at %s", name.c_str(), streamid, source.GetCS().c_str(), from.c_str());
	return false;
}

void CStream::CloseStream(bool istimeout)
{
	LogInfo("%s stream %s id=0x%02hu, duration=%.2f sec", name.c_str(), istimeout ? "timeout" : "closed", streamid, 0.04f * count);
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
