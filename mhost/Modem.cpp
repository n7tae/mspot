/*
 *   Copyright (C) 2011-2018,2020,2021 by Jonathan Naylor G4KLX
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


#include "M17Defines.h"
#include "Modem.h"
#include "Utils.h"
#include "Log.h"

#include <cmath>
#include <cstdio>
#include <cassert>
#include <cstdint>
#include <ctime>
#include <thread>
#include <unistd.h>

const unsigned char MMDVM_FRAME_START = 0xE0U;

const unsigned char MMDVM_GET_VERSION = 0x00U;
const unsigned char MMDVM_GET_STATUS  = 0x01U;
const unsigned char MMDVM_SET_CONFIG  = 0x02U;
const unsigned char MMDVM_SET_MODE    = 0x03U;
const unsigned char MMDVM_SET_FREQ    = 0x04U;

const unsigned char MMDVM_M17_LINK_SETUP = 0x45U;
const unsigned char MMDVM_M17_STREAM     = 0x46U;
const unsigned char MMDVM_M17_PACKET     = 0x47U;
const unsigned char MMDVM_M17_LOST       = 0x48U;
const unsigned char MMDVM_M17_EOT        = 0x49U;

const unsigned char MMDVM_ACK         = 0x70U;
const unsigned char MMDVM_NAK         = 0x7FU;

const unsigned char MMDVM_DEBUG1      = 0xF1U;
const unsigned char MMDVM_DEBUG2      = 0xF2U;
const unsigned char MMDVM_DEBUG3      = 0xF3U;
const unsigned char MMDVM_DEBUG4      = 0xF4U;
const unsigned char MMDVM_DEBUG5      = 0xF5U;
const unsigned char MMDVM_DEBUG_DUMP  = 0xFAU;

const unsigned int MAX_RESPONSES = 30U;

const unsigned int BUFFER_LENGTH = 2000U;

const unsigned char CAP1_M17    = 0x20U;


CModem::CModem(bool duplex, bool rxInvert, bool txInvert, bool pttInvert, unsigned int txDelay, bool trace, bool debug) :
m_protocolVersion(0U),
m_m17TXHang(5U),
m_duplex(duplex),
m_rxInvert(rxInvert),
m_txInvert(txInvert),
m_pttInvert(pttInvert),
m_txDelay(txDelay),
m_rxLevel(0.0F),
m_m17TXLevel(0.0F),
m_rfLevel(0.0F),
m_trace(trace),
m_debug(debug),
m_rxFrequency(0U),
m_txFrequency(0U),
m_rxDCOffset(0),
m_txDCOffset(0),
m_port(NULL),
m_buffer(NULL),
m_length(0U),
m_offset(0U),
m_state(SS_START),
m_type(0U),
m_rxM17Data(1000U, "Modem RX M17"),
m_txM17Data(1000U, "Modem TX M17"),
m_statusTimer(1000U, 0U, 250U),
m_inactivityTimer(1000U, 2U),
m_playoutTimer(1000U, 0U, 10U),
m_m17Space(0U),
m_tx(false),
m_cd(false),
m_error(false),
m_mode(MODE_IDLE),
m_hwType(HWT_UNKNOWN),
m_capabilities1(0x00U),
m_capabilities2(0x00U)
{
	m_buffer = new unsigned char[BUFFER_LENGTH];
}

CModem::~CModem()
{
	delete   m_port;
	delete[] m_buffer;
}

void CModem::setPort(IModemPort* port)
{
	assert(port != NULL);

	m_port = port;
}

void CModem::setRFParams(unsigned int rxFrequency, int rxOffset, unsigned int txFrequency, int txOffset, int txDCOffset, int rxDCOffset, float rfLevel)
{
	m_rxFrequency     = rxFrequency + rxOffset;
	m_txFrequency     = txFrequency + txOffset;
	m_txDCOffset      = txDCOffset;
	m_rxDCOffset      = rxDCOffset;
	m_rfLevel         = rfLevel;
}

void CModem::setLevels(float rxLevel, float m17TXLevel)
{
	m_rxLevel       = rxLevel;
	m_m17TXLevel    = m17TXLevel;
}

void CModem::setM17Params(unsigned int txHang)
{
	m_m17TXHang = txHang;
}

bool CModem::open()
{
	::LogMessage("Opening the MMDVM");

	bool ret = m_port->open();
	if (!ret)
		return false;

	ret = readVersion();
	if (!ret) {
		m_port->close();
		delete m_port;
		m_port = NULL;
		return false;
	} else {
		/* Stopping the inactivity timer here when a firmware version has been
		   successfuly read prevents the death spiral of "no reply from modem..." */
		m_inactivityTimer.stop();
	}

	ret = setFrequency();
	if (!ret) {
		m_port->close();
		delete m_port;
		m_port = NULL;
		return false;
	}

	ret = writeConfig();
	if (!ret) {
		m_port->close();
		delete m_port;
		m_port = NULL;
		return false;
	}

	m_statusTimer.start();

	m_error  = false;
	m_offset = 0U;

	return true;
}

