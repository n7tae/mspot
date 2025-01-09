/****************************************************************
 *                                                              *
 *             More - An M17-only Repeater/HotSpot              *
 *                                                              *
 *         Copyright (c) 2024 by Thomas A. Early N7TAE          *
 *                                                              *
 * See the LICENSE file for details about the software license. *
 *                                                              *
 ****************************************************************/

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
#include "M17Gateway.h"
#include "Configure.h"
#include "CRC.h"
#include "Log.h"

extern CConfigure g_Cfg;
extern CCRC       g_Crc;
extern IPFrameFIFO Host2Gate;
extern IPFrameFIFO Gate2Host;

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

void CM17Gateway::Stop()
{
	keep_running = false;
	if (gateFuture.valid())
		gateFuture.get();
	if (hostFuture.valid())
		hostFuture.get();
	ipv4.Close();
	ipv6.Close();
}

bool CM17Gateway::Start()
{
	destMap.ReadAll();
	std::string cs(g_Cfg.GetString(g_Keys.reflector.section, g_Keys.reflector.callsign));
	cs.resize(8, ' ');
	cs.append(1, g_Cfg.GetString(g_Keys.reflector.section, g_Keys.reflector.module).at(0));
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
		LogInfo("Gateway supports Dual Stack (IPv4 and IPv6");
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

	can = g_Cfg.GetUnsigned(g_Keys.reflector.section, g_Keys.reflector.can);

	mlink.state = ELinkState::unlinked;
	keep_running = true;
	hostStream.in_stream = gateStream.in_stream = false;
	hostStream.streamid = gateStream.streamid = 0;
	mlink.maintainLink = g_Cfg.GetBoolean(g_Keys.gateway.section, g_Keys.gateway.maintainLink);
	LogInfo("Gateway will%s try to re-establish a dropped link", mlink.maintainLink ? "" : " NOT");
	if (g_Cfg.IsString(g_Keys.gateway.section, g_Keys.gateway.startupLink))
	{
		setDestination(g_Cfg.GetString(g_Keys.gateway.section, g_Keys.gateway.startupLink));
	}

	audioPath.assign(g_Cfg.GetString(g_Keys.gateway.section, g_Keys.gateway.audioFolder));
	LogInfo("Audio folder is at %s", audioPath.c_str());

	makeCSData(thisCS, "repeater.dat");

	gateFuture = std::async(std::launch::async, &CM17Gateway::ProcessGateway, this);
	if (not gateFuture.valid())
	{
		LogError("Could not start the ProcessGateway() thread");
		keep_running = false;
		return true;
	}

	hostFuture = std::async(std::launch::async, &CM17Gateway::ProcessHost, this);
	if (not hostFuture.valid())
	{
		LogError("Could not start the ProcessHost() thread");
		keep_running = false;
		return true;
	}
	return false;
}

