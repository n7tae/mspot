/*
 *   Copyright (C) 2011-2018,2020,2021 by Jonathan Naylor G4KLX
 *   Copyright (C) 2024 by Thomas A. Early N7TAE
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

#pragma once

#include <memory>

#include "SerialPort.h"
#include "RingBuffer.h"
#include "Defines.h"
#include "Timer.h"

#include <string>

enum RESP_TYPE_MMDVM
{
	RTM_OK,
	RTM_TIMEOUT,
	RTM_ERROR
};

enum SERIAL_STATE
{
	SS_START,
	SS_LENGTH1,
	SS_LENGTH2,
	SS_TYPE,
	SS_DATA
};

class CModem
{
public:
	CModem(bool duplex, bool rxInvert, bool txInvert, bool pttInvert, unsigned int txDelay, bool trace, bool debug);
	~CModem();

	void setPort(std::unique_ptr<ISerialPort> port);
	void setRFParams(unsigned rxFrequency, int rxOffset, unsigned txFrequency, int txOffset, int txDCOffset, int rxDCOffset, unsigned rfLevel);
	void setLevels(unsigned rxLevel, unsigned m17TXLevel);
	void setM17Params(unsigned txHang);

	bool open();

	bool hasM17() const;

	unsigned int getVersion() const;

	unsigned int readM17Data(unsigned char* data);

	bool hasM17Space() const;

	bool hasTX() const;
	bool hasCD() const;

	bool hasLockout() const;
	bool hasError() const;

	bool writeConfig();
	bool writeM17Data(const unsigned char* data, unsigned int length);

	unsigned char getMode() const;
	bool setMode(unsigned char mode);

	HW_TYPE getHWType() const;

	void clock(unsigned int ms);

	void close();

private:
	unsigned                   m_protocolVersion;
	unsigned                   m_txHang;
	bool                       m_duplex;
	bool                       m_rxInvert;
	bool                       m_txInvert;
	bool                       m_pttInvert;
	unsigned                   m_txDelay;
	unsigned                   m_rxLevel;
	unsigned                   m_txLevel;
	unsigned                   m_rfLevel;
	bool                       m_trace;
	bool                       m_debug;
	unsigned                   m_rxFrequency;
	unsigned                   m_txFrequency;
	int                        m_rxDCOffset;
	int                        m_txDCOffset;
	std::unique_ptr<ISerialPort> m_port;
	uint8_t                   *m_buffer;
	unsigned int               m_length;
	unsigned int               m_offset;
	SERIAL_STATE               m_state;
	unsigned char              m_type;
	CRingBuffer<unsigned char> m_rxM17Data;
	CRingBuffer<unsigned char> m_txM17Data;
	CTimer                     m_statusTimer;
	CTimer                     m_inactivityTimer;
	CTimer                     m_playoutTimer;
	unsigned                   m_m17Space;
	bool                       m_tx;
	bool                       m_cd;
	bool                       m_lockout;
	bool                       m_error;
	unsigned char              m_mode;
	HW_TYPE                    m_hwType;
	unsigned char              m_capabilities1;
	unsigned char              m_capabilities2;

	bool readVersion();
	bool readStatus();
	bool setConfig1();
	bool setConfig2();
	bool setFrequency();

	void printDebug();

	RESP_TYPE_MMDVM getResponse();
};
