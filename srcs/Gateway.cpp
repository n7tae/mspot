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
#ifdef DVREF
#include <curl/curl.h>
#include <sys/stat.h>
#endif
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
#include "Position.h"
#include "Version.h"
#include "MspotDB.h"
#include "Gateway.h"
#include "Random.h"
#include "CRC.h"
#ifdef DHT
#include "dht-values.h"
#endif

extern CCRC        g_Crc;
extern CRandom     g_RNG;
extern CConfigure  g_Cfg;
extern CGateState  g_GateState;
extern IPFrameFIFO Modem2Gate;
extern IPFrameFIFO Gate2Modem;

CMspotDB g_DataBase;

static const uint8_t quiet[] { 0x01u, 0x00u, 0x09u, 0x43u, 0x9Cu, 0xE4u, 0x21u, 0x08u };

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
	#ifdef DHT
	stopDHT();
	#endif
	Log(EUnit::gate, "stopping the Gateway...\n");
	keep_running = false;
	if (gateFuture.valid())
		gateFuture.get();
	if (modemFuture.valid())
		modemFuture.get();
	Log(EUnit::gate, "Gateway and Modem processing threads closed...\n");
	ipv4.Close();
	ipv6.Close();
	Log(EUnit::gate, "All Gateway resourced released\n");
}

bool CGateway::Start()
{
	// Prepare the sqlite3 database
	if (g_DataBase.Open(g_Cfg.GetString(g_Keys.gateway.section, g_Keys.gateway.dbPath).c_str()))
		return true;
	#ifdef DHT
	if (startDHT())
		return true;
	#endif
	// Get the hostfiles.refcheck.radio path, and maybe update the file, and then parse it and add it to the database
	int n;
	#ifdef DVREF
	std::filesystem::path jsonPath(g_Cfg.GetString(g_Keys.gateway.section, g_Keys.gateway.jsonHostPath));
	updateJsonHostFile(jsonPath);
	n = g_DataBase.ParseJsonFile(jsonPath.c_str());
	Log(EUnit::gate, "Read %d reflectors from %s\n", n, jsonPath.c_str());
	#endif
	// Get the M17 reflector pathname and add it to the database
	auto m17hosts = g_Cfg.GetString(g_Keys.gateway.section, g_Keys.gateway.m17HostPath);
	n = g_DataBase.FillGW(m17hosts.c_str());
	Log(EUnit::gate, "Read %d targets from %s\n", n, m17hosts.c_str());
	// Get the personal target pathname and add it to the database
	auto myhosts = g_Cfg.GetString(g_Keys.gateway.section, g_Keys.gateway.myHostPath);
	n = g_DataBase.FillGW(myhosts.c_str());
	Log(EUnit::gate, "Read %d targets from %s\n", n, myhosts.c_str());
	n = g_DataBase.Count("targets");
	Log(EUnit::gate, "There are now %d targets defined in the database\n", n);
	#ifndef DHT
	if (0 == n) {
		Log(EUnit::gate, "ERROR: Without any targets, mspot must die!\n");
		return true;
	}
	#endif


	// create the callsign for the hotspot
	std::string cs(g_Cfg.GetString(g_Keys.repeater.section, g_Keys.repeater.callsign));
	cs.resize(8, ' ');
	cs.append(1, g_Cfg.GetString(g_Keys.repeater.section, g_Keys.repeater.module).at(0));
	thisCS.CSIn(cs);
	Log(EUnit::gate, "Station Callsign: %s\n", cs.c_str());

	// Get the h/s stack support and report it
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
		Log(EUnit::gate,"Neither IPv4 or IPV6 is enabled!\n");
		return true;
	}
	switch (internetType)
	{
	case EInternetType::ipv4only:
		Log(EUnit::gate, "Gateway supports IPv4 Only\n");
		break;
	case EInternetType::ipv6only:
		Log(EUnit::gate, "Gateway supports IPv6 Only\n");
		break;
	default:
		Log(EUnit::gate, "Gateway supports Dual Stack (IPv4 and IPv6)\n");
		break;
	}

	// If config'ed, open the IPV4 port, then the IPv6 port
	if (EInternetType::ipv6only != internetType)
	{
		CSockAddress addr;
		if (addr.Initialize(AF_INET, 0, "any")) // use ephemeral port
			return true;
		if (ipv4.Open(addr))
			return true;
		Log(EUnit::gate, "Gateway listening on %s\n", addr.GetAddress());
	}
	if (EInternetType::ipv4only != internetType)
	{
		CSockAddress addr;
		if (addr.Initialize(AF_INET6, 0, "any")) // use ephemeral port
			return true;
		if (ipv6.Open(addr))
			return true;
		Log(EUnit::gate, "Gateway listening on %s\n", addr.GetAddress());
	}

	// Set the channel access number
	can = g_Cfg.GetUnsigned(g_Keys.repeater.section, g_Keys.repeater.can);
	Log(EUnit::gate, "CAN = %u\n", unsigned(can));
	// Set the TYPE format for the h/s transmitter
	radioTypeIsV3 = g_Cfg.GetBoolean(g_Keys.repeater.section, g_Keys.repeater.radioTypeIsV3);
	Log(EUnit::gate, "Radio is using %s TYPE values\n", radioTypeIsV3 ? "V#3" : "V#2");

	keep_running = true;
	gateStream.Initialize(EStreamType::gate);
	modemStream.Initialize(EStreamType::modem);
	warnOnEncrypted    = g_Cfg.GetBoolean(g_Keys.gateway.section, g_Keys.gateway.warnIfEncrypted);
	warnOnNoTranscoder = g_Cfg.GetBoolean(g_Keys.gateway.section, g_Keys.gateway.warnNoTranscoder);
	// set up link on boot up
	if (g_Cfg.IsString(g_Keys.gateway.section, g_Keys.gateway.startupLink))
	{
		setDestination(g_Cfg.GetString(g_Keys.gateway.section, g_Keys.gateway.startupLink));
	}

	// set the audio path
	audioPath.assign(g_Cfg.GetString(g_Keys.gateway.section, g_Keys.gateway.audioFolder));
	Log(EUnit::gate, "Audio folder is at %s\n", audioPath.c_str());
	// make the repeater cs encoded audio
	makeCSData(thisCS, "repeater.dat");

	// add the startup message
	addMessage("welcome repeater");

	// Launch the processGateway() thread
	gateFuture = std::async(std::launch::async, &CGateway::processGateway, this);
	if (not gateFuture.valid())
	{
		Log(EUnit::gate, "Could not start the processGateway() thread\n");
		keep_running = false;
		return true;
	}

	// Launch the processModem() thread
	modemFuture = std::async(std::launch::async, &CGateway::processModem, this);
	if (not modemFuture.valid())
	{
		Log(EUnit::gate, "Could not start the processModem() thread\n");
		keep_running = false;
		return true;
	}

	std::this_thread::sleep_for(std::chrono::milliseconds(1500));
	return false;
}