void CModem::clock(unsigned int ms)
{
	assert(m_port != NULL);

	// Poll the modem status every 250ms
	m_statusTimer.clock(ms);
	if (m_statusTimer.hasExpired()) {
		readStatus();
		m_statusTimer.start();
	}

	m_inactivityTimer.clock(ms);
	if (m_inactivityTimer.hasExpired()) {
		LogError("No reply from the modem for some time, resetting it");
		m_error = true;
		close();

		std::this_thread::sleep_for(std::chrono::seconds(2));
		while (!open())
			std::this_thread::sleep_for(std::chrono::seconds(5));
	}

	RESP_TYPE_MMDVM type = getResponse();

	if (type == RTM_TIMEOUT) {
		// Nothing to do
	} else if (type == RTM_ERROR) {
		// Nothing to do
	} else {
		// type == RTM_OK
		switch (m_type) {
			case MMDVM_M17_LINK_SETUP: {
				if (m_trace)
					CUtils::dump(1U, "RX M17 Link Setup", m_buffer, m_length);

				unsigned char data = m_length - 2U;
				m_rxM17Data.addData(&data, 1U);

				data = TAG_HEADER;
				m_rxM17Data.addData(&data, 1U);

				m_rxM17Data.addData(m_buffer + 3U, m_length - 3U);
			}
			break;

			case MMDVM_M17_STREAM: {
				if (m_trace)
					CUtils::dump(1U, "RX M17 Stream Data", m_buffer, m_length);

				unsigned char data = m_length - 2U;
				m_rxM17Data.addData(&data, 1U);

				data = TAG_DATA;
				m_rxM17Data.addData(&data, 1U);

				m_rxM17Data.addData(m_buffer + 3U, m_length - 3U);
			}
			break;

			case MMDVM_M17_EOT: {
				if (m_trace)
					CUtils::dump(1U, "RX M17 EOT", m_buffer, m_length);

				unsigned char data = 1U;
				m_rxM17Data.addData(&data, 1U);

				data = TAG_EOT;
				m_rxM17Data.addData(&data, 1U);
			}
			break;

			case MMDVM_M17_LOST: {
				if (m_trace)
					CUtils::dump(1U, "RX M17 Lost", m_buffer, m_length);

				unsigned char data = 1U;
				m_rxM17Data.addData(&data, 1U);

				data = TAG_LOST;
				m_rxM17Data.addData(&data, 1U);
			}
			break;

			case MMDVM_GET_STATUS:
				// if (m_trace)
				//	CUtils::dump(1U, "GET_STATUS", m_buffer, m_length);

				switch (m_protocolVersion) {
				case 1U: {
						m_mode = m_buffer[m_offset + 1U];

						m_tx = (m_buffer[m_offset + 2U] & 0x01U) == 0x01U;
						bool adcOverflow = (m_buffer[m_offset + 2U] & 0x02U) == 0x02U;
						if (adcOverflow)
							LogError("MMDVM ADC levels have overflowed");
						bool rxOverflow = (m_buffer[m_offset + 2U] & 0x04U) == 0x04U;
						if (rxOverflow)
							LogError("MMDVM RX buffer has overflowed");
						bool txOverflow = (m_buffer[m_offset + 2U] & 0x08U) == 0x08U;
						if (txOverflow)
							LogError("MMDVM TX buffer has overflowed");
						m_lockout = (m_buffer[m_offset + 2U] & 0x10U) == 0x10U;
						bool dacOverflow = (m_buffer[m_offset + 2U] & 0x20U) == 0x20U;
						if (dacOverflow)
							LogError("MMDVM DAC levels have overflowed");
						m_cd = (m_buffer[m_offset + 2U] & 0x40U) == 0x40U;

						m_m17Space    = 0U;

						// The following depend on the version of the firmware
						if (m_length > (m_offset + 10U))
							m_m17Space    = m_buffer[m_offset + 10U];
					}
					break;

				case 2U: {
						m_mode = m_buffer[m_offset + 0U];

						m_tx = (m_buffer[m_offset + 1U] & 0x01U) == 0x01U;
						bool adcOverflow = (m_buffer[m_offset + 1U] & 0x02U) == 0x02U;
						if (adcOverflow)
							LogError("MMDVM ADC levels have overflowed");
						bool rxOverflow = (m_buffer[m_offset + 1U] & 0x04U) == 0x04U;
						if (rxOverflow)
							LogError("MMDVM RX buffer has overflowed");
						bool txOverflow = (m_buffer[m_offset + 1U] & 0x08U) == 0x08U;
						if (txOverflow)
							LogError("MMDVM TX buffer has overflowed");
						m_lockout = (m_buffer[m_offset + 1U] & 0x10U) == 0x10U;
						bool dacOverflow = (m_buffer[m_offset + 1U] & 0x20U) == 0x20U;
						if (dacOverflow)
							LogError("MMDVM DAC levels have overflowed");
						m_cd = (m_buffer[m_offset + 1U] & 0x40U) == 0x40U;

						m_m17Space    = m_buffer[m_offset + 9U];
					}
					break;

				default:
					m_m17Space    = 0U;
					break;
				}

				m_inactivityTimer.start();
				// LogMessage("status=%02X, tx=%d, space=%u,%u,%u,%u,%u,%u,%u,%u,%u,%u lockout=%d, cd=%d", m_buffer[m_offset + 2U], int(m_tx), m_dstarSpace, m_dmrSpace1, m_dmrSpace2, m_ysfSpace, m_p25Space, m_nxdnSpace, m_m17Space, m_pocsagSpace, m_fmSpace, m_ax25Space, int(m_lockout), int(m_cd));
				break;

			// These should not be received, but don't complain if we do
			case MMDVM_GET_VERSION:
			case MMDVM_ACK:
				break;

			case MMDVM_NAK:
				LogWarning("Received a NAK from the MMDVM, command = 0x%02X, reason = %u", m_buffer[m_offset], m_buffer[m_offset + 1U]);
				break;

			case MMDVM_DEBUG1:
			case MMDVM_DEBUG2:
			case MMDVM_DEBUG3:
			case MMDVM_DEBUG4:
			case MMDVM_DEBUG5:
			case MMDVM_DEBUG_DUMP:
				printDebug();
				break;

			default:
				LogMessage("Unknown message, type: %02X", m_type);
				CUtils::dump("Buffer dump", m_buffer, m_length);
				break;
		}
	}

	// Only feed data to the modem if the playout timer has expired
	m_playoutTimer.clock(ms);
	if (!m_playoutTimer.hasExpired())
		return;

	if (m_m17Space > 1U && !m_txM17Data.isEmpty()) {
		unsigned char len = 0U;
		m_txM17Data.getData(&len, 1U);
		m_txM17Data.getData(m_buffer, len);

		if (m_trace) {
			switch (m_buffer[2U]) {
			case MMDVM_M17_LINK_SETUP:
				CUtils::dump(1U, "TX M17 Link Setup", m_buffer, len);
				break;
			case MMDVM_M17_STREAM:
				CUtils::dump(1U, "TX M17 Stream Data", m_buffer, len);
				break;
			case MMDVM_M17_EOT:
				CUtils::dump(1U, "TX M17 EOT", m_buffer, len);
				break;
			}
		}

		int ret = m_port->write(m_buffer, len);
		if (ret != int(len))
			LogWarning("Error when writing M17 data to the MMDVM");

		m_playoutTimer.start();

		m_m17Space--;
	}
}

