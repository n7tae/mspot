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
	bool in_stream;
	CSteadyTimer lastPacketTime;
	uint16_t streamid;
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
	SStream gateStream, hostStream;
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
	bool setDestination(const std::string &cs);
	bool setDestination(const   CCallsign &cs);
	void Dump(const char *title, const void *pointer, int length);
	void addMessage(const std::string &message);
	void makeCSData(const CCallsign &cs, const std::string &ofileName);
	unsigned PlayVoiceFiles(std::string &message);

	// for executing rf based commands!
	uint16_t makeStreamID();
	void doUnlink(std::unique_ptr<SIPFrame> &);
	void doEcho(std::unique_ptr<SIPFrame> &);
	void doRecord(std::unique_ptr<SIPFrame> &);
	void doRecord(char, uint16_t);
	void doPlay(std::unique_ptr<SIPFrame> &);
	void doPlay(char c);
	void wait4end(std::unique_ptr<SIPFrame> &);
};