// this thread reads packets from the internet and acts on them
void CGateway::processGateway()
{
	// Setup poll for the IPv4 and IPv6 gateway input
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
		switch (target.GetState())
		{
		case ELinkState::linked:
			if (target.TimedOut()) // is the reflector okay?
			{
				// looks like we lost contact, TIMEOUT
				addMessage("repeater was_disconnected_from destination");
				Log(EUnit::gate, "Disconnected from %s, TIMEOUT...\n", target.GetCS().c_str());
				target.Unlinked();
			}
			break;
		case ELinkState::linking:
			if (linkingTime.time() >= 30.0)
			{
				Log(EUnit::gate, "Link request to %s timeout.\n", target.GetCS().c_str());
				target.Unlinked();
			}
			else
			{
				if (lastLinkSent.time() > 5.0)
					sendLinkRequest();
			}
			break;
		case ELinkState::unlinked:
			if (target.HasAddress())
			{
				if (ERefType::none != target.GetType())
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
				Log(EUnit::gate, "Played %.2f sec message\n", count*0.04f);
			}
			else
			{
				Log(EUnit::gate, "Message task is done, but future task is invalid");
			}
			msgTask.reset();
			if ((EGateState::bootup != g_GateState.GetState()) or voiceQueue.Empty())
				g_GateState.Idle();
		}

		// can we play an audio message?
		if (not voiceQueue.Empty()) {
			if ((EGateState::bootup == g_GateState.GetState()) or g_GateState.TryState(EGateState::messagein))
			{
				if (not msgTask)
				{
					auto message = voiceQueue.Pop();
					Log(EUnit::gate, "Playing message '%s'\n", message.c_str());
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
			Log(EUnit::gate, "Gateway poll() error: %s\n", strerror(errno));
			return;
		}

		uint8_t buf[MAX_PACKET_SIZE];
		socklen_t fromlen = sizeof(struct sockaddr_storage);
		int length = 0;

		if (rval)	// receive any packet
		{
			if (pfds[0].revents & POLLIN)
			{
				// get the IPv4 Packet and remove POLLIN from revents
				length = recvfrom(pfds[0].fd, buf, MAX_PACKET_SIZE, 0, from17k.GetPointer(), &fromlen);
				pfds[0].revents &= ~POLLIN;
				if (pfds[0].revents)
				{
					Log(EUnit::gate, "poll() returned revents %d from IPv4 port\n", pfds[0].revents);
					return;
				}
			}
			else if (pfds[1].revents & POLLIN)
			{
				// get the IPv6 Packet and remove POLLIN from revents
				length = recvfrom(pfds[1].fd, buf, MAX_PACKET_SIZE, 0, from17k.GetPointer(), &fromlen);
				pfds[1].revents &= ~POLLIN;
				if (pfds[1].revents)
				{
					Log(EUnit::gate, "poll() returned revents %d from IPv6 port\n", pfds[1].revents);
					return;
				}
			}
		}

		if (length) // we have a packet
		{
			switch (length)	// process known packets
			{
			case 4:  				// DISC, ACKN or NACK
				if ((ELinkState::unlinked != target.GetState()) and (from17k == target.GetAddress()))
				{	// the current state in not unlinked and the packet is from the link target
					if (0 == memcmp(buf, "ACKN", 4))
					{
						target.Linked();
						makeCSData(target.GetCS(), "destination.dat");
						addMessage("repeater is_linked_to destination");
					}
					else if (0 == memcmp(buf, "NACK", 4))
					{
						addMessage("link_refused");
						Log(EUnit::gate, "Connection request refused from %s\n", target.GetCS().c_str());
						target.Unlinked();
					}
					else if (0 == memcmp(buf, "DISC", 4))
					{
						addMessage("repeater is_unlinked");
						Log(EUnit::gate, "Disconnected from %s at %s\n", target.GetCS().c_str(), target.GetAddress().GetAddress());
						target.Unlinked();
					}
					else
					{
						Log(EUnit::gate, "Unknown Packet:\n");
						Dump(nullptr, buf, length);
					}
				}
				else
				{
					Log(EUnit::gate, "Unknown Packet:\n");
					Dump(nullptr, buf, length);
				}
				break;
			case 10: 				// PING or DISC
				if ((ELinkState::linked == target.GetState()) and (from17k == target.GetAddress()))
				{
					if (0 == memcmp(buf, "PING", 4))
					{
						sendPacket(target.GetPongPacket(), 10, target.GetAddress());
					}
					else if (0 == memcmp(buf, "DISC", 4))
					{
						const CCallsign from(buf+4);
						if (from == target.GetCS())
						{
							addMessage("repeater was_disconnected_from destination");
							Log(EUnit::gate, "%s initiated a disconnect\n", from.GetCS().c_str());
							target.Unlinked();
						}
						else
							Log(EUnit::gate, "Got a bogus disconnect from '%s' @ %s\n", from.GetCS().c_str(), from17k.GetAddress());
					}
					else
					{
						Log(EUnit::gate, "Unknown Packet:\n");
						Dump(nullptr, buf, length);
					}
				}
				break;
			default:
				auto t = validate(buf, length);	// what kind of a packet is it?
				if (EPacketType::none == t)
				{
					Log(EUnit::gate, "Unknown Packet:\n");
					Dump(nullptr, buf, length);
				} else { // it's either a PM or SM data packet
					// allocate a new packet with the default constructor and initialize it
					auto p = std::make_unique<CPacket>();
					p->Initialize(t, buf, length);
					if (p->CheckCRC())
					{
						Log(EUnit::gate, "Incoming Gateway Packet failed CRC check:\n");
						Dump(nullptr, buf, length);
					} else {
						const CCallsign dst(p->GetCDstAddress());
						if (dst == thisCS)
						{
							// the DST is this h/s, readdress to @ALL
							memset(p->GetDstAddress(), 0xffu, 6);
							p->CalcCRC();
						}
						// Send it on!!!!!!!!
						sendPacket2Modem(std::move(p));
					}
				}
				break;
			}
		}

		// check for a stream timeout
		if (gateStream.IsOpen() and gateStream.GetLastTime() >= 1.6)
		{
			gateStream.CloseStream(true, g_DataBase);
			g_GateState.Idle();
		}

		// finally, if we can, send a saved PM data. It should already be in the lastheard.
		if (not pmQueue.IsEmpty() and g_GateState.SetStateToOnlyIfFrom(EGateState::gatepacketin, EGateState::idle))
		{
			auto p = pmQueue.PopWait();
			Gate2Modem.Push(p);
		}
	}
}

void CGateway::processModem()
{
	while (keep_running)
	{
		auto p = Modem2Gate.PopWaitFor(40);
		if (p) {
			const auto ptype = p->GetType();
			const CCallsign dst(p->GetCDstAddress());
			const auto eReflectorType = dst.GetReflectorType();
			if (EPacketType::packet == ptype) { // process packet data
				switch (target.GetState())
				{
				case ELinkState::linked:
					sendPacket2Dest(std::move(p));
					break;
				case ELinkState::linking:
					addMessage("repeater is_already_linking");
					Log(EUnit::gate, "%s is not yet linked, the packet was not sent", thisCS.c_str());
					break;
				case ELinkState::unlinked:
					if (ERefType::none != eReflectorType) {
						#ifdef DHT
						get(dst.GetCS(ERefType::m17==eReflectorType ? 7 : 6));
						#endif
						if (setDestination(dst)) {
							linkingTime.start();
							target.Linking();
							// the packet is not used, it will die here
						} else {
							Log(EUnit::gate, "Reflector %s not found\n", dst.c_str());
						}
					} else {
						if (dst == target.GetCS()) {
							sendPacket2Dest(std::move(p));
						} else {
							if (setDestination(dst)) {
								sendPacket2Dest(std::move(p));
							} else {
								Log(EUnit::gate, "Target %s not found\n", dst.c_str());
							}
						}
					}
					break;
				}
			} else if (EPacketType::stream == ptype) {
				switch (dst.GetBase())
				{
				case CalcCSCode("E"):
				case CalcCSCode("ECHO"):
					doEcho(p);
					g_GateState.Idle();
					break;
				case CalcCSCode("I"):
				case CalcCSCode("S"):
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
					switch (target.GetState())
					{
					case ELinkState::linked:
						if ((dst == target.GetCS()) or (ERefType::none == eReflectorType)) { // is the destination the linked reflector?
							sendPacket2Dest(std::move(p));
						} else {
							addMessage("repeater is_already_linked");
							wait4end(p);
							Log(EUnit::gate, "Destination is %s but you are already linked to %s\n", dst.c_str(), target.GetCS().c_str());
							g_GateState.Idle();
						}
						break;
					case ELinkState::linking:
						wait4end(p);
						if (dst == target.GetCS()) {
							Log(EUnit::gate, "%s is not yet linked", dst.c_str());
						} else {
							addMessage("repeater is_already_linking");
							Log(EUnit::gate, "Destination is %s but you are linking to %s\n", dst.c_str(), target.GetCS().c_str());
						}
						g_GateState.Idle();
						break;
					case ELinkState::unlinked:
						if (ERefType::none != eReflectorType) {
							wait4end(p);
							#ifdef DHT
							get(dst.GetCS(ERefType::m17 == eReflectorType ? 7 : 6));
							#endif
							if (setDestination(dst))
							{
								linkingTime.start();
								target.Linking();
							}
							g_GateState.Idle();
						} else {
							if (dst == target.GetCS()) {
								sendPacket2Dest(std::move(p));
							} else {
								wait4end(p);
								setDestination(dst);
							}
						}
						break;
					}
					break;
				}
			}
		} else {
			// check for a timeout from the modem
			if (modemStream.IsOpen() and modemStream.GetLastTime() >= 1.0)
			{
				modemStream.CloseStream(true, g_DataBase); // close the modemStream
				g_GateState.Idle();
			}
			if (g_GateState.SetStateToOnlyIfFrom(EGateState::idle, EGateState::rftimeout))
				Log(EUnit::gate, "Reset state to idle from RF timeout\n");
		}
	}
}

void CGateway::sendLinkRequest()
{
	// make a CONN packet
	SM17RefPacket conn;
	memcpy(conn.magic, "CON", 3);
	conn.magic[3] = (ETypeVersion::deprecated == target.GetTypeVersion()) ? 'N' : '3';
	thisCS.CodeOut(conn.cscode);
	conn.mod = target.GetCS().GetModule();
	// send the link request
	sendPacket(conn.magic, 11, target.GetAddress());

	Log(EUnit::gate, "CON%c request sent to %s at %s on port %u\n", char(conn.magic[3]), target.GetCS().c_str(), target.GetAddress().GetAddress(), target.GetAddress().GetPort());
	// finish up
	lastLinkSent.start();
}

// this also opens and closes the gateStream
void CGateway::sendPacket2Modem(std::unique_ptr<CPacket> p)
{
	if (EGateState::bootup == g_GateState.GetState())
	{
		p.reset();
		return; // drop the packet unit we're done booting up
	}

	// act on a PM packet
	if (EPacketType::packet == p->GetType())
	{
		const CCallsign dst(p->GetCDstAddress());
		const CCallsign src(p->GetCSrcAddress());
		std::string from;
		if (from17k == target.GetAddress())
			from.assign(target.GetCS().c_str());
		else
			from.assign("Direct");
		unsigned fc = p->GetSize()-34u;
		fc = fc / 25 + ((fc % 25) ? 2 : 1);
		g_DataBase.UpdateLH(src.c_str(), dst.c_str(), false, from.c_str(), fc);
		if (g_GateState.TryState(EGateState::gatepacketin))
			Gate2Modem.Push(p);
		else
		{
			// save this for sending later;
			Log(EUnit::gate, "Got Packet from %s, saving for later\n", src.c_str());
			pmQueue.Push(p);
		}
		return;
	}

	// process a Stream data packet
	const auto sid = p->GetStreamId();
	if (gateStream.IsOpen())	// is the stream open?
	{
		if (gateStream.GetStreamID() == sid)
		{
			if ((p->GetFrameNumber()%6 == 0) and (CFrameType(p->GetFrameType()).GetMetaDataType()==EMetaDatType::gnss))
			{
				CPosition position(p->GetCMetaData());
				std::string la, lo;
				auto maidenhead = position.GetPosition(la, lo);
				if (maidenhead) {
					const CCallsign src(p->GetCSrcAddress());
					g_DataBase.UpdatePosition(src.c_str(), maidenhead, la, lo);
					//Log(EUnit::cc12, "Position for %s: lat=%.5f lon=%.5f Station=%s Source=%s\n", src.c_str(), la, lo, position.GetStation(), position.GetSource());
				}
			}
			gateStream.CountnTouch();
			auto islast = p->IsLastPacket();
			Gate2Modem.Push(p);
			if (islast)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(2));
				gateStream.CloseStream(false, g_DataBase); // close the stream
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
			const CCallsign src(p->GetCSrcAddress());
			const CCallsign dst(p->GetCDstAddress());
			if (from17k == target.GetAddress()) {
				gateStream.OpenStream(src.c_str(), sid, target.GetCS().c_str());
				g_DataBase.UpdateLH(src.c_str(), dst.c_str(), true, target.GetCS().c_str());
			} else if (ELinkState::unlinked == target.GetState() and target.GetAddress().AddressIsZero()) {
				if (setDestination(dst)) {
					Log(EUnit::gate, "Direct routing from %s!\n", src.c_str());
				} else {
					if (p->IsLastPacket())
						Log(EUnit::gate, "Detected direct routing from %s, but could not find an address for that\n", src.c_str());
					g_GateState.Idle();
					return;
				}
			}
			Gate2Modem.Push(p);
			gateStream.CountnTouch();
		}
	}
	return;
}