void CM17Gateway::ProcessGateway()
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
				LogInfo("Disconnected from %s, TIMEOUT...\n", mlink.cs.GetCS().c_str());
				mlink.state = ELinkState::unlinked;
				if (not mlink.maintainLink)
					mlink.addr.Clear();
			}
			break;
		case ELinkState::linking:
			if (linkingTime.time() >= 30)
			{
				LogInfo("Link request to %s timeout.\n", mlink.cs.GetCS().c_str());
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
				sendLinkRequest();
				linkingTime.start();
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
			gateState.Idle();
		}

		// can we play an audio message?
		if (not voiceQueue.empty())
		{
			if (gateState.SetStateOnlyIfIdle(EGateState::messagein))
			{
				auto message = voiceQueue.front();
				voiceQueue.pop();
				if (msgTask)
				{
					LogError("Trying to initiate message play, the gateState was idle, but msgTask is not empty!");
					LogError("'%s' will not be played!", message.c_str());
					gateState.Idle();
				}
				else
				{
					LogInfo("Playing message '%s'", message.c_str());
					msgTask = std::make_unique<SMessageTask>();
					msgTask->isDone = false;
					msgTask->futTask = std::async(std::launch::async, &CM17Gateway::PlayVoiceFiles, this, message);
				}
			}
			else
			{
				LogInfo("VoiceQueue not empty but state is %s", gateState.GetState());
			}
		}

		// any packets from IPv4 or 6?
		auto rval = poll(pfds, 2, 10);
		if (0 > rval)
		{
			LogError("gateway poll() error: %s", strerror(errno));
			return;
		}

		bool is_packet = false;
		uint8_t buf[100];
		socklen_t fromlen = sizeof(struct sockaddr_storage);
		int length;

		if (rval)	// receive any packet
		{
			if (pfds[0].revents & POLLIN)
			{
				length = recvfrom(pfds[0].fd, buf, 100, 0, from17k.GetPointer(), &fromlen);
				pfds[0].revents &= ~POLLIN;
				is_packet = true;
			}
			else if (pfds[1].revents & POLLIN)
			{
				length = recvfrom(pfds[1].fd, buf, 100, 0, from17k.GetPointer(), &fromlen);
				pfds[1].revents &= ~POLLIN;
				is_packet = true;
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

		if (is_packet)
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
						LogInfo("Connected to %s", mlink.cs.c_str());
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
						LogInfo("Disconnected from %s\n", mlink.cs.c_str());
						mlink.addr.Clear(); // initiated with UNLINK, so don't try to reconnect
						mlink.cs.Clear();   // ^^^^^^^^^ ^^^^ ^^^^^^
						mlink.state = ELinkState::unlinked;
					}
					else
					{
						is_packet = false;
					}
				}
				else
				{
					is_packet = false;
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
						is_packet = false;
					}
				}
				break;
			case IPFRAMESIZE:
				if (memcmp(buf, "M17 ", 4))
				{
					is_packet = false;
					break;
				}
				// only process if we can set the GateState
				if (gateState.TryState(EGateState::gatein))
					sendPacket2Host(buf);
				break;
			default:
				is_packet = false;
				break;
			}
			if (not is_packet)
				Dump("Unknown packet", buf, length);
		}

		// check for a stream timeout
		if (gateStream.in_stream and gateStream.lastPacketTime.time() >= 1.6)
		{
			LogInfo("Gateway stream timeout id=0x%02hu", ntohs(gateStream.streamid));
			gateStream.in_stream = false;
			gateState.Idle();
			continue;
		}
	}
}

void CM17Gateway::ProcessHost()
{
	while (keep_running)
	{
		auto Frame = Host2Gate.PopWaitFor(100);
		if (Frame)
		{
			// if TryState fails, the frame will be reset
			if (gateState.TryState(EGateState::modemin))
			{
				const CCallsign dest(Frame->data.lich.addr_dst);
				switch (dest.GetBase())
				{
				case CalcCSCode("E"):
				case CalcCSCode("ECHO"):
					doEcho(Frame);
					gateState.Idle();
					break;
				case CalcCSCode("I"):
				case CalcCSCode("STATUS"):
					doStatus(Frame);
					gateState.Idle();
					break;
				case CalcCSCode("U"):
				case CalcCSCode("UNLINK"):
					doUnlink(Frame);
					gateState.Idle();
					break;
				case CalcCSCode("RECORD"):
					doRecord(Frame);
					gateState.Idle();
					break;
				case CalcCSCode("PLAY"):
					doPlay(Frame);
					gateState.Idle();
					break;
				default:
					const CCallsign dest(Frame->data.lich.addr_dst); // where are we going?
					// the only way we are going to send this anywhere is if we are linked
					switch (mlink.state)
					{
					case ELinkState::linked:
						if (dest == mlink.cs) // is the destination the linked reflector?
						{
							sendPacket2Dest(Frame);
						}
						else
						{
							wait4end(Frame);
							LogWarning("Destination is %s but you are already linked to %s", dest.c_str(), mlink.cs.c_str());
							gateState.Idle();
						}
						break;
					case ELinkState::linking:
						wait4end(Frame);
						if (dest == mlink.cs)
							LogInfo("%s is not yet linked", dest.c_str());
						else
							LogWarning("Destination is %s but you are linking to %s", dest.c_str(), mlink.cs.c_str());
						gateState.Idle();
						break;
					case ELinkState::unlinked:
						wait4end(Frame);
						if (setDestination(dest))
						{
							mlink.state = ELinkState::linking;
						}
						gateState.Idle();
					}
				}
			}
		}
		else
		{
			// check for a timeout from the host
			if (hostStream.in_stream and hostStream.lastPacketTime.time() >= 1.0)
			{
				LogInfo("Modem stream timeout id=0x%02hu", ntohs(gateStream.streamid));
				hostStream.in_stream = false; // close the hostStream
				gateState.Idle();
			}
		}
	}
}

