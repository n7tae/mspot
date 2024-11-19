// Copyright (C) 2015-2021 by Jonathan Naylor G4KLX

/****************************************************************
 *                                                              *
 *             More - An M17-only Repeater/HotSpot              *
 *                                                              *
 *         Copyright (c) 2024 by Thomas A. Early N7TAE          *
 *                                                              *
 * See the LICENSE file for details about the software license. *
 *                                                              *
 ****************************************************************/

#pragma once

#include <string>
#include <memory>
#include <future>

#include "M17Control.h"
#include "M17Network.h"
#include "Timer.h"
#include "Modem.h"

class CM17Host
{
public:
	CM17Host();
	~CM17Host();

	bool Start();
	void Stop();

private:
	std::atomic<bool>            keep_running;
	std::unique_ptr<CModem>      m_modem;
	std::unique_ptr<CM17Control> m_m17;
	std::shared_ptr<CM17Network> m_m17Network;
	std::future<void> hostFuture;
	unsigned char   m_mode;
	unsigned int    m_m17RFModeHang;
	unsigned int    m_m17NetModeHang;

	CTimer          m_modeTimer;
	CTimer          m_cwIdTimer;
	bool            m_duplex;
	unsigned int    m_timeout;
	unsigned int    m_cwIdTime;
	std::string     m_callsign;
	unsigned int    m_id;
	std::string     m_cwCallsign;
	bool            m_fixedMode;

	void Run();
	bool createModem();
	bool createM17Network();

	void processModeCommand(unsigned char mode, unsigned int timeout);
	void processEnableCommand(bool& mode, bool enabled);

	void setMode(unsigned char mode);
};
