/*
 *   Copyright (C) 2015,2016,2018,2020,2021 by Jonathan Naylor G4KLX
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

#include "Sync.h"

#include "M17Defines.h"

#include <cstdio>
#include <cassert>
#include <cstring>


void CSync::addM17LinkSetupSync(unsigned char* data)
{
	assert(data != NULL);

	::memcpy(data, M17_LINK_SETUP_SYNC_BYTES, M17_SYNC_LENGTH_BYTES);
}

void CSync::addM17StreamSync(unsigned char* data)
{
	assert(data != NULL);

	::memcpy(data, M17_STREAM_SYNC_BYTES, M17_SYNC_LENGTH_BYTES);
}

void CSync::addM17EOTSync(unsigned char* data)
{
	assert(data != NULL);

	::memcpy(data, M17_EOT_SYNC_BYTES, M17_SYNC_LENGTH_BYTES);
}
