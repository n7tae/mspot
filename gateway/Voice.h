/*
*   Copyright (C) 2017,2018,2021 by Jonathan Naylor G4KLX
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

#if !defined(Voice_H)
#define	Voice_H

#include "StopWatch.h"
#include "M17LSF.h"
#include "Timer.h"

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

enum VOICE_STATUS {
	VS_NONE,
	VS_WAITING,
	VS_SENDING
};

struct CPositions {
	unsigned int m_start;
	unsigned int m_length;
};

class CVoice {
public:
	CVoice(const std::string& directory, const std::string& language, const std::string& callsign);
	~CVoice();

	bool open();

	void linkedTo(const std::string& reflector);
	void unlinked();

	bool read(unsigned char* data);

	void eof();

	void clock(unsigned int ms);

private:
	std::string                            m_language;
	std::string                            m_indxFile;
	std::string                            m_m17File;
	CM17LSF                                m_lsf;
	VOICE_STATUS                           m_status;
	CTimer                                 m_timer;
	CStopWatch                             m_stopWatch;
	unsigned int                           m_sent;
	unsigned char*                         m_m17;
	unsigned char*                         m_voiceData;
	unsigned int                           m_voiceLength;
	std::unordered_map<std::string, CPositions*> m_positions;
	std::vector<const unsigned char*>      m_metaArray;
	std::vector<const unsigned char*>::const_iterator m_itMeta;

	void createVoice(const std::vector<std::string>& words, const char* text);
	void createFrame(uint16_t id, uint16_t& fn, const unsigned char* audio, unsigned int length, bool end);
};

#endif