void CModem::close()
{
	assert(m_port != NULL);

	::LogMessage("Closing the MMDVM");

	m_port->close();
}

unsigned int CModem::readM17Data(unsigned char* data)
{
	assert(data != NULL);

	if (m_rxM17Data.isEmpty())
		return 0U;

	unsigned char len = 0U;
	m_rxM17Data.getData(&len, 1U);
	m_rxM17Data.getData(data, len);

	return len;
}

bool CModem::hasM17Space() const
{
	unsigned int space = m_txM17Data.freeSpace() / (M17_FRAME_LENGTH_BYTES + 4U);

	return space > 1U;
}

bool CModem::writeM17Data(const unsigned char* data, unsigned int length)
{
	assert(data != NULL);
	assert(length > 0U);

	unsigned char buffer[130U];

	buffer[0U] = MMDVM_FRAME_START;
	buffer[1U] = length + 2U;

	switch (data[0U]) {
		case TAG_HEADER:
			buffer[2U] = MMDVM_M17_LINK_SETUP;
			::memcpy(buffer + 3U, data + 1U, length - 1U);
			break;
		case TAG_DATA:
			buffer[2U] = MMDVM_M17_STREAM;
			::memcpy(buffer + 3U, data + 1U, length - 1U);
			break;
		case TAG_EOT:
			buffer[2U] = MMDVM_M17_EOT;
			::memcpy(buffer + 3U, data + 1U, length - 1U);
			break;
		default:
			return false;
	}

	unsigned char len = length + 2U;
	m_txM17Data.addData(&len, 1U);
	m_txM17Data.addData(buffer, len);

	return true;
}

