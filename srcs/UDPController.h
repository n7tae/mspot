// Copyright (C) 2021 by Jonathan Naylor G4KLX

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

#include "SerialPort.h"
#include "RingBuffer.h"
#include "UDPSocket.h"

#include <string>

class CUDPController : public ISerialPort
{
public:
	CUDPController(const std::string& modemAddress, unsigned modemPort, const std::string& localAddress, unsigned localPort);
	virtual ~CUDPController();

	virtual bool open();

	virtual int read(unsigned char *buffer, unsigned length);

	virtual int write(const unsigned char *buffer, unsigned length);

	virtual void close();

protected:
	const std::string m_modemAddress, m_localAddress;
	const unsigned m_modemPort, m_localPort;
	CSockAddress m_localSocket, m_modemSocket;
	CUDPSocket m_udpSocket;
	CRingBuffer<unsigned char> m_buffer;
};
