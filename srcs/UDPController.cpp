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

#include "UDPController.h"
#include "Log.h"

#include <cstring>
#include <cassert>

const unsigned int BUFFER_LENGTH = 600U;

CUDPController::CUDPController(const std::string &modemAddress, unsigned modemPort, const std::string &localAddress, unsigned localPort)
: m_modemAddress(modemAddress)
, m_localAddress(localAddress)
, m_modemPort(modemPort)
, m_localPort(localPort)
, m_buffer(2000U, "UDP Controller Ring Buffer")
{
}

CUDPController::~CUDPController()
{
}

bool CUDPController::open()
{
	if (m_localSocket.Initialize(m_localAddress, m_localPort))
		return true;
	if (m_modemSocket.Initialize(m_modemAddress, m_modemPort))
		return true;
	return m_udpSocket.Open(m_localSocket);
}

int CUDPController::read(unsigned char* buffer, unsigned int length)
{
    assert(buffer != NULL);
    assert(length > 0U);

    unsigned char data[BUFFER_LENGTH];
    CSockAddress addr;
	auto ret = m_udpSocket.Read(data, BUFFER_LENGTH, addr);

    // An error occurred on the socket
    if (ret < 0)
        return ret;

    // Add new data to the ring buffer
    if (ret > 0) {
        if (addr == m_modemSocket && addr.GetPort() == m_modemSocket.GetPort())
            m_buffer.addData(data, ret);
    }

    // Get required data from the ring buffer
    unsigned int avail = m_buffer.dataSize();
    if (avail < length)
        length = avail;

    if (length > 0U)
        m_buffer.getData(buffer, length);

    return int(length);
}

int CUDPController::write(const unsigned char* buffer, unsigned int length)
{
	assert(buffer != NULL);
	assert(length > 0U);

	return m_udpSocket.Write(buffer, length, m_modemSocket);
}

void CUDPController::close()
{
	m_udpSocket.Close();
}
