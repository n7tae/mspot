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

#include <poll.h>
#include <string>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <cstring>
#include <thread>
#include <chrono>
#include <map>

#include "SafePacketQueue.h"
#include "FrameType.h"
#include "Configure.h"
#include "GateState.h"
#include "Gateway.h"
#include "Random.h"
#include "CRC.h"

extern CCRC        g_Crc;
extern CRandom     g_RNG;
extern CConfigure  g_Cfg;
extern CGateState  g_GateState;
extern IPFrameFIFO Modem2Gate;
extern IPFrameFIFO Gate2Modem;


static const uint8_t quiet[] { 0x01u, 0x00u, 0x09u, 0x43u, 0x9Cu, 0xE4u, 0x21u, 0x08u };


static inline void split(const std::string &s, char delim, std::queue<std::string> &q)
{
	std::istringstream iss(s);
	std::string item;
	while (std::getline(iss, item, delim))
		q.push(item);
}

// trim from start (in place)
static inline void ltrim(std::string &s)
{
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](int ch) {
        return !std::isspace(ch);
    }));
}

// trim from end (in place)
static inline void rtrim(std::string &s)
{
    s.erase(std::find_if(s.rbegin(), s.rend(), [](int ch) {
        return !std::isspace(ch);
    }).base(), s.end());
}

// trim from both ends (in place)
static inline void trim(std::string &s)
{
    ltrim(s);
    rtrim(s);
}

// for calculating switch case values for host command processor
constexpr uint64_t CalcCSCode(const char *cs)
{
	int i = 0;
	while (cs[i++]) // cout the cs string length
		;
	if (i > 9)
		i = 9; // no more than 9 chars!
	/*
	If there are less than 9 chars in the command, then the 9th
	char could be used as a command parameter!
	*/

	uint64_t coded = 0u;
	const char *m17_alphabet(" ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-/.");
	bool skip = true;

	// procees each char in reverse order
	while (i-- > 0)
	{
		/*
		Find the char in the M17 character set.
		It's unlikely that there will be spaces at the end,
		or the that the input would contain bogus characters,
		but we'll check anyway, just to be safe!
		*/
		unsigned pos = 0u;
		for ( ; pos<41u; pos++)
		{
			if (m17_alphabet[pos] == cs[i])
				break;
		}
		if (40u == pos)
			pos = 0;
		if (skip and 0 == pos)
			continue;
		// found an M17 char, so everything counts from here
		skip = false;
		coded *= 40u;
		coded += pos;
	}
	return coded;
}

void CGateway::Stop()
{
	printMsg(TC_MAGENTA, TC_GREEN, "stopping the Gateway...\n");
	keep_running = false;
	if (gateFuture.valid())
		gateFuture.get();
	if (modemFuture.valid())
		modemFuture.get();
	printMsg(TC_MAGENTA, TC_GREEN, "Gateway and Modem processing threads closed...\n");
	ipv4.Close();
	ipv6.Close();
	printMsg(TC_MAGENTA, TC_GREEN, "All Gateway resourced released\n");
}