void CM17Gateway::sendLinkRequest()
{
	mlink.from_mod = thisCS.GetModule();

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
	// finish up
	lastLinkSent.start();
	mlink.state = ELinkState::linking;
}

// this also opens and closes the gateStream
void CM17Gateway::sendPacket2Host(const uint8_t *buf)
{
	static unsigned streamcount = 0;
	auto Frame = std::make_unique<SIPFrame>();
	memcpy(Frame->data.magic, buf, IPFRAMESIZE);
	if (gateStream.in_stream)	// is the stream open?
	{
		if (gateStream.streamid == Frame->data.streamid)
		{
			streamcount++;
			auto sid = Frame->GetStreamID();
			auto fn = Frame->GetFrameNumber();
			memset(Frame->data.lich.addr_dst, 0xffu, 6);
			g_Crc.setCRC(Frame->data.magic, IPFRAMESIZE);
			Gate2Host.Push(Frame);
			if (fn & EOTFNMask)
			{
				LogInfo("Close Gate stream id=0x%04x, duration=%.2f sec", sid, 0.04f * streamcount);
				gateStream.in_stream = false; // close the stream
				gateState.Idle();
			}
			else
			{
				gateStream.lastPacketTime.start();
			}
		}
		else
		{
			Frame.reset();  // this frame doesn't belong to the open stream
		}
	}
	else
	{
		if (EOTFNMask & Frame->GetFrameNumber()) // don't open a stream on a last packet
		{
			Frame.reset();
			gateState.Idle();
			return;
		}
		// don't open a stream if this packet has the same SID as the last stream
		// and the last stream closed less than 1 second ago
		// NOTE: It's theoretically possible that this is actually a new stream that has the same SID.
		//       In that case, up to a second of the beginning of the stream will be lost. Sorry.
		if (Frame->data.streamid == gateStream.streamid and gateStream.lastPacketTime.time() < 1.0)
		{
			Frame.reset();
			gateState.Idle();
			return;
		}

		// Start the stream!!
		gateStream.in_stream = true;
		streamcount = 0;

		// remember the SID
		gateStream.streamid = Frame->data.streamid;

		// we need source callsign for the log
		const CCallsign src(Frame->data.lich.addr_src);

		LogInfo("Open Gate stream id=0x%04x from %s at %s", Frame->GetStreamID(), src.GetCS().c_str(), from17k.GetAddress());
		Gate2Host.Push(Frame);
		gateStream.lastPacketTime.start();
	}
	return;
}

