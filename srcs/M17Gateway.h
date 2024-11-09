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

#include <atomic>
#include <string>
#include <mutex>
#include <future>
#include <random>

#include "SteadyTimer.h"
#include "SockAddress.h"
#include "Configure.h"
#include "UDPSocket.h"
#include "Callsign.h"
#include "GateState.h"
#include "Packet.h"
#include "HostMap.h"

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

using SStream = struct stream_tag
{
	CSteadyTimer lastPacketTime;
	SIPFrame header;
};

class CM17Gateway
{
public:
	bool Start();
	void Stop();

private:
	CCallsign thisCS;
	uint16_t can;
	std::string audioPath;
	EInternetType internetType;
	std::atomic<bool> keep_running;
	CUDPSocket ipv4, ipv6;
	SM17Link mlink;
	CSteadyTimer linkingTime, lastLinkSent;
	SStream currentStream;
	std::mutex stateLock;
	CSockAddress from17k;
	std::future<void> gateFuture, hostFuture;
	CGateState gateState;
	CHostMap destMap;
	std::mt19937 m_random;

	void ProcessGateway();
	void writePacket(const void *buf, const size_t size, const CSockAddress &addr) const;
	void streamTimeout();
	void sendPacket(const void *buf, size_t size, const CSockAddress &addr) const;
	void processGatePacket(const uint8_t *buf);
	void ProcessHost();
	void sendLinkRequest();
	bool setDestination(const std::string &cs);
	void Dump(const char *title, const void *pointer, int length);

	// for executing rf based commands!
	uint16_t makeStreamID();
	void doLink(std::unique_ptr<SIPFrame> &);
	void doUnlink(std::unique_ptr<SIPFrame> &);
	void doEcho(std::unique_ptr<SIPFrame> &);
	void doRecord(std::unique_ptr<SIPFrame> &);
	void doRecord(char, uint16_t);
	void doPlay(std::unique_ptr<SIPFrame> &);
	void doPlay(char c);
	void wait4end(std::unique_ptr<SIPFrame> &);
};
