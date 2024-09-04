/*
*   Copyright (C) 2018,2020,2021 by Jonathan Naylor G4KLX
*
*   This program is free software; you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation; either version 2 of the License, or
*   (at your option) any later version.
*
*   This program is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU General Public License for more details.
*
*   You should have received a copy of the GNU General Public License
*   along with this program; if not, write to the Free Software
*   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include "GPSHandler.h"
#include "M17Defines.h"
#include "Utils.h"
#include "Log.h"

#include <cstdint>
#include <cstdio>
#include <cassert>
#include <cstring>
#include <cmath>


const float INVALID_GPS_DATA = 999.0F;

CGPSHandler::CGPSHandler(const std::string& callsign, const std::string& suffix, CAPRSWriter* writer) :
m_callsign(callsign),
m_writer(writer),
m_suffix(suffix)
{
	assert(!callsign.empty());
	assert(writer != NULL);

	m_callsign += "-" + suffix;
}

CGPSHandler::~CGPSHandler()
{
}

void CGPSHandler::process(const CM17LSF& lsf)
{
	unsigned char encType = lsf.getEncryptionType();
	if (encType != M17_ENCRYPTION_TYPE_NONE)
		return;

	unsigned char encSubType = lsf.getEncryptionSubType();
	if (encSubType != M17_ENCRYPTION_SUB_TYPE_GPS)
		return;

	unsigned char meta[M17_META_LENGTH_BYTES];
	lsf.getMeta(meta);

	std::string text;
	switch (meta[0U]) {
		case M17_GPS_CLIENT_M17CLIENT:
			text = "M17 Client via MMDVM";
			break;
		case M17_GPS_CLIENT_OPENRTX:
			text = "OpenRTX via MMDVM";
			break;
		default:
			text = "M17 via MMDVM";
			break;
	}

	char sym1, sym2;
	switch (meta[1U]) {
		case M17_GPS_TYPE_HANDHELD:
			sym1 = '/';
			sym2 = '[';
			break;
		case M17_GPS_TYPE_MOBILE:
			sym1 = '/';
			sym2 = '>';
			break;
		case M17_GPS_TYPE_FIXED:
			sym1 = '/';
			sym2 = 'y';
			break;
		default:
			sym1 = '/';
			sym2 = 'I';
			break;
	}

	float tempLat = float(meta[2U]) + float((meta[3U] << 8) + (meta[4U] << 0)) / 65535.0F;
	float tempLon = float(meta[5U]) + float((meta[6U] << 8) + (meta[7U] << 0)) / 65535.0F;

	float latitude  = ::floor(tempLat);
	float longitude = ::floor(tempLon);

	latitude  = (tempLat - latitude)  * 60.0F + latitude  * 100.0F;
	longitude = (tempLon - longitude) * 60.0F + longitude * 100.0F;

	char north = 'N';
	if ((meta[8U] & 0x01U) == 0x01U) north = 'S';

	char east = 'E';
	if ((meta[8U] & 0x02U) == 0x02U) east = 'W';

	float altitude = INVALID_GPS_DATA;
	if ((meta[8U] & 0x04U) == 0x04U)
		altitude = float((meta[9U] << 8) + (meta[10U] << 0)) - 1500.0F;

	float speed = INVALID_GPS_DATA, track = INVALID_GPS_DATA;
	if ((meta[8U] & 0x08U) == 0x08U) {
		track = float((meta[11U] << 8) + (meta[12U] << 0));
		speed = float(meta[13U]);
	}

	char output[300U];
	::sprintf(output, "%s>APDPRS,M17*,qAR,%s:!%07.2f%c%c%08.2f%c%c",
		lsf.getSource().c_str(), m_callsign.c_str(), latitude, north, sym1, longitude, east, sym2);

	if (track != INVALID_GPS_DATA && speed != INVALID_GPS_DATA && speed > 0.0F)
		::sprintf(output + ::strlen(output), "%03.0f/%03.0f", track, speed);

	if (altitude != INVALID_GPS_DATA)
		::sprintf(output + ::strlen(output), "/A=%06.0f", altitude);

	::sprintf(output + ::strlen(output), " %s\r\n", text.c_str());

	LogDebug("APRS ==> %s", output);

	m_writer->write(output);
}