bool CModem::hasTX() const
{
	return m_tx;
}

bool CModem::hasCD() const
{
	return m_cd;
}

bool CModem::hasLockout() const
{
	return m_lockout;
}

bool CModem::hasError() const
{
	return m_error;
}

bool CModem::hasM17() const
{
	return (m_capabilities1 & CAP1_M17) == CAP1_M17;
}

unsigned int CModem::getVersion() const
{
	return m_protocolVersion;
}

bool CModem::readVersion()
{
	assert(m_port != NULL);

	std::this_thread::sleep_for(std::chrono::seconds(2));

	for (unsigned int i = 0U; i < 6U; i++) {
		unsigned char buffer[3U];

		buffer[0U] = MMDVM_FRAME_START;
		buffer[1U] = 3U;
		buffer[2U] = MMDVM_GET_VERSION;

		// CUtils::dump(1U, "Written", buffer, 3U);

		int ret = m_port->write(buffer, 3U);
		if (ret != 3)
			return false;

#if defined(__APPLE__)
		m_port->setNonblock(true);
#endif

		for (unsigned int count = 0U; count < MAX_RESPONSES; count++) {
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
			RESP_TYPE_MMDVM resp = getResponse();
			if (resp == RTM_OK && m_buffer[2U] == MMDVM_GET_VERSION) {
				if (::memcmp(m_buffer + 4U, "MMDVM ", 6U) == 0)
					m_hwType = HWT_MMDVM;
				else if (::memcmp(m_buffer + 23U, "MMDVM ", 6U) == 0)
					m_hwType = HWT_MMDVM;
				else if (::memcmp(m_buffer + 4U, "DVMEGA", 6U) == 0)
					m_hwType = HWT_DVMEGA;
				else if (::memcmp(m_buffer + 4U, "ZUMspot", 7U) == 0)
					m_hwType = HWT_MMDVM_ZUMSPOT;
				else if (::memcmp(m_buffer + 4U, "MMDVM_HS_Hat", 12U) == 0)
					m_hwType = HWT_MMDVM_HS_HAT;
				else if (::memcmp(m_buffer + 4U, "MMDVM_HS_Dual_Hat", 17U) == 0)
					m_hwType = HWT_MMDVM_HS_DUAL_HAT;
				else if (::memcmp(m_buffer + 4U, "Nano_hotSPOT", 12U) == 0)
					m_hwType = HWT_NANO_HOTSPOT;
				else if (::memcmp(m_buffer + 4U, "Nano_DV", 7U) == 0)
					m_hwType = HWT_NANO_DV;
				else if (::memcmp(m_buffer + 4U, "D2RG_MMDVM_HS", 13U) == 0)
					m_hwType = HWT_D2RG_MMDVM_HS;
				else if (::memcmp(m_buffer + 4U, "MMDVM_HS-", 9U) == 0)
					m_hwType = HWT_MMDVM_HS;
				else if (::memcmp(m_buffer + 4U, "OpenGD77_HS", 11U) == 0)
					m_hwType = HWT_OPENGD77_HS;
				else if (::memcmp(m_buffer + 4U, "SkyBridge", 9U) == 0)
					m_hwType = HWT_SKYBRIDGE;

				m_protocolVersion = m_buffer[3U];

				switch (m_protocolVersion) {
				case 1U:
					LogInfo("MMDVM protocol version: 1, description: %.*s", m_length - 4U, m_buffer + 4U);
					m_capabilities1 = CAP1_M17;
					m_capabilities2 = 0;
					return true;

				case 2U:
					LogInfo("MMDVM protocol version: 2, description: %.*s", m_length - 23U, m_buffer + 23U);
					switch (m_buffer[6U]) {
					case 0U:
						LogInfo("CPU: Atmel ARM, UDID: %02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X", m_buffer[7U], m_buffer[8U], m_buffer[9U], m_buffer[10U], m_buffer[11U], m_buffer[12U], m_buffer[13U], m_buffer[14U], m_buffer[15U], m_buffer[16U], m_buffer[17U], m_buffer[18U], m_buffer[19U], m_buffer[20U], m_buffer[21U], m_buffer[22U]);
						break;
					case 1U:
						LogInfo("CPU: NXP ARM, UDID: %02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X", m_buffer[7U], m_buffer[8U], m_buffer[9U], m_buffer[10U], m_buffer[11U], m_buffer[12U], m_buffer[13U], m_buffer[14U], m_buffer[15U], m_buffer[16U], m_buffer[17U], m_buffer[18U], m_buffer[19U], m_buffer[20U], m_buffer[21U], m_buffer[22U]);
						break;
					case 2U:
						LogInfo("CPU: ST-Micro ARM, UDID: %02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X", m_buffer[7U], m_buffer[8U], m_buffer[9U], m_buffer[10U], m_buffer[11U], m_buffer[12U], m_buffer[13U], m_buffer[14U], m_buffer[15U], m_buffer[16U], m_buffer[17U], m_buffer[18U]);
						break;
					default:
						LogInfo("CPU: Unknown type: %u", m_buffer[6U]);
						break;
					}
					m_capabilities1 = m_buffer[4U];
					m_capabilities2 = m_buffer[5U];
					char modeText[100U];
					::strcpy(modeText, "Modes:");
					if (hasM17())
						::strcat(modeText, " M17");
					LogInfo(modeText);
					return true;

				default:
					LogError("MMDVM protocol version: %u, unsupported by this version of the MMDVM Host", m_protocolVersion);
					return false;
				}

				return true;
			}
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1500));
	}

	LogError("Unable to read the firmware version after six attempts");

	return false;
}