// this also opens and closes the modemStream
void CGateway::sendPacket2Dest(std::unique_ptr<CPacket> p)
{
	CFrameType TYPE(p->GetFrameType());
	if (EVersionType::v3 == TYPE.GetVersion())
	{
		if (ETypeVersion::deprecated == target.GetTypeVersion()) {
			// the target requires legacy type
			p->SetFrameType(TYPE.GetFrameType(EVersionType::legacy));
			p->CalcCRC();
		} 
	} else {
		// the packet is using Legacy type
		if (ETypeVersion::deprecated != target.GetTypeVersion()) {
			// the target needs V3 TYPE
			p->SetFrameType(TYPE.GetFrameType(EVersionType::v3));
			p->CalcCRC();
		}
	}
	// TODO: -----------------------------------------------------------------
	if (EPacketType::packet == p->GetType())
	{
		const CCallsign dst(p->GetCDstAddress());
		const CCallsign src(p->GetCSrcAddress());
		auto fc = p->GetSize();
		sendPacket(p->GetCData(), fc, target.GetAddress());
		fc = fc / 25 + ((fc % 25) ? 2 : 1);
		g_DataBase.UpdateLH(src.c_str(), dst.c_str(), false, "CC1200", fc);
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
			if ((p->GetFrameNumber()%6 == 0) and (TYPE.GetMetaDataType()==EMetaDatType::gnss))
			{
				CPosition position(p->GetCMetaData());
				std::string la, lo;
				auto maidenhead = position.GetPosition(la, lo);
				if (maidenhead) {
					const CCallsign src(p->GetCSrcAddress());
					g_DataBase.UpdatePosition(src.c_str(), maidenhead, la, lo);
					//Log(EUnit::cc12, "Pos'tion for %s: lat=%.5f lon=%.5f Station=%s Source=%s\n", src.c_str(), la, lo, position.GetStation(), position.GetSource());
				}
			}
			sendPacket(p->GetCData(), p->GetSize(), target.GetAddress());
			modemStream.CountnTouch();
			if (islast)
			{
				modemStream.CloseStream(false, g_DataBase);
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
		const CCallsign dst(p->GetCDstAddress());
		const CCallsign src(p->GetCSrcAddress());
		modemStream.OpenStream(src.c_str(), framesid, "CC1200");
		sendPacket(p->GetCData(), p->GetSize(), target.GetAddress());
		g_DataBase.UpdateLH(src.c_str(), dst.c_str(), true, "CC1200");
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

// returns true on success
bool CGateway::setDestination(const CCallsign &callsign)
{
	std::string csstr;
	const auto eRefType = callsign.GetReflectorType();
	const auto module   = callsign.GetModule();
	if (ERefType::none == eRefType)
		csstr.assign(callsign.c_str());
	else
		csstr.assign(callsign.c_str(), ERefType::m17==eRefType ? 7 : 6);
	EDataType dType;
	ETypeVersion tVersion;
	std::string mods, smods;
	CSockAddress addr;
	if (g_DataBase.GetTarget(csstr.c_str(), dType, tVersion, mods, smods, addr))
	{
		std::string datatype, versiontype;
		switch (dType)
		{
			case EDataType::pkt_only: datatype.assign("Pkt-only");  break;
			case EDataType::str_only: datatype.assign("Str-only");  break;
			default:                  datatype.assign("Pkt & Str"); break;
		}
		switch (tVersion)
		{
			case ETypeVersion::deprecated: versiontype.assign("Legacy-only");  break;
			case ETypeVersion::v3:         versiontype.assign("V#3-only");     break;
			default:                       versiontype.assign("Legacy & V#3"); break;
		}
		Log(EUnit::gate, "Found %s with IP adress %s on port %u\n", csstr.c_str(), addr.GetAddress(), addr.GetPort());
		Log(EUnit::gate, "Capabilites for %s: Data Handling: %s TYPE Handling: %s\n", csstr.c_str(), datatype.c_str(), versiontype.c_str());
		if (not mods.empty())
			Log(EUnit::gate, "%s Modules: '%s' Special-Modules: '%s'\n", csstr.c_str(), mods.c_str(), smods.c_str());
		if (ERefType::none != eRefType)
		{
			if (std::string::npos == mods.find(module))
			{
				Log(EUnit::gate, "Reflector %s doesn't have a module %c\n", csstr.c_str(), module);
				return false;
			}
			if (ERefType::m17 == eRefType) {
				if (warnOnEncrypted and (std::string::npos != smods.find(module)))
					Log(EUnit::gate, "WARNING: Reflector module %s is encrypted\n", callsign.c_str());
			} else {
				if (warnOnNoTranscoder and (std::string::npos == smods.find(module)))
					Log(EUnit::gate, "WARNING: Reflector module %s is not transcoded\n", callsign.c_str());
			}
		}
		target.TargetInit(callsign, eRefType, dType, tVersion, mods, smods, addr, thisCS);
		return true;
	}
	Log(EUnit::gate, "Host '%s' not found\n", csstr.c_str());
	return false;
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
		Log(EUnit::gate, "could not open %s\n", oFilePath.c_str());
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
		Log(EUnit::gate, "could not open %s\n", ap.c_str());
		ofile.close();
		return;
	}

	if (words.size() < 67)
	{
		Log(EUnit::gate, "Only found %u words in %s\n", words.size(), speakPath.c_str());
		ofile.close();
		return;
	}

	// open speak.dat
	speakPath.replace_extension("dat");
	std::ifstream sfile(speakPath, std::ios::binary);
	if (not sfile.is_open())
	{
		ofile.close();
		Log(EUnit::gate, "Could not open %s\n", speakPath.c_str());
		return;
	}

	Log(EUnit::gate, "Building '%s' at %s\n", cs.c_str(), oFilePath.c_str());

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
			Log(EUnit::gate, "adding quiet (for ' ' at position %u)\n", pos);
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
			Log(EUnit::gate, "'%s' does not exist\n", afp.c_str());
			continue;
		}

		unsigned fsize = std::filesystem::file_size(afp);
		if (fsize % 8 or fsize == 0)
		{
			Log(EUnit::gate, "'%s' size, %u, is not a multiple of 8\n", afp.c_str(), fsize);
		}
		fsize /= 8u; // count of 1/2 of a 16 byte payload, 20 ms

		ifile.open(afp.c_str(), std::ios::binary);
		if (not ifile.is_open())
		{
			Log(EUnit::gate, "'%s' could not be opened\n", afp.c_str());
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

#ifdef DVREF
// callback function writes data to a std::ostream
static size_t data_write(void* buf, size_t size, size_t nmemb, void* userp)
{
	if(userp)
	{
		std::ostream& os = *static_cast<std::ostream*>(userp);
		std::streamsize len = size * nmemb;
		if(os.write(static_cast<char*>(buf), len))
			return len;
	}
	return 0;
}

// timeout is in seconds
static CURLcode curl_read(const std::string& url, std::ostream& os, long timeout = 30)
{
	CURLcode code(CURLE_FAILED_INIT);
	CURL* curl = curl_easy_init();

	if(curl)
	{
		if(CURLE_OK == (code = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &data_write))
		&& CURLE_OK == (code = curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L))
		&& CURLE_OK == (code = curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L))
		&& CURLE_OK == (code = curl_easy_setopt(curl, CURLOPT_FILE, &os))
		&& CURLE_OK == (code = curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout))
		&& CURLE_OK == (code = curl_easy_setopt(curl, CURLOPT_USERAGENT, "mvoice/1.0"))
		&& CURLE_OK == (code = curl_easy_setopt(curl, CURLOPT_URL, url.c_str())))
		{
			code = curl_easy_perform(curl);
		}
		curl_easy_cleanup(curl);
	}
	return code;
}