bool CGateway::Start()
{
	destMap.ReadAll();
	std::string cs(g_Cfg.GetString(g_Keys.repeater.section, g_Keys.repeater.callsign));
	cs.resize(8, ' ');
	cs.append(1, g_Cfg.GetString(g_Keys.repeater.section, g_Keys.repeater.module).at(0));
	thisCS.CSIn(cs);
	printMsg(TC_MAGENTA, TC_GREEN, "Station Callsign: %s\n", cs.c_str());

	if (g_Cfg.GetBoolean(g_Keys.gateway.section, g_Keys.gateway.ipv4))
	{
		internetType = g_Cfg.GetBoolean(g_Keys.gateway.section, g_Keys.gateway.ipv6) ? EInternetType::both : EInternetType::ipv4only;
	}
	else if (g_Cfg.GetBoolean(g_Keys.gateway.section, g_Keys.gateway.ipv6))
	{
		internetType = EInternetType::ipv6only;
	}
	else
	{
		printMsg(TC_MAGENTA, TC_RED,"Neither IPv4 or IPV6 is enabled!\n");
		return true;
	}
	switch (internetType)
	{
	case EInternetType::ipv4only:
		printMsg(TC_MAGENTA, TC_DEFAULT, "Gateway supports IPv4 Only\n");
		break;
	case EInternetType::ipv6only:
		printMsg(TC_MAGENTA, TC_DEFAULT, "Gateway supports IPv6 Only\n");
		break;
	default:
		printMsg(TC_MAGENTA, TC_DEFAULT, "Gateway supports Dual Stack (IPv4 and IPv6)\n");
		break;
	}

	if (EInternetType::ipv6only != internetType)
	{
		CSockAddress addr;
		if (addr.Initialize(AF_INET, 0, "any")) // use ephemeral port
			return true;
		if (ipv4.Open(addr))
			return true;
		printMsg(TC_MAGENTA, TC_DEFAULT, "Gateway listening on %s:%u\n", addr.GetAddress(), ipv4.GetPort());
	}
	if (EInternetType::ipv4only != internetType)
	{
		CSockAddress addr;
		if (addr.Initialize(AF_INET6, 0, "any")) // use ephemeral port
			return true;
		if (ipv6.Open(addr))
			return true;
		printMsg(TC_MAGENTA, TC_DEFAULT, "Gateway listening on [%s]:%u\n", addr.GetAddress(), ipv6.GetPort());
	}

	can = g_Cfg.GetUnsigned(g_Keys.repeater.section, g_Keys.repeater.can);
	radioTypeIsV3 = g_Cfg.GetBoolean(g_Keys.repeater.section, g_Keys.repeater.radioTypeIsV3);
	printMsg(TC_MAGENTA, TC_DEFAULT, "Radio is using %s TYPE values\n", radioTypeIsV3 ? "V#3" : "Legacy");

	mlink.state = ELinkState::unlinked;
	keep_running = true;
	gateStream.Initialize(EStreamType::gate);
	modemStream.Initialize(EStreamType::modem);
	mlink.maintainLink = g_Cfg.GetBoolean(g_Keys.gateway.section, g_Keys.gateway.maintainLink);
	printMsg(TC_MAGENTA, TC_DEFAULT, "Gateway will%s try to re-establish a dropped link\n", mlink.maintainLink ? "" : " NOT");
	if (g_Cfg.IsString(g_Keys.gateway.section, g_Keys.gateway.startupLink))
	{
		setDestination(g_Cfg.GetString(g_Keys.gateway.section, g_Keys.gateway.startupLink));
	}

	audioPath.assign(g_Cfg.GetString(g_Keys.gateway.section, g_Keys.gateway.audioFolder));
	printMsg(TC_MAGENTA,TC_DEFAULT, "Audio folder is at %s\n", audioPath.c_str());

	makeCSData(thisCS, "repeater.dat");

	addMessage("welcome repeater");
	gateFuture = std::async(std::launch::async, &CGateway::ProcessGateway, this);
	if (not gateFuture.valid())
	{
		printMsg(TC_MAGENTA, TC_RED, "Could not start the ProcessGateway() thread\n");
		keep_running = false;
		return true;
	}

	modemFuture = std::async(std::launch::async, &CGateway::ProcessModem, this);
	if (not modemFuture.valid())
	{
		printMsg(TC_MAGENTA, TC_RED, "Could not start the ProcessModem() thread\n");
		keep_running = false;
		return true;
	}

	std::this_thread::sleep_for(std::chrono::milliseconds(1500));
	return false;
}