bool CModem::readStatus()
{
	assert(m_port != NULL);

	unsigned char buffer[3U];

	buffer[0U] = MMDVM_FRAME_START;
	buffer[1U] = 3U;
	buffer[2U] = MMDVM_GET_STATUS;

	// CUtils::dump(1U, "Written", buffer, 3U);

	return m_port->write(buffer, 3U) == 3;
}

bool CModem::writeConfig()
{
	switch (m_protocolVersion) {
	case 1U:
		return setConfig1();
	case 2U:
		return setConfig2();
	default:
		return false;
	}
}

bool CModem::setConfig1()
{
	assert(m_port != NULL);

	unsigned char buffer[30U] = { 0 };

	buffer[0U] = MMDVM_FRAME_START;

	buffer[1U] = 26U;

	buffer[2U] = MMDVM_SET_CONFIG;

	buffer[3U] = 0x00U;
	if (m_rxInvert)
		buffer[3U] |= 0x01U;
	if (m_txInvert)
		buffer[3U] |= 0x02U;
	if (m_pttInvert)
		buffer[3U] |= 0x04U;
	if (m_debug)
		buffer[3U] |= 0x10U;
	if (!m_duplex)
		buffer[3U] |= 0x80U;

	buffer[4U] = 0x40U;	// M17 only!

	buffer[5U] = m_txDelay / 10U;		// In 10ms units

	buffer[6U] = MODE_IDLE;

	buffer[7U] = (unsigned char)(m_rxLevel * 2.55F + 0.5F);

	buffer[11U] = 128U;           // Was OscOffset

	buffer[16U] = (unsigned char)(m_txDCOffset + 128);
	buffer[17U] = (unsigned char)(m_rxDCOffset + 128);

	buffer[24U] = (unsigned char)(m_m17TXLevel * 2.55F + 0.5F);

	buffer[25U] = (unsigned char)m_m17TXHang;

	// CUtils::dump(1U, "Written", buffer, 26U);

	int ret = m_port->write(buffer, 26U);
	if (ret != 26)
		return false;

	unsigned int count = 0U;
	RESP_TYPE_MMDVM resp;
	do {
		std::this_thread::sleep_for(std::chrono::milliseconds(10));

		resp = getResponse();
		if (resp == RTM_OK && m_buffer[2U] != MMDVM_ACK && m_buffer[2U] != MMDVM_NAK) {
			count++;
			if (count >= MAX_RESPONSES) {
				LogError("The MMDVM is not responding to the SET_CONFIG command");
				return false;
			}
		}
	} while (resp == RTM_OK && m_buffer[2U] != MMDVM_ACK && m_buffer[2U] != MMDVM_NAK);

	// CUtils::dump(1U, "Response", m_buffer, m_length);

	if (resp == RTM_OK && m_buffer[2U] == MMDVM_NAK) {
		LogError("Received a NAK to the SET_CONFIG command from the modem");
		return false;
	}

	m_playoutTimer.start();

	return true;
}

