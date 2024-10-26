//  Copyright (C) 2015-2021 by Jonathan Naylor G4KLX
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
#include <atomic>
#include <memory>

#include "M17Control.h"
#include "M17Network.h"
#include "M17Gateway.h"
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
  std::unique_ptr<CM17Gateway> m_gateway;

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
