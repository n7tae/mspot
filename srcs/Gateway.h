/*
	mspot - an M17 hot-spot using an  M17 CC1200 Raspberry Pi Hat
				Copyright (C) 2026 Thomas A. Early

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#pragma once

#include <filesystem>
#include <atomic>
#include <string>
#include <future>
#include <vector>
#include <mutex>

#include "SafePacketQueue.h"
#include "SteadyTimer.h"
#include "SockAddress.h"
#include "Configure.h"
#include "UDPSocket.h"
#include "Callsign.h"
#include "HostMap.h"
#include "Packet.h"
#include "Stream.h"
#include "Base.h"

enum class ELinkState { unlinked, linking, linked };
enum class EInternetType { ipv4only, ipv6only, both };

using SM17Link = struct sm17link_tag
{
	SM17RefPacket pongPacket;
	CSockAddress addr;
	CCallsign cs;
	std::atomic<ELinkState> state;
	bool maintainLink;
	CSteadyTimer receivePingTimer;
	bool isReflector;
};

using SMessageTask = struct message_tag
{
	std::future<unsigned> futTask;
	std::atomic<bool> isDone;
};

class CGateway : public CBase
{
public:
	bool Start();
	void Stop();
	void SetName(const std::string &name) { progName.assign(name); }
	const CCallsign &GetLink() { return mlink.cs; }
	const std::string &GetName() { return progName; }

private:
	CCallsign thisCS;
	std::string progName;
	uint16_t can;
	std::string audioPath;
	bool radioTypeIsV3;
	EInternetType internetType;
	std::atomic<bool> keep_running;
	CUDPSocket ipv4, ipv6;
	SM17Link mlink;
	CSteadyTimer linkingTime, lastLinkSent;
	CStream gateStream, modemStream;
	std::mutex stateLock;
	CSockAddress from17k;
	std::future<void> gateFuture, hostFuture;
	CHostMap destMap;
	std::queue<std::string> voiceQueue;
	std::unique_ptr<SMessageTask> msgTask;

	void ProcessGateway();
	EPacketType validate(uint8_t *in, unsigned length);
	void sendPacket(const void *buf, const size_t size, const CSockAddress &addr) const;
	void sendPacket2Modem(std::unique_ptr<CPacket> pack);
	void sendPacket2Dest(std::unique_ptr<CPacket> pack);
	void ProcessModem();
	void sendLinkRequest();
	// returns true on error
	bool setDestination(const std::string &cs);
	// returns true on error
	bool setDestination(const   CCallsign &cs);
	void addMessage(const std::string &message);
	void makeCSData(const CCallsign &cs, const std::string &ofileName);
	unsigned PlayVoiceFiles(std::string message);

	// for executing rf based commands!
	void doUnlink(std::unique_ptr<CPacket> &);
	void doEcho(std::unique_ptr<CPacket> &);
	void doRecord(std::unique_ptr<CPacket> &);
	void doRecord(char, uint16_t);
	void doPlay(std::unique_ptr<CPacket> &);
	void doPlay(char c);
	void doStatus(std::unique_ptr<CPacket> &);
	void wait4end(std::unique_ptr<CPacket> &);
};
