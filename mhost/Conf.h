/*
 *   Copyright (C) 2015-2023 by Jonathan Naylor G4KLX
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

#if !defined(CONF_H)
#define	CONF_H

#include <string>
#include <vector>

class CConf
{
public:
  CConf(const std::string& file);
  ~CConf();

  bool read();

  // The General section
  std::string  getCallsign() const;
  unsigned int getTimeout() const;
  bool         getDuplex() const;
  std::string  getDisplay() const;
  bool         getDaemon() const;

  // The Info section
  unsigned int getRXFrequency() const;
  unsigned int getTXFrequency() const;


  // The Log section
  unsigned int getLogDisplayLevel() const;
  unsigned int getLogFileLevel() const;
  std::string  getLogFilePath() const;
  std::string  getLogFileRoot() const;
  bool         getLogFileRotate() const;

  // The Modem section
  std::string  getModemProtocol() const;
  std::string  getModemUARTPort() const;
  unsigned int getModemUARTSpeed() const;
  std::string  getModemI2CPort() const;
  unsigned int getModemI2CAddress() const;
  std::string  getModemModemAddress() const;
  unsigned short getModemModemPort() const;
  std::string  getModemLocalAddress() const;
  unsigned short getModemLocalPort() const;
  bool         getModemRXInvert() const;
  bool         getModemTXInvert() const;
  bool         getModemPTTInvert() const;
  unsigned int getModemTXDelay() const;
  int          getModemTXOffset() const;
  int          getModemRXOffset() const;
  int          getModemRXDCOffset() const;
  int          getModemTXDCOffset() const;
  float        getModemRFLevel() const;
  float        getModemRXLevel() const;
  float        getModemM17TXLevel() const;
  std::string  getModemRSSIMappingFile() const;
  bool         getModemTrace() const;
  bool         getModemDebug() const;

  // The M17 section
  unsigned int getM17CAN() const;
  bool         getM17SelfOnly() const;
  bool         getM17AllowEncryption() const;
  unsigned int getM17TXHang() const;
  unsigned int getM17ModeHang() const;

  // The M17 Network section
  std::string  getM17GatewayAddress() const;
  unsigned short getM17GatewayPort() const;
  std::string  getM17LocalAddress() const;
  unsigned short getM17LocalPort() const;
  unsigned int getM17NetworkModeHang() const;
  bool         getM17NetworkDebug() const;

private:
  std::string  m_file;
  std::string  m_callsign;
  unsigned int m_timeout;
  bool         m_duplex;
  std::string  m_display;
  bool         m_daemon;

  unsigned int m_rxFrequency;
  unsigned int m_txFrequency;

  unsigned int m_logDisplayLevel;
  unsigned int m_logFileLevel;
  std::string  m_logFilePath;
  std::string  m_logFileRoot;
  bool         m_logFileRotate;

  std::string  m_modemProtocol;
  std::string  m_modemUARTPort;
  unsigned int m_modemUARTSpeed;
  std::string  m_modemI2CPort;
  unsigned int m_modemI2CAddress;
  std::string  m_modemModemAddress;
  unsigned short m_modemModemPort;
  std::string  m_modemLocalAddress;
  unsigned short m_modemLocalPort;
  bool         m_modemRXInvert;
  bool         m_modemTXInvert;
  bool         m_modemPTTInvert;
  unsigned int m_modemTXDelay;
  int          m_modemTXOffset;
  int          m_modemRXOffset;
  int          m_modemRXDCOffset;
  int          m_modemTXDCOffset;
  float        m_modemRFLevel;
  float        m_modemRXLevel;
  float        m_modemM17TXLevel;
  std::string  m_modemRSSIMappingFile;
  bool         m_modemTrace;
  bool         m_modemDebug;

  unsigned int m_m17CAN;
  bool         m_m17SelfOnly;
  bool         m_m17AllowEncryption;
  unsigned int m_m17TXHang;
  unsigned int m_m17ModeHang;

  std::string  m_m17GatewayAddress;
  unsigned short m_m17GatewayPort;
  std::string  m_m17LocalAddress;
  unsigned short m_m17LocalPort;
  unsigned int m_m17NetworkModeHang;
  bool         m_m17NetworkDebug;
};

#endif