void CGateway::updateJsonHostFile(std::filesystem::path &jsonHostPath)
{
    if (std::filesystem::exists(jsonHostPath)) {
		struct stat fa;
		if (stat(jsonHostPath.c_str(), &fa))
		{
			Log(EUnit::gate, "ERROR: stat() of %s returned: %s\n", jsonHostPath.c_str(), strerror(errno));
			return;
		}
		time_t now = time(nullptr);
		if (now - fa.st_mtime < 2*24*60*60)
		{
			Log(EUnit::gate, "%s is not old\n", jsonHostPath.c_str());
			return;
		}
    }

	curl_global_init(CURL_GLOBAL_ALL);

	std::stringstream ss;
	const std::string url("https://hostfiles.refcheck.radio/M17Hosts.json");
	if(CURLE_OK == curl_read(url, ss))
	{
		Log(EUnit::gate, "Refreshing %s\n", jsonHostPath.c_str());
		std::ofstream bkup(jsonHostPath.c_str(), std::fstream::trunc);
		if (bkup.is_open())
		{
			bkup << ss.rdbuf();
			bkup.close();
		}
		else
		{
			Log(EUnit::gate, "ERROR: Could not open %s\n", jsonHostPath.c_str());
		}
	}
	curl_global_cleanup();
}
#endif