void CGateway::ProcessGateway()
{
	struct pollfd pfds[2];
	for (unsigned i=0; i<2; i++)
	{
		pfds[i].fd = i ? ipv6.GetSocket() : ipv4.GetSocket();
		pfds[i].events = POLLIN;
		pfds[i].revents = 0;
	}

	while (keep_running)
	{
		// do link maintenance
		switch (mlink.state)
		{
		case ELinkState::linked:
			if (mlink.receivePingTimer.time() > 30.0) // is the reflector okay?
			{
				// looks like we lost contact
				addMessage("repeater was_disconnected_from destination");
				printMsg(TC_MAGENTA, TC_YELLOW, "Disconnected from %s, TIMEOUT...\n", mlink.cs.c_str());
				mlink.state = ELinkState::unlinked;
				if (not mlink.maintainLink)
					mlink.addr.Clear();
			}
			break;
		case ELinkState::linking:
			if (linkingTime.time() >= 30.0)
			{
				printMsg(TC_MAGENTA, TC_YELLOW, "Link request to %s timeout.\n", mlink.cs.c_str());
				mlink.state = ELinkState::unlinked;
			}
			else
			{
				if (lastLinkSent.time() > 5.0)
					sendLinkRequest();
			}
			break;
		case ELinkState::unlinked:
			if (not mlink.addr.AddressIsZero())
			{
				if (mlink.isReflector)
				{
					linkingTime.start();
					sendLinkRequest();
				}
			}
			break;
		}

		// close the current audio message task
		if (msgTask and msgTask->isDone)
		{
			if (msgTask->futTask.valid())
			{
				auto count = msgTask->futTask.get();
				printMsg(TC_MAGENTA, TC_DEFAULT, "Played %.2f sec message\n", count*0.04f);
			}
			else
			{
				printMsg(TC_MAGENTA, TC_YELLOW, "Message task is done, but future task is invalid");
			}
			msgTask.reset();
			if (EGateState::bootup != g_GateState.GetState())
				g_GateState.Idle();
			else if (voiceQueue.Empty())
				g_GateState.Idle();
		}

		// can we play an audio message?
		if (not voiceQueue.Empty()) {
			if ((EGateState::bootup == g_GateState.GetState()) or g_GateState.TryState(EGateState::messagein))
			{
				if (not msgTask)
				{
					auto message = voiceQueue.Pop();
					printMsg(TC_MAGENTA, TC_DEFAULT, "Playing message '%s'\n", message.c_str());
					msgTask = std::make_unique<SMessageTask>();
					msgTask->isDone = false;
					msgTask->futTask = std::async(std::launch::async, &CGateway::PlayVoiceFiles, this, message);
				}
			}
		}

		// any packets from IPv4 or 6?
		auto rval = poll(pfds, 2, 10);
		if (0 > rval)
		{
			printMsg(TC_MAGENTA, TC_RED, "gateway poll() error: %s\n", strerror(errno));
			return;
		}

		uint8_t buf[100];
		socklen_t fromlen = sizeof(struct sockaddr_storage);
		int length = 0;

		if (rval)	// receive any packet
		{
			if (pfds[0].revents & POLLIN)
			{
				length = recvfrom(pfds[0].fd, buf, 100, 0, from17k.GetPointer(), &fromlen);
				pfds[0].revents &= ~POLLIN;
			}
			else if (pfds[1].revents & POLLIN)
			{
				length = recvfrom(pfds[1].fd, buf, 100, 0, from17k.GetPointer(), &fromlen);
				pfds[1].revents &= ~POLLIN;
			}

			for (unsigned i=0; i<2; i++)	// check for errors
			{
				if (pfds[i].revents)
				{
					printMsg(TC_MAGENTA, TC_RED, "poll() returned revents %d from IPv%s port\n", pfds[i].revents, i ? '6' : '4');
					return;
				}
			}
		}

		if (length)
		{
			switch (length)	// process known packets
			{
			case 4:  				// DISC, ACKN or NACK
				if ((ELinkState::unlinked != mlink.state) and (from17k == mlink.addr))
				{
					if (0 == memcmp(buf, "ACKN", 4))
					{
						mlink.state = ELinkState::linked;
						makeCSData(mlink.cs, "destination.dat");
						addMessage("repeater is_linked_to destination");
						printMsg(TC_MAGENTA, TC_GREEN, "Connected to %s at %s\n", mlink.cs.c_str(), mlink.addr.GetAddress());
						mlink.receivePingTimer.start();
					}
					else if (0 == memcmp(buf, "NACK", 4))
					{
						addMessage("link_refused");
						printMsg(TC_MAGENTA, TC_YELLOW, "Connection request refused from %s\n", mlink.cs.c_str());
						mlink.cs.Clear();
						mlink.addr.Clear();
						mlink.state = ELinkState::unlinked;
					}
					else if (0 == memcmp(buf, "DISC", 4))
					{
						addMessage("repeater is_unlinked");
						printMsg(TC_MAGENTA, TC_YELLOW, "Disconnected from %s at %s\n", mlink.cs.c_str(), mlink.addr.GetAddress());
						mlink.addr.Clear(); // initiated with UNLINK, so don't try to reconnect
						mlink.cs.Clear();   // ^^^^^^^^^ ^^^^ ^^^^^^
						mlink.state = ELinkState::unlinked;
					}
					else
					{
						printMsg(TC_MAGENTA, TC_YELLOW, "Unknown Packet:\n");
						Dump(nullptr, buf, length);
					}
				}
				else
				{
					printMsg(TC_MAGENTA, TC_YELLOW, "Unknown Packet:\n");
					Dump(nullptr, buf, length);
				}
				break;
			case 10: 				// PING or DISC
				if ((ELinkState::linked == mlink.state) and (from17k == mlink.addr))
				{
					if (0 == memcmp(buf, "PING", 4))
					{
						sendPacket(mlink.pongPacket.magic, 10, mlink.addr);
						mlink.receivePingTimer.start();
					}
					else if (0 == memcmp(buf, "DISC", 4))
					{
						const CCallsign from(buf+4);
						if (from == mlink.cs)
						{
							addMessage("repeater was_disconnected_from destination");
							mlink.state = ELinkState::unlinked;
							if (not mlink.maintainLink)
								mlink.addr.Clear();
							printMsg(TC_MAGENTA, TC_YELLOW, "%s initiated a disconnect\n", from.GetCS().c_str());
						}
						else
							printMsg(TC_MAGENTA, TC_YELLOW, "Got a bogus disconnect from '%s' @ %s\n", from.GetCS().c_str(), from17k.GetAddress());
					}
					else
					{
						printMsg(TC_MAGENTA, TC_YELLOW, "Unknown Packet:\n");
						Dump(nullptr, buf, length);
					}
				}
				break;
			default:
				auto t = validate(buf, length);
				if (EPacketType::none == t)
				{
					printMsg(TC_MAGENTA, TC_YELLOW, "Unknown Packet:\n");
					Dump(nullptr, buf, length);
				} else {
					auto p = std::make_unique<CPacket>();
					p->Initialize(t, buf, length);
					if (p->CheckCRC())
					{
						printMsg(TC_MAGENTA, TC_RED, "Incoming Gateway Packet failed CRC check:\n");
						Dump(nullptr, buf, length);
					} else {
						sendPacket2Modem(std::move(p));
					}
				}
				break;
			}
		}

		// check for a stream timeout
		if (gateStream.IsOpen() and gateStream.GetLastTime() >= 1.6)
		{
			gateStream.CloseStream(true);
			g_GateState.Idle();
		}
	}
}