// this also opens and closes the hostStream
void CM17Gateway::sendPacket2Dest(std::unique_ptr<SIPFrame> &Frame)
{
	static unsigned streamcount = 0;

	if (hostStream.in_stream)	// is the stream open?
	{
		if (hostStream.streamid == Frame->data.streamid)
		{
			streamcount++;
			auto sid = Frame->GetStreamID();
			auto fn = Frame->GetFrameNumber();
			g_Crc.setCRC(Frame->data.magic, IPFRAMESIZE);
			sendPacket(Frame->data.magic, IPFRAMESIZE, mlink.addr);
			if (fn & EOTFNMask)
			{
				LogInfo("Close modem stream id=0x%04x, duration=%.2f sec", sid, 0.04f * streamcount);
				hostStream.in_stream = false; // close the stream
				gateState.Idle();
			}
			else
			{
				hostStream.lastPacketTime.start();
			}
		}
		else
		{
			Frame.reset(); // this frame has the wrong SID
		}
	}
	else
	{
		if (EOTFNMask & Frame->GetFrameNumber()) // don't open a stream on a last packet
		{
			Frame.reset();
			gateState.Idle();
			return;
		}
		// don't open a stream if this packet has the same SID as the last stream
		if (Frame->data.streamid == hostStream.streamid)
		{
			Frame.reset();
			gateState.Idle();
			return;
		}

		// Open the Stream!!
		hostStream.in_stream = true;
		streamcount = 0;

		// remember the SID
		hostStream.streamid = Frame->data.streamid;

		// we need source callsign for the log
		const CCallsign src(Frame->data.lich.addr_src);

		LogInfo("Open modem stream id=0x%04x from %s", Frame->GetStreamID(), src.c_str());
		sendPacket(Frame->data.magic, IPFRAMESIZE, mlink.addr);
		hostStream.lastPacketTime.start();
	}
}

void CM17Gateway::sendPacket(const void *buf, const size_t size, const CSockAddress &addr) const
{
	if (AF_INET ==  addr.GetFamily())
		ipv4.Write(buf, size, addr);
	else
		ipv6.Write(buf, size, addr);
}

void CM17Gateway::Dump(const char *title, const void *pointer, int length)
{
	const unsigned char *data = (const unsigned char *)pointer;

	std::cout << title << std::endl;

	unsigned int offset = 0U;

	while (length > 0) {

		unsigned int bytes = (length > 16) ? 16U : length;

		for (unsigned i = 0U; i < bytes; i++) {
			if (i)
				std::cout << " ";
			std::cout << std::hex << std::setw(2) << std::right << std::setfill('0') << int(data[offset + i]);
		}

		for (unsigned int i = bytes; i < 16U; i++)
			std::cout << "   ";

		std::cout << "   *";

		for (unsigned i = 0U; i < bytes; i++) {
			unsigned char c = data[offset + i];

			if (::isprint(c))
				std::cout << c;
			else
				std::cout << '.';
		}

		std::cout << '*' << std::endl;

		offset += 16U;

		if (length >= 16)
			length -= 16;
		else
			length = 0;
	}
}

// returns false if successful
bool CM17Gateway::setDestination(const std::string &callsign)
{
	const CCallsign cs(callsign);
	return setDestination(cs);
}

bool CM17Gateway::setDestination(const CCallsign &cs)
{
	auto phost = destMap.Find(cs.c_str());

	if (phost)
	{
		// prefer IPv6
		if (EInternetType::ipv4only != internetType and not phost->ip6addr.empty())
		{
			mlink.addr.Initialize(phost->ip6addr, phost->port);
			mlink.cs = cs;
			return false;
		}

		// if this is IPv6 only, we're done
		if (EInternetType::ipv6only == internetType)
		{
			LogWarning("This IPv6-only system could not find an IPv6 address for '%s'", cs.c_str());
			return true;
		}

		// if the host is IPv6 only, were also done
		if (phost->ip4addr.empty())
		{
			LogWarning("There is no IPv4 address for '%s'", cs.c_str());
			return true;
		}

		// this is the default IPv4 address
		mlink.addr.Initialize(phost->ip4addr, phost->port);
		mlink.cs = cs;
		return false;
	}

	LogWarning("Host '%s' not found", cs.c_str());
	return true;
}

void CM17Gateway::addMessage(const std::string &message)
{
	voiceQueue.push(message);
}