bool CModem::setConfig2()
{
	assert(m_port != NULL);

	unsigned char buffer[50U] = { 0 };

	buffer[0U] = MMDVM_FRAME_START;

	buffer[1U] = 40U;

	buffer[2U] = MMDVM_SET_CONFIG;

	buffer[3U] = 0x00U;
	if (m_rxInvert)
		buffer[3U] |= 0x01U;
	if (m_txInvert)
		buffer[3U] |= 0x02U;
	if (m_pttInvert)
		buffer[3U] |= 0x04U;
	if (m_debug)
		buffer[3U] |= 0x10U;
	if (!m_duplex)
		buffer[3U] |= 0x80U;

	buffer[4U] = 0x40U;	// M17 only!

	buffer[5U] = 0x00U;

	buffer[6U] = m_txDelay / 10U;		// In 10ms units

	buffer[7U] = MODE_IDLE;

	buffer[8U] = (unsigned char)(m_txDCOffset + 128);
	buffer[9U] = (unsigned char)(m_rxDCOffset + 128);

	buffer[10U] = (unsigned char)(m_rxLevel * 2.55F + 0.5F);


	buffer[17U] = (unsigned char)(m_m17TXLevel * 2.55F + 0.5F);

	buffer[21U] = 0x00U;
	buffer[22U] = 0x00U;

	buffer[26U] = (unsigned char)m_m17TXHang;
	buffer[27U] = 0x00U;
	buffer[28U] = 0x00U;

	// CUtils::dump(1U, "Written", buffer, 40U);

	int ret = m_port->write(buffer, 40U);
	if (ret != 40)
		return false;

	unsigned int count = 0U;
	RESP_TYPE_MMDVM resp;
	do {
		std::this_thread::sleep_for(std::chrono::milliseconds(10));

		resp = getResponse();
		if (resp == RTM_OK && m_buffer[2U] != MMDVM_ACK && m_buffer[2U] != MMDVM_NAK) {
			count++;
			if (count >= MAX_RESPONSES) {
				LogError("The MMDVM is not responding to the SET_CONFIG command");
				return false;
			}
		}
	} while (resp == RTM_OK && m_buffer[2U] != MMDVM_ACK && m_buffer[2U] != MMDVM_NAK);

	// CUtils::dump(1U, "Response", m_buffer, m_length);

	if (resp == RTM_OK && m_buffer[2U] == MMDVM_NAK) {
		LogError("Received a NAK to the SET_CONFIG command from the modem");
		return false;
	}

	m_playoutTimer.start();

	return true;
}

