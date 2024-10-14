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

#include "UnixDgramSocket.h"
#include "SockAddress.h"
#include "Configure.h"
#include "UDPSocket.h"
#include "Callsign.h"
#include "SteadyTimer.h"
#include "Packet.h"
#include "CRC.h"

enum class ELinkState { unlinked, linking, linked };
enum class EInternetType { ipv4only, ipv6only, both };

using SM17Link = struct sm17link_tag
{
	SM17RefPacket pongPacket;
	CSockAddress addr;
	CCallsign cs;
	char from_mod;
	std::atomic<ELinkState> state;
	CSteadyTimer receivePingTimer;
};

using SStream = struct stream_tag
{
	CSteadyTimer lastPacketTime;
	SM17Frame header;
};

class CM17Gateway
{
public:
	bool Initialize();
	void Process();
	void Close();
	void SetDestAddress(const std::string &address, uint16_t port);
	ELinkState GetLinkState() const { return mlink.state; }
	bool TryLock();
	void ReleaseLock();

private:
	CCallsign thisCS;
	EInternetType internetType;
	std::atomic<bool> keep_running;
	CCRC crc;
	CUnixDgramReader Host2Gate;
	CUnixDgramWriter Gate2Host;
	CUDPSocket ipv4, ipv6;
	SM17Link mlink;
	CSteadyTimer linkingTime;
	SStream currentStream;
	std::mutex streamLock;
	std::string qnvoice_file;
	CSockAddress from17k, destination;

	void linkCheck();
	void Write(const void *buf, const size_t size, const CSockAddress &addr) const;
	void PlayAudioMessage(const char *msg);
	void StreamTimeout();
	void PlayVoiceFile();
	void PlayAudioNotifyMessage(const char *msg);
	void Send(const void *buf, size_t size, const CSockAddress &addr) const;
	bool ProcessFrame(const uint8_t *buf);
	bool ProcessAM(const uint8_t *buf);
	void SendLinkRequest(const CCallsign &ref);
	void Dump(const char *title, const void *pointer, int length);
};