void CGateway::ProcessModem()
{
	while (keep_running)
	{
		auto p = Modem2Gate.PopWaitFor(40);
		if (not p)
			continue; // Modem2Gate timeout
		const CCallsign dst(p->GetCDstAddress());
		if (EPacketType::packet == p->GetType()) { // process packet data
			switch (mlink.state)
			{
			case ELinkState::linked:
				if ((dst == mlink.cs) or (not dst.IsReflector())) { // is the destination the linked reflector?
					sendPacket2Dest(std::move(p));
				} else {
					printMsg(TC_MAGENTA, TC_YELLOW, "Destination is %s but you are already linked to %s\n", dst.c_str(), mlink.cs.c_str());
				}
				break;
			case ELinkState::linking:
				if (dst == mlink.cs) {
					printMsg(TC_MAGENTA, TC_DEFAULT, "%s is not yet linked", dst.c_str());
				} else {
					printMsg(TC_MAGENTA, TC_YELLOW, "Destination is %s but you are linking to %s\n", dst.c_str(), mlink.cs.c_str());
				}
				break;
			case ELinkState::unlinked:
				if (dst.IsReflector()) {
					if (not setDestination(dst))
					{
						if (mlink.isReflector)
							mlink.state = ELinkState::linking;
						else
							printMsg(TC_MAGENTA, TC_GREEN, "IP Address for %s found: %s\n", dst.c_str(), mlink.addr.GetAddress());
					}
				} else {
					if (dst == mlink.cs) {
						sendPacket2Dest(std::move(p));
					} else {
						if (not setDestination(dst))
						{
							printMsg(TC_MAGENTA, TC_GREEN, "IP Address for %s found: %s\n", dst.c_str(), mlink.addr.GetAddress());
						}
					}
				}
				break;
			}
		} else if (EPacketType::stream == p->GetType()) {
			switch (dst.GetBase())
			{
			case CalcCSCode("E"):
			case CalcCSCode("ECHO"):
				doEcho(p);
				g_GateState.Idle();
				break;
			case CalcCSCode("I"):
			case CalcCSCode("STATUS"):
				doStatus(p);
				g_GateState.Idle();
				break;
			case CalcCSCode("U"):
			case CalcCSCode("UNLINK"):
				doUnlink(p);
				g_GateState.Idle();
				break;
			case CalcCSCode("RECORD"):
				doRecord(p);
				g_GateState.Idle();
				break;
			case CalcCSCode("PLAY"):
				doPlay(p);
				g_GateState.Idle();
				break;
			default:
				switch (mlink.state)
				{
				case ELinkState::linked:
					if ((dst == mlink.cs) or (not dst.IsReflector())) { // is the destination the linked reflector?
						sendPacket2Dest(std::move(p));
					} else {
						addMessage("repeater is_already_linked");
						wait4end(p);
						printMsg(TC_MAGENTA, TC_YELLOW, "Destination is %s but you are already linked to %s\n", dst.c_str(), mlink.cs.c_str());
						g_GateState.Idle();
					}
					break;
				case ELinkState::linking:
					wait4end(p);
					if (dst == mlink.cs) {
						printMsg(TC_MAGENTA, TC_DEFAULT, "%s is not yet linked", dst.c_str());
					} else {
						addMessage("repeater is_already_linking");
						printMsg(TC_MAGENTA, TC_YELLOW, "Destination is %s but you are linking to %s\n", dst.c_str(), mlink.cs.c_str());
					}
					g_GateState.Idle();
					break;
				case ELinkState::unlinked:
					if (dst.IsReflector()) {
						wait4end(p);
						if (not setDestination(dst))
						{
							if (mlink.isReflector)
								mlink.state = ELinkState::linking;
							else
								printMsg(TC_MAGENTA, TC_GREEN, "IP Address for %s found: %s\n", dst.c_str(), mlink.addr.GetAddress());
						}
						g_GateState.Idle();
					} else {
						if (dst == mlink.cs) {
							sendPacket2Dest(std::move(p));
						} else {
							wait4end(p);
							if (not setDestination(dst))
							{
								printMsg(TC_MAGENTA, TC_GREEN, "IP Address for %s found: %s\n", dst.c_str(), mlink.addr.GetAddress());
							}
						}
					}
					break;
				}
				break;
			}
		}
		else
		{
			// check for a timeout from the modem
			if (modemStream.IsOpen() and modemStream.GetLastTime() >= 1.0)
			{
				modemStream.CloseStream(true); // close the modemStream
				g_GateState.Idle();
			}
		}
	}
}

