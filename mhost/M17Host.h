/*
 *   Copyright (C) 2015-2021 by Jonathan Naylor G4KLX
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

#if !defined(MMDVMHOST_H)
#define	MMDVMHOST_H

#include "M17Control.h"
#include "M17Network.h"
#include "Timer.h"
#include "Modem.h"
#include "Conf.h"

#include <string>


class CM17Host
{
public:
  CM17Host(const std::string& confFile);
  ~CM17Host();

  int run();

private:
  CConf           m_conf;
  CModem*         m_modem;
  CM17Control*    m_m17;
  CM17Network*    m_m17Network;
  unsigned char   m_mode;
  unsigned int    m_m17RFModeHang;
  unsigned int    m_m17NetModeHang;
  CTimer          m_modeTimer;
  bool            m_duplex;
  unsigned int    m_timeout;
  std::string     m_callsign;
  bool            m_fixedMode;

  void readParams();
  bool createModem();
  bool createM17Network();

  void setMode(unsigned char mode);

};

#endif
