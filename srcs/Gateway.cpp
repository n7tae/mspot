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
#include "Log.h"

extern CConfigure g_Cfg;
extern CCRC       g_Crc;
extern CRandom    g_RNG;
extern CGateState g_GateState;

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
	LogInfo("stopping the Gateway...");
	keep_running = false;
	if (gateFuture.valid())
		gateFuture.get();
	if (modemFuture.valid())
		modemFuture.get();
	LogDebug("Gateway and Modem processing threads closed...");
	ipv4.Close();
	ipv6.Close();
	LogInfo("All resourced released");
}

bool CGateway::Start()
{
	destMap.ReadAll();
	std::string cs(g_Cfg.GetString(g_Keys.repeater.section, g_Keys.repeater.callsign));
	cs.resize(8, ' ');
	cs.append(1, g_Cfg.GetString(g_Keys.repeater.section, g_Keys.repeater.module).at(0));
	thisCS.CSIn(cs);
	LogInfo("Station Callsign: %s", cs.c_str());

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
		LogError("Neither IPv4 or IPV6 is enabled!");
		return true;
	}
	switch (internetType)
	{
	case EInternetType::ipv4only:
		LogInfo("Gateway supports IPv4 Only");
		break;
	case EInternetType::ipv6only:
		LogInfo("Gateway supports IPv6 Only");
		break;
	default:
		LogInfo("Gateway supports Dual Stack (IPv4 and IPv6)");
		break;
	}

	if (EInternetType::ipv6only != internetType)
	{
		CSockAddress addr;
		if (addr.Initialize(AF_INET, 0, "any")) // use ephemeral port
			return true;
		if (ipv4.Open(addr))
			return true;
		LogInfo("Gateway listening on %s:%u", addr.GetAddress(), ipv4.GetPort());
	}
	if (EInternetType::ipv4only != internetType)
	{
		CSockAddress addr;
		if (addr.Initialize(AF_INET6, 0, "any")) // use ephemeral port
			return true;
		if (ipv6.Open(addr))
			return true;
		LogInfo("Gateway listening on [%s]:%u", addr.GetAddress(), ipv6.GetPort());
	}

	can = g_Cfg.GetUnsigned(g_Keys.repeater.section, g_Keys.repeater.can);
	radioTypeIsV3 = g_Cfg.GetBoolean(g_Keys.repeater.section, g_Keys.repeater.radioTypeIsV3);
	LogInfo("Radio is using %s TYPE values", radioTypeIsV3 ? "V#3" : "Legacy");

	mlink.state = ELinkState::unlinked;
	keep_running = true;
	gateStream.Initialize("Gateway");
	modemStream.Initialize("Modem");
	mlink.maintainLink = g_Cfg.GetBoolean(g_Keys.gateway.section, g_Keys.gateway.maintainLink);
	LogInfo("Gateway will%s try to re-establish a dropped link", mlink.maintainLink ? "" : " NOT");
	if (g_Cfg.IsString(g_Keys.gateway.section, g_Keys.gateway.startupLink))
	{
		setDestination(g_Cfg.GetString(g_Keys.gateway.section, g_Keys.gateway.startupLink));
	}

	audioPath.assign(g_Cfg.GetString(g_Keys.gateway.section, g_Keys.gateway.audioFolder));
	LogInfo("Audio folder is at %s", audioPath.c_str());

	makeCSData(thisCS, "repeater.dat");

	gateFuture = std::async(std::launch::async, &CGateway::ProcessGateway, this);
	if (not gateFuture.valid())
	{
		LogError("Could not start the ProcessGateway() thread");
		keep_running = false;
		return true;
	}

	modemFuture = std::async(std::launch::async, &CGateway::ProcessModem, this);
	if (not modemFuture.valid())
	{
		LogError("Could not start the ProcessModem() thread");
		keep_running = false;
		return true;
	}

	std::this_thread::sleep_for(std::chrono::milliseconds(1500));
	addMessage("welcome repeater");
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
			if (mlink.receivePingTimer.time() > 30) // is the reflector okay?
			{
				// looks like we lost contact
				addMessage("repeater was_disconnected_from destination");
				LogInfo("Disconnected from %s, TIMEOUT...\n", mlink.cs.c_str());
				mlink.state = ELinkState::unlinked;
				if (not mlink.maintainLink)
					mlink.addr.Clear();
			}
			break;
		case ELinkState::linking:
			if (linkingTime.time() >= 30)
			{
				LogInfo("Link request to %s timeout.\n", mlink.cs.c_str());
				mlink.state = ELinkState::unlinked;
			}
			else
			{
				if (lastLinkSent.time() > 4)
					sendLinkRequest();
			}
			break;
		case ELinkState::unlinked:
			if (not mlink.addr.AddressIsZero())
			{
				if (mlink.isReflector)
				{
					sendLinkRequest();
					linkingTime.start();
				}
			}
			break;
		}

		// close the current audio message task
		if (msgTask && msgTask->isDone)
		{
			if (msgTask->futTask.valid())
			{
				auto count = msgTask->futTask.get();
				LogInfo("Played %.2f sec message", count*0.04f);
			}
			else
			{
				LogWarning("Message task is done, but future task is invalid");
			}
			msgTask.reset();
			g_GateState.Idle();
		}

		// can we play an audio message?
		if (not voiceQueue.empty())
		{
			if (g_GateState.SetStateOnlyIfIdle(EGateState::messagein))
			{
				auto message = voiceQueue.front();
				voiceQueue.pop();
				if (msgTask)
				{
					LogError("Trying to initiate message play, the g_GateState was idle, but msgTask is not empty!");
					LogError("'%s' will not be played!", message.c_str());
					g_GateState.Idle();
				}
				else
				{
					LogInfo("Playing message '%s'", message.c_str());
					msgTask = std::make_unique<SMessageTask>();
					msgTask->isDone = false;
					msgTask->futTask = std::async(std::launch::async, &CGateway::PlayVoiceFiles, this, message);
				}
			}
			// else
			// {
			// 	LogInfo("VoiceQueue not empty but state is %s", g_GateState.GetStateName());
			// }
		}

		// any packets from IPv4 or 6?
		auto rval = poll(pfds, 2, 10);
		if (0 > rval)
		{
			LogError("gateway poll() error: %s", strerror(errno));
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
					LogError("poll() returned revents %d from IPv%s port", pfds[i].revents, i ? '6' : '4');
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
						LogInfo("Connected to %s at %s", mlink.cs.c_str(), mlink.addr.GetAddress());
						mlink.receivePingTimer.start();
					}
					else if (0 == memcmp(buf, "NACK", 4))
					{
						mlink.state = ELinkState::unlinked;
						addMessage("link_refused");
						LogInfo("Connection request refused from %s\n", mlink.cs.c_str());
						mlink.cs.Clear();
						mlink.addr.Clear();
						mlink.state = ELinkState::unlinked;
					}
					else if (0 == memcmp(buf, "DISC", 4))
					{
						addMessage("repeater is_unlinked");
						LogInfo("Disconnected from %s at %s", mlink.cs.c_str(), mlink.addr.GetAddress());
						mlink.addr.Clear(); // initiated with UNLINK, so don't try to reconnect
						mlink.cs.Clear();   // ^^^^^^^^^ ^^^^ ^^^^^^
						mlink.state = ELinkState::unlinked;
					}
					else
					{
						Dump("Uknown Packet", buf, length);;
					}
				}
				else
				{
					Dump("Unexpected Packet", buf, length);
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
							LogInfo("%s initiated a disconnect", from.GetCS().c_str());
						}
						else
							LogInfo("Got a bogus disconnect from '%s' @ %s", from.GetCS().c_str(), from17k.GetAddress());
					}
					else
					{
						Dump("Unknown Packet", buf, length);
					}
				}
				break;
			default:
				auto t = validate(buf, length);
				if (EPacketType::none == t)
				{
					Dump("Unknown Packet", buf, length);
				} else {
					auto pack = std::make_unique<CPacket>();
					pack->Initialize(t, buf, length);
					if (pack->CheckCRC())
					{
						Dump("Incoming Gateway Packet failed CRC check", buf, length);
						pack.reset();
					} else {
						sendPacket2Modem(std::move(pack));
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
		auto pack = Modem2Gate.PopWaitFor(100);
		if (pack)
		{
			const CCallsign dst(pack->GetCDstAddress());
			switch (dst.GetBase())
			{
			case CalcCSCode("E"):
			case CalcCSCode("ECHO"):
				doEcho(pack);
				g_GateState.Idle();
				break;
			case CalcCSCode("I"):
			case CalcCSCode("STATUS"):
				doStatus(pack);
				g_GateState.Idle();
				break;
			case CalcCSCode("U"):
			case CalcCSCode("UNLINK"):
				doUnlink(pack);
				g_GateState.Idle();
				break;
			case CalcCSCode("RECORD"):
				doRecord(pack);
				g_GateState.Idle();
				break;
			case CalcCSCode("PLAY"):
				doPlay(pack);
				g_GateState.Idle();
				break;
			default:
				switch (mlink.state)
				{
				case ELinkState::linked:
					if (dst != mlink.cs and dst.IsReflector()) // is the destination the linked reflector?
					{
						addMessage("repeater is_already_linked");
						wait4end(pack);
						LogWarning("Destination is %s but you are already linked to %s", dst.c_str(), mlink.cs.c_str());
						g_GateState.Idle();
					} else {
						sendPacket2Dest(std::move(pack));
					}
					break;
				case ELinkState::linking:
					wait4end(pack);
					if (dst == mlink.cs) {
						LogInfo("%s is not yet linked", dst.c_str());
					} else {
						addMessage("repeater is_already_linking");
						LogWarning("Destination is %s but you are linking to %s", dst.c_str(), mlink.cs.c_str());
					}
					g_GateState.Idle();
					break;
				case ELinkState::unlinked:
					if (dst.IsReflector()) {
						wait4end(pack);
						if (not setDestination(dst))
						{
							if (mlink.isReflector)
								mlink.state = ELinkState::linking;
							else
								LogInfo("IP Address for %s found: %s", dst.c_str(), mlink.addr.GetAddress());
						}
						g_GateState.Idle();
					} else {
						if (dst == mlink.cs) {
							sendPacket2Dest(std::move(pack));
						} else {
							wait4end(pack);
							if (not setDestination(dst))
							{
								LogInfo("IP Address for %s found: %s", dst.c_str(), mlink.addr.GetAddress());
							}
						}
					}
				}
			}
		}
		else
		{
			// check for a timeout from the host
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
	LogMessage("Link request sent to %s at %s on port %u", mlink.cs.c_str(), mlink.addr.GetAddress(), mlink.addr.GetPort());
	// finish up
	lastLinkSent.start();
	mlink.state = ELinkState::linking;
}

// this also opens and closes the gateStream
void CGateway::sendPacket2Modem(std::unique_ptr<CPacket> pack)
{
	if (EPacketType::packet == pack->GetType())
	{
		if (g_GateState.TryState(EGateState::gatepacketin))
			Gate2Modem.Push(pack);
		else
		{
			const CCallsign dst(pack->GetCDstAddress());
			const CCallsign src(pack->GetCSrcAddress());
			LogInfo("PM data received from the Gateway, but the system was busy.");
			LogInfo("SRC = %s, DST = %s", src.c_str(), dst.c_str());
			if (0x5u == *pack->GetCPayload() and 0 == pack->GetCData()[pack->GetSize()-3])
				LogInfo("SMS MSG: %s", (const char *)(pack->GetCPayload()+1));
			else
				Dump("Non-SMS DATA", pack->GetCPayload(), pack->GetSize()-34);
		}
		return;
	}
	const auto sid = pack->GetStreamId();
	if (gateStream.IsOpen())	// is the stream open?
	{
		if (gateStream.GetStreamID() == sid)
		{
			gateStream.CountnTouch();
			auto islast = pack->IsLastPacket();
			Gate2Modem.Push(pack);
			if (islast)
			{
				gateStream.CloseStream(false); // close the stream
				g_GateState.Idle();
			}
		}
		else
		{
			pack.reset();  // this frame doesn't belong to the open stream
		}
	}
	else
	{
		if (pack->IsLastPacket()) // don't open a stream on a last packet
		{
			pack.reset();
			g_GateState.Idle();
			return;
		}
		// don't open a stream if this packet has the same SID as the last stream
		// and the last stream closed less than 1 second ago
		// NOTE: It's theoretically possible that this is actually a new stream that has the same SID.
		//       In that case, up to a second of the beginning of the stream will be lost. Sorry.
		if (sid == gateStream.GetPreviousID() and gateStream.GetLastTime() < 1.0)
		{
			pack.reset();
			return;
		}

		// Open the stream
		if (g_GateState.TryState(EGateState::gatestreamin))
		{
			gateStream.OpenStream(pack->GetCSrcAddress(), sid, from17k.GetAddress());
			Gate2Modem.Push(pack);
			gateStream.CountnTouch();
		}
	}
	return;
}

// this also opens and closes the modemStream
void CGateway::sendPacket2Dest(std::unique_ptr<CPacket> pack)
{
	if (EPacketType::packet == pack->GetType())
	{
		sendPacket(pack->GetCData(), pack->GetSize(), mlink.addr);
		g_GateState.Idle();
		return;
	}

	auto framesid = pack->GetStreamId();
	if (modemStream.IsOpen())	// is the stream open?
	{
		if (modemStream.GetStreamID() == framesid)
		{	// Here's the next stream packet
			auto islast = pack->IsLastPacket();
			pack->CalcCRC();
			sendPacket(pack->GetCData(), pack->GetSize(), mlink.addr);
			modemStream.CountnTouch();
			if (islast)
			{
				gateStream.CloseStream(false);
				g_GateState.Idle();
			}
		}
		else
		{
			pack.reset(); // this frame has the wrong SID
		}
	}
	else
	{
		if (not pack->IsLastPacket()) // don't open a stream on a last packet
		{
			pack.reset();
			g_GateState.Idle();
			return;
		}
		// don't open a stream if this packet has the same SID as the last stream
		if (framesid == modemStream.GetPreviousID() && modemStream.GetLastTime() < 1.0)
		{
			pack.reset();
			g_GateState.Idle();
			return;
		}

		// Open the Stream!!
		modemStream.OpenStream(pack->GetCSrcAddress(), pack->GetStreamId(), "MSpot");
		sendPacket(pack->GetCData(), pack->GetSize(), mlink.addr);
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
			return false;
		}

		// if this is IPv6 only, we're done
		if (EInternetType::ipv6only == internetType)
		{
			LogWarning("This IPv6-only system could not find an IPv6 address for '%s'", cs.c_str());
			return true;
		}

		// if the host is IPv6 only, we're also done
		if (phost->ipv4address.empty())
		{
			LogWarning("There is no IPv4 address for '%s'", cs.c_str());
			return true;
		}

		// this is the default IPv4 address
		mlink.addr.Initialize(phost->ipv4address, phost->port);
		mlink.cs = cs;
		mlink.isReflector = cs.IsReflector();
		return false;
	}

	LogWarning("Host '%s' not found", cs.c_str());
	return true;
}

void CGateway::addMessage(const std::string &message)
{
	voiceQueue.push(message);
}

void CGateway::makeCSData(const CCallsign &cs, const std::string &ofileName)
{
	const std::filesystem::path ap(audioPath);
	const std::filesystem::path oFilePath(ap / ofileName);
	std::ofstream ofile(oFilePath, std::ios::binary | std::ios::trunc);
	if (not ofile.is_open())
	{
		LogError("could not open %s", oFilePath.c_str());
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
		LogError("could not open %s", ap.c_str());
		ofile.close();
		return;
	}

	if (words.size() < 67)
	{
		LogError("Only found %u words in %s", words.size(), speakPath.c_str());
		ofile.close();
		return;
	}

	// open speak.dat
	speakPath.replace_extension("dat");
	std::ifstream sfile(speakPath, std::ios::binary);
	if (not sfile.is_open())
	{
		ofile.close();
		LogError("Could not open %s", speakPath.c_str());
		return;
	}

	LogInfo("Building '%s' at %s", cs.c_str(), oFilePath.c_str());

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
			LogInfo("adding quiet (for ' ' at position %u)", pos);
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
	memset(master.GetDstAddress(), 0xffu, 6); // set destination to Broadcast
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
			auto pack = std::make_unique<CPacket>();
			pack->Initialize(EPacketType::stream, master.GetCData());
			clock = clock + std::chrono::milliseconds(40);
			std::this_thread::sleep_until(clock);
			Gate2Modem.Push(pack);
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
			LogError("'%s' does not exist", afp.c_str());
			continue;
		}

		unsigned fsize = std::filesystem::file_size(afp);
		if (fsize % 8 or fsize == 0)
		{
			LogWarning("'%s' size, %u, is not a multiple of 8", afp.c_str(), fsize);
		}
		fsize /= 8u; // count of 1/2 of a 16 byte payload, 20 ms

		ifile.open(afp.c_str(), std::ios::binary);
		if (not ifile.is_open())
		{
			LogError("'%s' could not be opened", afp.c_str());
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
				auto pack = std::make_unique<CPacket>();
				pack->Initialize(EPacketType::stream, master.GetCData());
				clock = clock + std::chrono::milliseconds(40);
				std::this_thread::sleep_until(clock); // the frames will go out every 40 milliseconds
				//LogDebug("pushing msg packet 0x%04x", pack->GetFrameNumber());
				Gate2Modem.Push(pack);
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
					auto pack = std::make_unique<CPacket>();
					pack->Initialize(EPacketType::stream, master.GetCData());
					clock = clock + std::chrono::milliseconds(40);
					std::this_thread::sleep_until(clock);
					//LogDebug("pushing msg packet 0x%04x", pack->GetFrameNumber());
					Gate2Modem.Push(pack);
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
		auto pack = std::make_unique<CPacket>();
		pack->Initialize(EPacketType::stream, master.GetCData());
		clock = clock + std::chrono::milliseconds(40);
		std::this_thread::sleep_until(clock);
		//LogDebug("pushing msg packet 0x%04x", pack->GetFrameNumber());
		Gate2Modem.Push(pack);
	}

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