void CGateway::sendLinkRequest()
{
	// make a CONN packet
	SM17RefPacket conn;
	memcpy(conn.magic, "CONN", 4);
	thisCS.CodeOut(conn.cscode);
	conn.mod = mlink.cs.GetModule();
	// go ahead and make the pong packet
	memcpy(mlink.pongPacket.magic, "PONG", 4);
	thisCS.CodeOut(mlink.pongPacket.cscode);
	// send the link request
	sendPacket(conn.magic, 11, mlink.addr);
	printMsg(TC_MAGENTA,TC_DEFAULT, "Link request sent to %s at %s on port %u\n", mlink.cs.c_str(), mlink.addr.GetAddress(), mlink.addr.GetPort());
	// finish up
	lastLinkSent.start();
	mlink.state = ELinkState::linking;
}

// this also opens and closes the gateStream
void CGateway::sendPacket2Modem(std::unique_ptr<CPacket> p)
{
	if (EGateState::bootup == g_GateState.GetState())
	{
		p.reset();
		return; // drop the packet unit we're done booting up
	}
	if (EPacketType::packet == p->GetType())
	{
		if (g_GateState.TryState(EGateState::gatepacketin))
			Gate2Modem.Push(p);
		else
		{
			const CCallsign dst(p->GetCDstAddress());
			const CCallsign src(p->GetCSrcAddress());
			printMsg(TC_MAGENTA, TC_YELLOW, "Packet mode data received from the Gateway, but the system was busy\n");
			printMsg(TC_MAGENTA, TC_DEFAULT, "SRC = %s, DST = %s Message:\n", src.c_str(), dst.c_str());
			if (0x5u == *p->GetCPayload() and (0 == p->GetCData()[p->GetSize()-3]))
				printMsg(TC_MAGENTA, TC_BLUE, "%s\n", (const char *)(p->GetCPayload()+1));
			else
				Dump(nullptr, p->GetCPayload(), p->GetSize()-34);
		}
		return;
	}
	const auto sid = p->GetStreamId();
	if (gateStream.IsOpen())	// is the stream open?
	{
		if (gateStream.GetStreamID() == sid)
		{
			gateStream.CountnTouch();
			auto islast = p->IsLastPacket();
			Gate2Modem.Push(p);
			if (islast)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(2));
				gateStream.CloseStream(false); // close the stream
			}
		}
	}
	else
	{
		if (p->IsLastPacket()) // don't open a stream on a last packet
		{
			g_GateState.Idle();
			return;
		}
		// don't open a stream if this packet has the same SID as the last stream
		// and the last stream closed less than 1 second ago
		// NOTE: It's theoretically possible that this is actually a new stream that has the same SID.
		//       In that case, up to a second of the beginning of the stream will be lost. Sorry.
		if (sid == gateStream.GetPreviousID() and gateStream.GetLastTime() < 1.0)
		{
			return;
		}

		// Open the stream
		if (g_GateState.TryState(EGateState::gatestreamin))
		{
			gateStream.OpenStream(p->GetCSrcAddress(), sid, from17k.GetAddress());
			Gate2Modem.Push(p);
			gateStream.CountnTouch();
		}
	}
	return;
}