bool CModem::setFrequency()
{
	assert(m_port != NULL);

	unsigned char buffer[20U];
	unsigned char len;
	unsigned int  pocsagFrequency = 433000000U;

	if (m_hwType == HWT_DVMEGA)
		len = 12U;
	else {
		buffer[12U]  = (unsigned char)(m_rfLevel * 2.55F + 0.5F);

		buffer[13U] = (pocsagFrequency >> 0)  & 0xFFU;
		buffer[14U] = (pocsagFrequency >> 8)  & 0xFFU;
		buffer[15U] = (pocsagFrequency >> 16) & 0xFFU;
		buffer[16U] = (pocsagFrequency >> 24) & 0xFFU;

		len = 17U;
	}

	buffer[0U]  = MMDVM_FRAME_START;

	buffer[1U]  = len;

	buffer[2U]  = MMDVM_SET_FREQ;

	buffer[3U]  = 0x00U;

	buffer[4U]  = (m_rxFrequency >> 0) & 0xFFU;
	buffer[5U]  = (m_rxFrequency >> 8) & 0xFFU;
	buffer[6U]  = (m_rxFrequency >> 16) & 0xFFU;
	buffer[7U]  = (m_rxFrequency >> 24) & 0xFFU;

	buffer[8U]  = (m_txFrequency >> 0) & 0xFFU;
	buffer[9U]  = (m_txFrequency >> 8) & 0xFFU;
	buffer[10U] = (m_txFrequency >> 16) & 0xFFU;
	buffer[11U] = (m_txFrequency >> 24) & 0xFFU;

	// CUtils::dump(1U, "Written", buffer, len);

	int ret = m_port->write(buffer, len);
	if (ret != len)
		return false;

	unsigned int count = 0U;
	RESP_TYPE_MMDVM resp;
	do {
		std::this_thread::sleep_for(std::chrono::milliseconds(10));

		resp = getResponse();
		if (resp == RTM_OK && m_buffer[2U] != MMDVM_ACK && m_buffer[2U] != MMDVM_NAK) {
			count++;
			if (count >= MAX_RESPONSES) {
				LogError("The MMDVM is not responding to the SET_FREQ command");
				return false;
			}
		}
	} while (resp == RTM_OK && m_buffer[2U] != MMDVM_ACK && m_buffer[2U] != MMDVM_NAK);

	// CUtils::dump(1U, "Response", m_buffer, m_length);

	if (resp == RTM_OK && m_buffer[2U] == MMDVM_NAK) {
		LogError("Received a NAK to the SET_FREQ command from the modem");
		return false;
	}

	return true;
}