void CM17Gateway::makeCSData(const CCallsign &cs, const std::string &ofileName)
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
		if (pos+3 >= callsign.size() or callsign.compare(pos, 3, "M17"))
		{
			// no "M17" at this pos, so find the m17 alphabet index
			indx = m17_ab.find(callsign[pos]);
			if (indx > 40u)
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
			// add 80 ms of quiet between each word
			for (unsigned n=0; n<4; n++)
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

unsigned CM17Gateway::PlayVoiceFiles(std::string message)
{
	unsigned count = 0u;	// this counts the number of 20ms half-payloads

	// make a voice frame template
	// we'll still need to add the payload, frame counter and the CRC before sending it to the modem.
	SIPFrame master;
	memcpy(master.data.magic, "M17 ", 4);
	master.SetStreamID(makeStreamID());
	memset(master.data.lich.addr_dst, 0xffu, 6); // set destination to Broadcast
	thisCS.CodeOut(master.data.lich.addr_src);
	master.SetFrameType(0x0005 | (can << 7));
	memset(master.data.lich.meta, 0, 14);

	auto clock = std::chrono::steady_clock::now(); // start the packet clock
	std::ifstream ifile;

	std::queue<std::string> words;
	split(message, ' ', words);

	std::this_thread::sleep_for(std::chrono::milliseconds(500));

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
				ifile.read(reinterpret_cast<char *>(master.data.payload+8), 8);
				// now finsih off the packet
				uint16_t fn = ((count / 2u) % 0x8000u);
				if (words.empty() and (i == fsize))
					fn |= 0x8000u; // nothing left to read, mark the end of the stream
				master.SetFrameNumber(fn);
				g_Crc.setCRC(master.data.magic, IPFRAMESIZE); // seal it with the CRC

				// make the packet to pass to the modem
				auto frame = std::make_unique<SIPFrame>();
				memcpy(frame->data.magic, master.data.magic, IPFRAMESIZE);
				clock = clock + std::chrono::milliseconds(40);
				std::this_thread::sleep_until(clock); // the frames will go out every 40 milliseconds
				Gate2Host.Push(frame);
			}
			else
			{	// counter is even, this is the first 20 ms of C2_3200 data
				ifile.read(reinterpret_cast<char *>(master.data.payload), 8);
			}
			count++;
		}
		ifile.close();
		if (not words.empty())
		{
			// add 80 ms of quiet between files
			for (unsigned i=0; i<8; i++)
			{
				if (count % 2)
				{	// counter is odd, put this in the second half
					memcpy(master.data.payload+8, quiet, 8);
					uint16_t fn = ((count / 2u) % 0x8000u);
					master.SetFrameNumber(fn);
					g_Crc.setCRC(master.data.magic, IPFRAMESIZE);
					auto frame = std::make_unique<SIPFrame>();
					memcpy(frame->data.magic, master.data.magic, IPFRAMESIZE);
					clock = clock + std::chrono::milliseconds(40);
					std::this_thread::sleep_until(clock);
					Gate2Host.Push(frame);
				}
				else
				{	// counter is even, this goes in the first half
					memcpy(master.data.payload, quiet, 8);
				}
				count++;
			}
		}
	}
	if (count % 2) // if this is true, we need to complete and send the last packet
	{
		memcpy(master.data.payload+8, quiet, 8);
		uint16_t fn = ((count %0x8000u) / 2u) + 0x8000u;
		master.SetFrameNumber(fn);
		g_Crc.setCRC(master.data.magic, IPFRAMESIZE);
		auto frame = std::make_unique<SIPFrame>();
		memcpy(frame->data.magic, master.data.magic, IPFRAMESIZE);
		clock = clock + std::chrono::milliseconds(40);
		std::this_thread::sleep_until(clock);
		Gate2Host.Push(frame);
	}

	// this thread can be harvested
	msgTask->isDone = true;
	// return the number of packets sent
	return count / 2u;
}