// this also opens and closes the modemStream
void CGateway::sendPacket2Dest(std::unique_ptr<CPacket> p)
{
	// There are only legacy destinations out there, so for now this will work
	// TODO: -----------------------------------------------------------------
	// The HostMap returned version needs to be in mlink so we can set the correct TYPE!
	CFrameType TYPE(p->GetFrameType());
	if (EVersionType::v3 == TYPE.GetVersion())
	{
		p->SetFrameType(TYPE.GetFrameType(EVersionType::legacy));
		p->CalcCRC();
	}
	// TODO: -----------------------------------------------------------------
	if (EPacketType::packet == p->GetType())
	{
		sendPacket(p->GetCData(), p->GetSize(), mlink.addr);
		g_GateState.Idle();
		return;
	}

	auto framesid = p->GetStreamId();
	if (modemStream.IsOpen())	// is the stream open?
	{
		if (modemStream.GetStreamID() == framesid)
		{	// Here's the next stream packet
			auto islast = p->IsLastPacket();
			p->CalcCRC();
			sendPacket(p->GetCData(), p->GetSize(), mlink.addr);
			modemStream.CountnTouch();
			if (islast)
			{
				modemStream.CloseStream(false);
				g_GateState.Idle();
			}
		}
	}
	else
	{
		if (p->IsLastPacket()) // don't open a stream on a last packet
		{
			g_GateState.Idle();
			return;
		}
		// don't open a stream if this packet has the same SID as the last stream
		if (framesid == modemStream.GetPreviousID() and modemStream.GetLastTime() < 1.0)
		{
			g_GateState.Idle();
			return;
		}

		// Open the Stream!!
		const CCallsign src(p->GetCSrcAddress());
		modemStream.OpenStream(p->GetCSrcAddress(), framesid);
		sendPacket(p->GetCData(), p->GetSize(), mlink.addr);
		modemStream.CountnTouch();
	}
}

void CGateway::sendPacket(const void *buf, const size_t size, const CSockAddress &addr) const
{
	if (AF_INET ==  addr.GetFamily())
		ipv4.Write(buf, size, addr);
	else
		ipv6.Write(buf, size, addr);
}

// returns false if successful
bool CGateway::setDestination(const std::string &callsign)
{
	const CCallsign cs(callsign);
	return setDestination(cs);
}

// returns true on error
bool CGateway::setDestination(const CCallsign &cs)
{
	auto phost = destMap.Find(cs.c_str());

	if (phost)
	{
		// prefer IPv6
		if (EInternetType::ipv4only != internetType and not phost->ipv6address.empty())
		{
			mlink.addr.Initialize(phost->ipv6address, phost->port);
			mlink.cs = cs;
			mlink.isReflector = cs.IsReflector();
			return false;
		}

		// if this is IPv6 only, we're done
		if (EInternetType::ipv6only == internetType)
		{
			printMsg(TC_MAGENTA, TC_YELLOW, "This IPv6-only system could not find an IPv6 address for '%s'\n", cs.c_str());
			return true;
		}

		// if the host is IPv6 only, we're also done
		if (phost->ipv4address.empty())
		{
			printMsg(TC_MAGENTA, TC_YELLOW, "There is no IPv4 address for '%s'\n", cs.c_str());
			return true;
		}

		// this is the default IPv4 address
		mlink.addr.Initialize(phost->ipv4address, phost->port);
		mlink.cs = cs;
		mlink.isReflector = cs.IsReflector();
		return false;
	}

	printMsg(TC_MAGENTA, TC_YELLOW, "Host '%s' not found\n", cs.c_str());
	return true;
}

void CGateway::addMessage(const std::string &message)
{
	voiceQueue.Push(message);
}