RESP_TYPE_MMDVM CModem::getResponse()
{
	assert(m_port != NULL);

	if (m_state == SS_START) {
		// Get the start of the frame or nothing at all
		int ret = m_port->read(m_buffer + 0U, 1U);
		if (ret < 0) {
			LogError("Error when reading from the modem");
			return RTM_ERROR;
		}

		if (ret == 0)
			return RTM_TIMEOUT;

		if (m_buffer[0U] != MMDVM_FRAME_START)
			return RTM_TIMEOUT;

		m_state  = SS_LENGTH1;
		m_length = 1U;
	}

	if (m_state == SS_LENGTH1) {
		// Get the length of the frame, 1/2
		int ret = m_port->read(m_buffer + 1U, 1U);
		if (ret < 0) {
			LogError("Error when reading from the modem");
			m_state = SS_START;
			return RTM_ERROR;
		}

		if (ret == 0)
			return RTM_TIMEOUT;

		m_length = m_buffer[1U];
		m_offset = 2U;

		if (m_length == 0U)
			m_state = SS_LENGTH2;
		else
			m_state = SS_TYPE;
	}

	if (m_state == SS_LENGTH2) {
		// Get the length of the frame, 2/2
		int ret = m_port->read(m_buffer + 2U, 1U);
		if (ret < 0) {
			LogError("Error when reading from the modem");
			m_state = SS_START;
			return RTM_ERROR;
		}

		if (ret == 0)
			return RTM_TIMEOUT;

		m_length = m_buffer[2U] + 255U;
		m_offset = 3U;
		m_state  = SS_TYPE;
	}

	if (m_state == SS_TYPE) {
		// Get the frame type
		int ret = m_port->read(&m_type, 1U);
		if (ret < 0) {
			LogError("Error when reading from the modem");
			m_state = SS_START;
			return RTM_ERROR;
		}

		if (ret == 0)
			return RTM_TIMEOUT;

		m_buffer[m_offset++] = m_type;

		m_state = SS_DATA;
	}

	if (m_state == SS_DATA) {
		while (m_offset < m_length) {
			int ret = m_port->read(m_buffer + m_offset, m_length - m_offset);
			if (ret < 0) {
				LogError("Error when reading from the modem");
				m_state = SS_START;
				return RTM_ERROR;
			}

			if (ret == 0)
				return RTM_TIMEOUT;

			if (ret > 0)
				m_offset += ret;
		}
	}

	// CUtils::dump(1U, "Received", m_buffer, m_length);

	m_offset = m_length > 255U ? 4U : 3U;
	m_state  = SS_START;

	return RTM_OK;
}

HW_TYPE CModem::getHWType() const
{
	return m_hwType;
}

unsigned char CModem::getMode() const
{
	return m_mode;
}

bool CModem::setMode(unsigned char mode)
{
	assert(m_port != NULL);

	unsigned char buffer[4U];

	buffer[0U] = MMDVM_FRAME_START;
	buffer[1U] = 4U;
	buffer[2U] = MMDVM_SET_MODE;
	buffer[3U] = mode;

	// CUtils::dump(1U, "Written", buffer, 4U);

	return m_port->write(buffer, 4U) == 4;
}

void CModem::printDebug()
{
	if (m_type == MMDVM_DEBUG1) {
		LogMessage("Debug: %.*s", m_length - m_offset - 0U, m_buffer + m_offset);
	} else if (m_type == MMDVM_DEBUG2) {
		short val1 = (m_buffer[m_length - 2U] << 8) | m_buffer[m_length - 1U];
		LogMessage("Debug: %.*s %d", m_length - m_offset - 2U, m_buffer + m_offset, val1);
	} else if (m_type == MMDVM_DEBUG3) {
		short val1 = (m_buffer[m_length - 4U] << 8) | m_buffer[m_length - 3U];
		short val2 = (m_buffer[m_length - 2U] << 8) | m_buffer[m_length - 1U];
		LogMessage("Debug: %.*s %d %d", m_length - m_offset - 4U, m_buffer + m_offset, val1, val2);
	} else if (m_type == MMDVM_DEBUG4) {
		short val1 = (m_buffer[m_length - 6U] << 8) | m_buffer[m_length - 5U];
		short val2 = (m_buffer[m_length - 4U] << 8) | m_buffer[m_length - 3U];
		short val3 = (m_buffer[m_length - 2U] << 8) | m_buffer[m_length - 1U];
		LogMessage("Debug: %.*s %d %d %d", m_length - m_offset - 6U, m_buffer + m_offset, val1, val2, val3);
	} else if (m_type == MMDVM_DEBUG5) {
		short val1 = (m_buffer[m_length - 8U] << 8) | m_buffer[m_length - 7U];
		short val2 = (m_buffer[m_length - 6U] << 8) | m_buffer[m_length - 5U];
		short val3 = (m_buffer[m_length - 4U] << 8) | m_buffer[m_length - 3U];
		short val4 = (m_buffer[m_length - 2U] << 8) | m_buffer[m_length - 1U];
		LogMessage("Debug: %.*s %d %d %d %d", m_length - m_offset - 8U, m_buffer + m_offset, val1, val2, val3, val4);
	} else if (m_type == MMDVM_DEBUG_DUMP) {
		CUtils::dump(1U, "Debug: Data", m_buffer + m_offset, m_length - m_offset);
	}
}