#ifdef DHT
void CGateway::get(const std::string &cs)
{
	static std::time_t ts;
	ts = 0;
	dht::Where w;
	if (0 == cs.compare(0, 4, "M17-"))
		w.id(toUType(EMrefdValueID::Config));
	else if (0 == cs.compare(0, 3, "URF"))
		w.id(toUType(EUrfdValueID::Config));
	else
	{
		// std::cerr << "Unknown callsign '" << cs << "' for node.get()" << std::endl;
		return;
	}
	static auto iType = internetType;
	node.get(
		dht::InfoHash::get(cs),
		[](const std::shared_ptr<dht::Value> &v) {
			if (0 == v->user_type.compare(MREFD_CONFIG_1))
			{
				auto rdat = dht::Value::unpack<SMrefdConfig1>(*v);
				if (rdat.timestamp > ts)
				{
					ts = rdat.timestamp;
					if (iType!=EInternetType::ipv4only and rdat.ipv6addr.size())
						g_DataBase.UpdateGW(rdat.callsign, rdat.version, rdat.modules, rdat.encryptedmods, rdat.ipv6addr, rdat.port, rdat.url);
					else if (iType!=EInternetType::ipv6only and rdat.ipv4addr.size())
						g_DataBase.UpdateGW(rdat.callsign, rdat.version, rdat.modules, rdat.encryptedmods, rdat.ipv4addr, rdat.port, rdat.url);
				}
			}
			else if (0 == v->user_type.compare(URFD_CONFIG_1))
			{
				auto rdat = dht::Value::unpack<SUrfdConfig1>(*v);
				if (rdat.timestamp > ts)
				{
					ts = rdat.timestamp;
					if (iType!=EInternetType::ipv4only and rdat.ipv6addr.size())
						g_DataBase.UpdateGW(rdat.callsign, rdat.version, rdat.modules, rdat.transcodedmods, rdat.ipv6addr, rdat.port[toUType(EUrfdPorts::m17)], rdat.url);
					else if (iType!=EInternetType::ipv6only and rdat.ipv4addr.size())
						g_DataBase.UpdateGW(rdat.callsign, rdat.version, rdat.modules, rdat.transcodedmods, rdat.ipv4addr, rdat.port[toUType(EUrfdPorts::m17)], rdat.url);
				}
			}
			else
			{
				std::cerr << "Found the data, but it has an unknown user_type: " << v->user_type << std::endl;
			}
			return true;
		},
		[](bool success) {
			if (not success)
				std::cout << "node.get() was unsuccessful!" << std::endl;
		},
		{}, // empty filter
		w
	);
}