void CGateway::makeCSData(const CCallsign &cs, const std::string &ofileName)
{
	const std::filesystem::path ap(audioPath);
	const std::filesystem::path oFilePath(ap / ofileName);
	std::ofstream ofile(oFilePath, std::ios::binary | std::ios::trunc);
	if (not ofile.is_open())
	{
		printMsg(TC_MAGENTA, TC_RED, "could not open %s\n", oFilePath.c_str());
		return;
	}

	// our dictionary index
	std::map<unsigned, std::pair<unsigned, unsigned>> words;

	// open speak.index
	std::filesystem::path speakPath(ap);
	speakPath /= "speak.index";
	std::ifstream speakFile(speakPath.c_str());
	if (speakFile.is_open())
	{
		std::string line;
		while (std::getline(speakFile, line))
		{
			trim(line);
			if (0 == line.size() or '#' == line.at(0))
				continue;
			std::stringstream ss(line);
			unsigned index, start, stop, length;
			ss >> index >> start >> stop >> length;
			words[index] = std::make_pair(start, stop);
		}
		speakFile.close();
	}
	else
	{
		printMsg(TC_MAGENTA, TC_RED, "could not open %s\n", ap.c_str());
		ofile.close();
		return;
	}

	if (words.size() < 67)
	{
		printMsg(TC_MAGENTA, TC_RED, "Only found %u words in %s\n", words.size(), speakPath.c_str());
		ofile.close();
		return;
	}

	// open speak.dat
	speakPath.replace_extension("dat");
	std::ifstream sfile(speakPath, std::ios::binary);
	if (not sfile.is_open())
	{
		ofile.close();
		printMsg(TC_MAGENTA, TC_RED, "Could not open %s\n", speakPath.c_str());
		return;
	}

	printMsg(TC_MAGENTA, TC_DEFAULT, "Building '%s' at %s\n", cs.c_str(), oFilePath.c_str());

	// fill the output file with voice data
	auto callsign(cs.GetCS(8));
	rtrim(callsign);

	const std::string m17_ab(" ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-/.");
	for (std::size_t pos=0; pos<callsign.size(); pos++)
	{
		std::size_t indx = 66u;	// initialize the index to point at the "M17" word
		// check for "M17" at this postion
		if (pos+3 > callsign.size() or callsign.compare(pos, 3, "M17"))
		{
			// no "M17" at this pos, so find the m17 alphabet index
			indx = m17_ab.find(callsign[pos]);
			if (std::string::npos == indx)
				indx = 0u;
		}
		else // yes, here is an "M17" in the callsign
			pos += 2u; // increment the  position to the end of "M17"

		// now add the word to the data
		if (0 == indx)
		{
			printMsg(TC_MAGENTA, TC_DEFAULT, "adding quiet (for ' ' at position %u)\n", pos);
			// insert 200 millisecond of quiet, this is a ' ' in the callsign (very rare)
			for (int n=0; n<10; n++)
				ofile.write(reinterpret_cast<const char *>(quiet), 8);
		}
		else
		{
			unsigned start = words[indx].first;
			unsigned stop  = words[indx].second;
			sfile.seekg(8u * start);
			for (unsigned n=start; n<=stop; n++)
			{
				uint8_t hf[8];
				sfile.read(reinterpret_cast<char *>(hf), 8);
				ofile.write(reinterpret_cast<char *>(hf), 8);
			}
			// add 60 ms of quiet between each word
			for (unsigned n=0; n<3; n++)
				ofile.write(reinterpret_cast<const char *>(quiet), 8);
		}
		// loop back for the next char in the callsign base
	}
	// now add the callsign module, using a phonetic word, if the module is not a space
	const auto m = cs.GetModule();
	if (' ' != m)
	{
		unsigned indx = 40u + unsigned(m - 'A');	// alpha is at 40, zulu is as 65
		unsigned start = words[indx].first;
		unsigned stop  = words[indx].second;
		sfile.seekg(8u * start);
		for (unsigned n=start; n<=stop; n++)
		{
			uint8_t hf[8];
			sfile.read(reinterpret_cast<char *>(hf), 8);
			ofile.write(reinterpret_cast<char *>(hf), 8);
		}
	}
	// done adding all the words!
	sfile.close();
	ofile.close();
}

