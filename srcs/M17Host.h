/*
 *   Copyright (C) 2015-2021 by Jonathan Naylor G4KLX
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

#include <string>
#include <atomic>
#include <memory>

#include "M17Control.h"
#include "M17Network.h"
#include "Timer.h"
#include "Modem.h"

class CM17Host
{
public:
  CM17Host();
  ~CM17Host();

  bool Run();
  void Stop();

private:
  std::unique_ptr<CModem>      m_modem;
  std::unique_ptr<CM17Control> m_m17;
  std::shared_ptr<CM17Network> m_m17Network;
  unsigned char   m_mode;
  unsigned int    m_m17NetModeHang;
  bool            m_duplex;
  unsigned int    m_timeout;
  std::string     m_callsign;
  std::atomic<bool> keep_running;

  void readParams();
  bool createModem();
  bool createM17Network();

  void setMode(uint8_t mode);

};