bool CGateway::startDHT()
{
	// start the dht instance
	try {
		node.run(17171, dht::crypto::generateIdentity(thisCS.c_str()), true, 59973);
	} catch (const std::exception &e) {
		Log(EUnit::gate, "Could not start the Ham-network: %s\n", e.what());
		return true;
	}

	// bootstrap the DHT from either saved nodes from a previous run,
	// or from the configured node
	std::string path(g_Cfg.GetString(g_Keys.dht.section, g_Keys.dht.dhtSavePath));
	std::string bs(g_Cfg.GetString(g_Keys.dht.section, g_Keys.dht.bootStrap));
	// Try to import nodes from binary file
	std::ifstream myfile(path, std::ios::binary|std::ios::ate);
	if (myfile.is_open())
	{
		msgpack::unpacker pac;
		auto size = myfile.tellg();
		myfile.seekg (0, std::ios::beg);
		pac.reserve_buffer(size);
		myfile.read (pac.buffer(), size);
		pac.buffer_consumed(size);
		// Import nodes
		msgpack::object_handle oh;
		while (pac.next(oh)) {
			auto imported_nodes = oh.get().as<std::vector<dht::NodeExport>>();
			Log(EUnit::gate, "Importing %u ham-dht nodes from %s\n", imported_nodes.size(), path.c_str());
			node.bootstrap(imported_nodes);
		}
		myfile.close();
	}
	else if (bs.size())
	{
		Log(EUnit::gate, "Bootstrapping from %s\n", bs.c_str());
		node.bootstrap(bs, "17171");
	}
	else
	{
		Log(EUnit::gate, "ERROR: Could not bootstrap the Ham-DHT network!\n");
		return true;
	}
	return false;
}

void CGateway::stopDHT()
{
	auto exnodes = node.exportNodes();
	if (exnodes.size() > 1)
	{
		std::string path(g_Cfg.GetString(g_Keys.dht.section, g_Keys.dht.dhtSavePath));
		std::ofstream myfile(path, std::ios::binary | std::ios::trunc);
		if (myfile.is_open())
		{
			Log(EUnit::gate, "Saving %u nodes to %s\n", exnodes.size(), path.c_str());
			msgpack::pack(myfile, exnodes);
			myfile.close();
		}
		else
			Log(EUnit::gate, "ERROR opening %s\n", path.c_str());
	}
	node.join();
}
#endif
