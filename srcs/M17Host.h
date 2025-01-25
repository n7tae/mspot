// Copyright (C) 2015-2021 by Jonathan Naylor G4KLX

/*

         mspot - an M17-only HotSpot using an MMDVM device
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

#pragma once

#include <string>
#include <memory>
#include <future>

#include "RSSIInterpolator.h"
#include "M17Gateway.h"
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
	std::unique_ptr<CM17Gateway> m_m17Gateway;
	std::future<void> hostFuture, gateFuture;
	std::unique_ptr<CRSSIInterpolator> m_rssi;
	unsigned char   m_mode;

	CTimer          m_cwIdTimer;
	bool            m_duplex;
	unsigned int    m_timeout;
	unsigned int    m_cwIdTime;
	std::string     m_callsign;
	unsigned int    m_id;
	std::string     m_cwCallsign;

	void Run();
	bool createModem();
	bool createM17Network();

	void setMode(unsigned char mode);
};
