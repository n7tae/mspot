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

#include <atomic>
#include <string>
#include <mutex>
#include <future>
#include <random>
#include <vector>

#include "SafePacketQueue.h"
#include "SteadyTimer.h"
#include "SockAddress.h"
#include "Configure.h"
#include "UDPSocket.h"
#include "Callsign.h"
#include "GateState.h"
#include "Packet.h"
#include "HostMap.h"
#include "Stream.h"

enum class ELinkState { unlinked, linking, linked };
enum class EInternetType { ipv4only, ipv6only, both };

using SM17Link = struct sm17link_tag
{
	SM17RefPacket pongPacket;
	CSockAddress addr;
	CCallsign cs;
	char from_mod;
	std::atomic<ELinkState> state;
	bool maintainLink;
	CSteadyTimer receivePingTimer;
};

using SMessageTask = struct message_tag
{
	std::future<unsigned> futTask;
	std::atomic<bool> isDone;
};

class CM17Gateway
{
public:
	CM17Gateway() : EOTFNMask(0x8000u) {}
	bool Start();
	void Stop();

private:
	const uint16_t EOTFNMask;
	CCallsign thisCS;
	uint16_t can;
	std::string audioPath;
	EInternetType internetType;
	std::atomic<bool> keep_running;
	CUDPSocket ipv4, ipv6;
	SM17Link mlink;
	CSteadyTimer linkingTime, lastLinkSent;
	CStream gateStream, hostStream;
	std::mutex stateLock;
	CSockAddress from17k;
	std::future<void> gateFuture, hostFuture;
	CGateState gateState;
	CHostMap destMap;
	std::mt19937 m_random;
	std::queue<std::string> voiceQueue;
	std::unique_ptr<SMessageTask> msgTask;

	void ProcessGateway();
	void sendPacket(const void *buf, const size_t size, const CSockAddress &addr) const;
	void sendPacket2Host(const uint8_t *buf);
	void sendPacket2Dest(std::unique_ptr<SIPFrame> &frame);
	void ProcessHost();
	void sendLinkRequest();
	// returns true on error
	bool setDestination(const std::string &cs);
	// returns true on error
	bool setDestination(const   CCallsign &cs);
	void Dump(const char *title, const void *pointer, int length);
	void addMessage(const std::string &message);
	void makeCSData(const CCallsign &cs, const std::string &ofileName);
	unsigned PlayVoiceFiles(std::string message);

	// for executing rf based commands!
	uint16_t makeStreamID();
	void doUnlink(std::unique_ptr<SIPFrame> &);
	void doEcho(std::unique_ptr<SIPFrame> &);
	void doRecord(std::unique_ptr<SIPFrame> &);
	void doRecord(char, uint16_t);
	void doPlay(std::unique_ptr<SIPFrame> &);
	void doPlay(char c);
	void doStatus(std::unique_ptr<SIPFrame> &);
	void wait4end(std::unique_ptr<SIPFrame> &);
};