unsigned CGateway::PlayVoiceFiles(std::string message)
{
	CFrameType ft;
	ft.SetPayloadType(EPayloadType::c2_3200);
	ft.SetEncryptType(EEncryptType::none);
	ft.SetSigned(false);
	ft.SetMetaDataType(EMetaDatType::none);
	ft.SetCan(can);

	// make a voice frame template
	// we'll still need to add the payload, frame counter and the CRC before sending it to the modem.
	CPacket master;
	master.Initialize(EPacketType::stream, 54);
	master.SetStreamId(g_RNG.Get());
	memset(master.GetDstAddress(), 0xffu, 6); // set destination to BROADCAST
	thisCS.CodeOut(master.GetSrcAddress());
	master.SetFrameType(ft.GetFrameType(radioTypeIsV3 ? EVersionType::v3 : EVersionType::legacy));

	auto clock = std::chrono::steady_clock::now(); // start the packet clock
	std::ifstream ifile;

	std::queue<std::string> words;
	split(message, ' ', words);

	unsigned count = 0u;	// this counts the number of 20ms half-payloads

	// start with 320ms of quiet
	for (unsigned i=0; i<16; i++)
	{
		if (count % 2)
		{	// counter is odd, put this in the second half
			memcpy(master.GetPayload(false), quiet, 8);
			uint16_t fn = ((count / 2u) % 0x8000u);
			master.SetFrameNumber(fn);
			master.CalcCRC();
			auto p = std::make_unique<CPacket>();
			p->Initialize(EPacketType::stream, master.GetCData());
			clock = clock + std::chrono::milliseconds(40);
			std::this_thread::sleep_until(clock);
			Gate2Modem.Push(p);
		}
		else
		{	// counter is even, this goes in the first half
			memcpy(master.GetPayload(), quiet, 8);
		}
		count++;
	}

	while (not words.empty())
	{
		// build the pathname to the data file
		std::filesystem::path afp(audioPath);
		afp /= words.front();
		words.pop();
		afp.replace_extension(".dat");

		if (not std::filesystem::exists(afp))
		{
			printMsg(TC_MAGENTA, TC_RED, "'%s' does not exist\n", afp.c_str());
			continue;
		}

		unsigned fsize = std::filesystem::file_size(afp);
		if (fsize % 8 or fsize == 0)
		{
			printMsg(TC_MAGENTA, TC_YELLOW, "'%s' size, %u, is not a multiple of 8\n", afp.c_str(), fsize);
		}
		fsize /= 8u; // count of 1/2 of a 16 byte payload, 20 ms

		ifile.open(afp.c_str(), std::ios::binary);
		if (not ifile.is_open())
		{
			printMsg(TC_MAGENTA, TC_RED, "'%s' could not be opened\n", afp.c_str());
			continue;
		}

		for (unsigned i=1; i<=fsize; i++) // read all the data
		{
			if (count % 2)
			{	// counter is odd, this is the second half of the C2_3200 data
				ifile.read(reinterpret_cast<char *>(master.GetPayload(false)), 8);
				// now finsih off the packet
				uint16_t fn = ((count / 2u) % 0x8000u);
				if (words.empty() and (i == fsize))
					fn |= 0x8000u; // nothing left to read, mark the end of the stream
				master.SetFrameNumber(fn);
				master.CalcCRC(); // seal it with the CRC

				// make the packet to pass to the modem
				auto p = std::make_unique<CPacket>();
				p->Initialize(EPacketType::stream, master.GetCData());
				clock = clock + std::chrono::milliseconds(40);
				std::this_thread::sleep_until(clock); // the frames will go out every 40 milliseconds
				Gate2Modem.Push(p);
			}
			else
			{	// counter is even, this is the first 20 ms of C2_3200 data
				ifile.read(reinterpret_cast<char *>(master.GetPayload()), 8);
			}
			count++;
		}
		ifile.close();
		if (not words.empty())
		{
			// add 100 ms of quiet between files
			for (unsigned i=0; i<5; i++)
			{
				if (count % 2)
				{	// counter is odd, put this in the second half
					memcpy(master.GetPayload(false), quiet, 8);
					uint16_t fn = ((count / 2u) % 0x8000u);
					master.SetFrameNumber(fn);
					master.CalcCRC();
					auto p = std::make_unique<CPacket>();
					p->Initialize(EPacketType::stream, master.GetCData());
					clock = clock + std::chrono::milliseconds(40);
					std::this_thread::sleep_until(clock);
					Gate2Modem.Push(p);
				}
				else
				{	// counter is even, this goes in the first half
					memcpy(master.GetPayload(), quiet, 8);
				}
				count++;
			}
		}
	}
	if (count % 2) // if this is true, we need to complete and send the last packet
	{
		memcpy(master.GetPayload(false), quiet, 8);
		uint16_t fn = ((count %0x8000u) / 2u) + 0x8000u;
		master.SetFrameNumber(fn);
		master.CalcCRC();
		auto p = std::make_unique<CPacket>();
		p->Initialize(EPacketType::stream, master.GetCData());
		clock = clock + std::chrono::milliseconds(40);
		std::this_thread::sleep_until(clock);
		Gate2Modem.Push(p);
	}
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	// this thread can be harvested
	msgTask->isDone = true;
	// return the number of packets sent
	return count / 2u;
}

EPacketType CGateway::validate(uint8_t *in, unsigned length)
{
	if (0 == memcmp(in, "M17", 3))
	{
		if (' ' == in[3] and 54u == length)
		{
			CFrameType type(0x100u*in[18]+in[19]);
			if (EPayloadType::packet != type.GetPayloadType())
			{
				return EPacketType::stream;
			}
		} else if ('P' == in[3] and 37 < length and length <= MAX_PACKET_SIZE) {
			CFrameType type(0x100u*in[16]+in[17]);
			if (EPayloadType::packet == type.GetPayloadType())
			{
				return EPacketType::packet;
			}
		}
	}
	return EPacketType::none;
}
