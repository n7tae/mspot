/*
*   Copyright (C) 2017,2018.2021 by Jonathan Naylor G4KLX
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

#include "Voice.h"
#include "M17Defines.h"
#include "Log.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cassert>
#include <random>

#include <sys/stat.h>

const unsigned int SILENCE_LENGTH = 4U;

const unsigned char BIT_MASK_TABLE[] = { 0x80U, 0x40U, 0x20U, 0x10U, 0x08U, 0x04U, 0x02U, 0x01U };

#define WRITE_BIT1(p,i,b) p[(i)>>3] = (b) ? (p[(i)>>3] | BIT_MASK_TABLE[(i)&7]) : (p[(i)>>3] & ~BIT_MASK_TABLE[(i)&7])
#define READ_BIT1(p,i)    (p[(i)>>3] & BIT_MASK_TABLE[(i)&7])

CVoice::CVoice(const std::string& directory, const std::string& language, const std::string& callsign) :
m_language(language),
m_indxFile(),
m_m17File(),
m_lsf(),
m_status(VS_NONE),
m_timer(1000U, 2U),
m_stopWatch(),
m_sent(0U),
m_m17(NULL),
m_voiceData(NULL),
m_voiceLength(0U),
m_positions(),
m_metaArray(),
m_itMeta()
{
	assert(!directory.empty());
	assert(!language.empty());

	m_indxFile = directory + "/" + language + ".indx";
	m_m17File  = directory + "/" + language + ".m17";

	// 15s of audio maximum
	m_voiceData = new unsigned char[15U * 25U * M17_NETWORK_FRAME_LENGTH];

	m_lsf.setSource(callsign);
	m_lsf.setDest("INFO");
	m_lsf.setPacketStream(M17_STREAM_TYPE);
	m_lsf.setDataType(M17_DATA_TYPE_VOICE);
	m_lsf.setEncryptionType(M17_ENCRYPTION_TYPE_NONE);
	m_lsf.setEncryptionSubType(M17_ENCRYPTION_SUB_TYPE_TEXT);
	m_lsf.setMeta(M17_NULL_NONCE);
	m_lsf.setCAN(0U);
}

CVoice::~CVoice()
{
	for (std::unordered_map<std::string, CPositions*>::iterator it = m_positions.begin(); it != m_positions.end(); ++it)
		delete it->second;

	m_positions.clear();

	delete[] m_m17;
	delete[] m_voiceData;
}

bool CVoice::open()
{
	FILE* fpindx = ::fopen(m_indxFile.c_str(), "rt");
	if (fpindx == NULL) {
		LogError("Unable to open the index file - %s", m_indxFile.c_str());
		return false;
	}

	struct stat statStruct;
	int ret = ::stat(m_m17File.c_str(), &statStruct);
	if (ret != 0) {
		LogError("Unable to stat the M17 file - %s", m_m17File.c_str());
		::fclose(fpindx);
		return false;
	}

	FILE* fpm17 = ::fopen(m_m17File.c_str(), "rb");
	if (fpm17 == NULL) {
		LogError("Unable to open the M17 file - %s", m_m17File.c_str());
		::fclose(fpindx);
		return false;
	}

	m_m17 = new unsigned char[statStruct.st_size];

	size_t sizeRead = ::fread(m_m17, 1U, statStruct.st_size, fpm17);
	if (sizeRead != 0U) {
		char buffer[80U];
		while (::fgets(buffer, 80, fpindx) != NULL) {
			char* p1 = ::strtok(buffer, "\t\r\n");
			char* p2 = ::strtok(NULL, "\t\r\n");
			char* p3 = ::strtok(NULL, "\t\r\n");

			if (p1 != NULL && p2 != NULL && p3 != NULL) {
				std::string symbol  = std::string(p1);
				unsigned int start  = ::atoi(p2) * M17_3200_LENGTH_BYTES;
				unsigned int length = (::atoi(p3) + 1U) / 2U;

				CPositions* pos = new CPositions;
				pos->m_start = start;
				pos->m_length = length;

				m_positions[symbol] = pos;
			}
		}
	}

	::fclose(fpindx);
	::fclose(fpm17);

	LogInfo("Loaded the audio and index file for %s", m_language.c_str());

	return true;
}

void CVoice::linkedTo(const std::string& reflector)
{
	std::vector<std::string> words;
	if (m_positions.count("linkedto") == 0U) {
		words.push_back("linked");
		words.push_back("2");
	} else {
		words.push_back("linkedto");
	}

	// Remove the "M17-" prefix of the reflector name
	std::string name = reflector;
	name.erase(0, 4);

	std::string::const_iterator it = name.cbegin();
	while (it != name.cend()) {
		if (*it == ' ') {
			++it;
			break;
		}

		words.push_back(std::string(1U, *it));
		++it;
	}

	if (it != name.cend()) {
		switch (*it) {
		case 'A':
			words.push_back("alpha");
			break;
		case 'B':
			words.push_back("bravo");
			break;
		case 'C':
			words.push_back("charlie");
			break;
		case 'D':
			words.push_back("delta");
			break;
		default:
			words.push_back(std::string(1U, *it));
			break;
		}
	}

	char text[50U];
	if (m_language == "de_DE")
		::sprintf(text, "Verlinkt zu %s", reflector.c_str());
	else if (m_language == "dk_DK")
		::sprintf(text, "Linket til %s", reflector.c_str());
	else if (m_language == "es_ES")
		::sprintf(text, "Enlazado %s", reflector.c_str());
	else if (m_language == "fr_FR")
		::sprintf(text, "Connecte a %s", reflector.c_str());
	else if (m_language == "it_IT")
		::sprintf(text, "Connesso a %s", reflector.c_str());
	else if (m_language == "pl_PL")
		::sprintf(text, "Polaczony z %s", reflector.c_str());
	else if (m_language == "se_SE")
		::sprintf(text, "Lankad till %s", reflector.c_str());
	else
		::sprintf(text, "Linked to %s", reflector.c_str());

	createVoice(words, text);
}

void CVoice::unlinked()
{
	std::vector<std::string> words;
	words.push_back("notlinked");

	const char* text;
	if (m_language == "de_DE")
		text = "Nicht verbunden";
	else if (m_language == "dk_DK")
		text = "Ikke forbundet";
	else if (m_language == "es_ES")
		text = "No enlazado";
	else if (m_language == "fr_FR")
		text = "Non connecte";
	else if (m_language == "it_IT")
		text = "Non connesso";
	else if (m_language == "pl_PL")
		text = "Nie polaczony";
	else if (m_language == "se_SE")
		text = "Ej lankad";
	else
		text = "Not linked";

	createVoice(words, text);
}

void CVoice::createVoice(const std::vector<std::string>& words, const char* text)
{
	assert(text != NULL);

	size_t textSize = ::strlen(text);
	unsigned char count = textSize / (M17_META_LENGTH_BYTES - 1U);
	if ((textSize % (M17_META_LENGTH_BYTES - 1U)) > 0U)
		count++;

	if (count > 4U)
		count = 4U;

	unsigned char bitMap = 0U;
	if (count == 1U)
		bitMap = 0x10U;
	else if (count == 2U)
		bitMap = 0x30U;
	else if (count == 3U)
		bitMap = 0x70U;
	else
		bitMap = 0xF0U;

	for (unsigned char n = 0U; n < count; n++) {
		unsigned char* meta = new unsigned char[M17_META_LENGTH_BYTES];
		::memset(meta, ' ', M17_META_LENGTH_BYTES);

		meta[0U] = (0x01U << n) | bitMap;

		const char* p = text + n * (M17_META_LENGTH_BYTES - 1U);
		size_t textSize = ::strlen(p);
		if (textSize < (M17_META_LENGTH_BYTES - 1U))
			::memcpy(meta + 1U, p, textSize);
		else
			::memcpy(meta + 1U, p, M17_META_LENGTH_BYTES - 1U);

		m_metaArray.push_back(meta);
	}

	m_itMeta = m_metaArray.cbegin();
	m_lsf.setMeta(*m_itMeta);

	m_voiceLength = 0U;

	unsigned int m17Length = 0U;
	for (std::vector<std::string>::const_iterator it = words.begin(); it != words.end(); ++it) {
		if (m_positions.count(*it) > 0U) {
			CPositions* position = m_positions.at(*it);
			m17Length += position->m_length;
		} else {
			LogWarning("Unable to find character/phrase \"%s\" in the index", (*it).c_str());
		}
	}

	// Ensure that the Codec 2 audio is an integer number of M17 frames
	if ((m17Length % 2U) != 0U)
		m17Length++;

	// Add space for silence before and after the voice
	m17Length += SILENCE_LENGTH;
	m17Length += SILENCE_LENGTH;

	// Create a random id for this transmission if needed
	std::random_device rd;
	std::mt19937 mt(rd());
	std::uniform_int_distribution<uint16_t> dist(0x0001, 0xFFFE);
	uint16_t id = dist(mt);

	uint16_t fn = 0U;

	// Start with silence
	for (unsigned int i = 0U; i < SILENCE_LENGTH; i++)
		createFrame(id, fn, M17_3200_SILENCE, 1U, false);

	for (std::vector<std::string>::const_iterator it = words.begin(); it != words.end(); ++it) {
		if (m_positions.count(*it) > 0U) {
			CPositions* position = m_positions.at(*it);
			unsigned int start  = position->m_start;
			unsigned int length = position->m_length;
			createFrame(id, fn, m_m17 + start, length, false);
		}
	}

	// End with silence
	for (unsigned int i = 0U; i < (SILENCE_LENGTH - 1U); i++)
		createFrame(id, fn, M17_3200_SILENCE, 1U, false);

	createFrame(id, fn, M17_3200_SILENCE, 1U, true);

	for (std::vector<const unsigned char*>::iterator it = m_metaArray.begin(); it != m_metaArray.end(); ++it)
		delete *it;
	m_metaArray.clear();
}

bool CVoice::read(unsigned char* data)
{
	assert(data != NULL);

	if (m_status != VS_SENDING)
		return false;

	unsigned int count = m_stopWatch.elapsed() / M17_FRAME_TIME;

	if (m_sent < count) {
		unsigned int offset = m_sent * M17_NETWORK_FRAME_LENGTH;
		::memcpy(data, m_voiceData + offset, M17_NETWORK_FRAME_LENGTH);

		offset += M17_NETWORK_FRAME_LENGTH;
		m_sent++;

		if (offset >= m_voiceLength) {
			m_timer.stop();
			m_status = VS_NONE;
		}

		return true;
	}

	return false;
}

void CVoice::eof()
{
	if (m_voiceLength == 0U)
		return;

	m_status = VS_WAITING;

	m_timer.start();
}

void CVoice::clock(unsigned int ms)
{
	m_timer.clock(ms);
	if (m_timer.isRunning() && m_timer.hasExpired()) {
		if (m_status == VS_WAITING) {
			m_stopWatch.start();
			m_status = VS_SENDING;
			m_sent = 0U;
		}
	}
}

void CVoice::createFrame(uint16_t id, uint16_t& fn, const unsigned char* audio, unsigned int length, bool end)
{
	assert(audio != NULL);
	assert(length > 0U);

	for (unsigned int i = 0U; i < length; i++) {
		// Create an M17 network frame
		unsigned char frame[M17_NETWORK_FRAME_LENGTH];
		frame[0U] = 'M';
		frame[1U] = '1';
		frame[2U] = '7';
		frame[3U] = ' ';

		frame[4U] = id / 256U;	// Unique session id
		frame[5U] = id % 256U;

		m_lsf.getNetwork(frame + 6U);

		frame[34U] = (fn >> 8) & 0xFFU;
		frame[35U] = (fn >> 0) & 0xFFU;
		if (end)
			frame[34U] |= 0x80U;
		fn++;
		if ((fn % 6U) == 0U) {
			++m_itMeta;
			if (m_itMeta == m_metaArray.cend())
				m_itMeta = m_metaArray.cbegin();

			m_lsf.setMeta(*m_itMeta);
		}

		::memcpy(frame + 36U, audio, M17_PAYLOAD_LENGTH_BYTES);
		audio += M17_PAYLOAD_LENGTH_BYTES;

		// Dummy CRC
		frame[52U] = 0x00U;
		frame[53U] = 0x00U;

		::memcpy(m_voiceData + m_voiceLength, frame, M17_NETWORK_FRAME_LENGTH);
		m_voiceLength += M17_NETWORK_FRAME_LENGTH;
	}
}
