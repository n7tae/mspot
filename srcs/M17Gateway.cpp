/****************************************************************
 *                                                              *
 *             More - An M17-only Repeater/HotSpot              *
 *                                                              *
 *         Copyright (c) 2024 by Thomas A. Early N7TAE          *
 *                                                              *
 * See the LICENSE file for details about the software license. *
 *                                                              *
 ****************************************************************/


#include <sys/select.h>

#include <string>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <cstring>

#include "SafePacketQueue.h"
#include "M17Gateway.h"
#include "Configure.h"
#include "JsonKeys.h"
#include "CRC.h"
#include "Log.h"

extern CConfigure g_Cfg;
extern SJsonKeys  g_Keys;
extern CCRC       g_Crc;
extern IPFrameFIFO Host2Gate;
extern IPFrameFIFO Gate2Host;

void CM17Gateway::Stop()
{
	keep_running = false;
	if (gateFuture.valid())
		gateFuture.get();
	ipv4.Close();
	ipv6.Close();
}

bool CM17Gateway::Start()
{
	std::string cs(g_Cfg.GetString(g_Keys.general.callsign));
	cs.resize(8, ' ');
	cs.append(1, g_Cfg.GetString(g_Keys.general.module).at(0));
	thisCS.CSIn(cs);

	if (g_Cfg.GetBoolean(g_Keys.gateway.ipv4))
	{
		internetType = g_Cfg.GetBoolean(g_Keys.gateway.ipv6) ? EInternetType::both : EInternetType::ipv4only;
	}
	else if (g_Cfg.GetBoolean(g_Keys.gateway.ipv6))
	{
		internetType = EInternetType::ipv6only;
	}
	else
	{
		LogError("Neither IPv4 or IPV6 is enabled!");
		return true;
	}

	mlink.state = ELinkState::unlinked;

	if (EInternetType::ipv6only != internetType)
	{
		CSockAddress addr;
		if (addr.Initialize(AF_INET, 0, "any")) // use ephemeral port
			return true;
		if (ipv4.Open(addr))
			return true;
	}
	if (EInternetType::ipv4only != internetType)
	{
		CSockAddress addr;
		if (addr.Initialize(AF_INET6, 0, "any")) // use ephemeral port
			return true;
		if (ipv6.Open(addr))
			return true;
	}
	keep_running = true;
	currentStream.header.data.streamid = 0;
	gateFuture = std::async(std::launch::async, &CM17Gateway::Process, this);
	if (! gateFuture.valid())
	{
		LogError("Could not start CM17Gateway::Process()");
		return true;
	}
	return false;
}

void CM17Gateway::streamTimeout()
{
	auto Frame = std::make_unique<SIPFrame>();
	memcpy(Frame->data.magic, currentStream.header.data.magic, IPFRAMESIZE);
	// set the frame number
	uint16_t fn = (currentStream.header.GetFrameNumber() + 1) % 0x8000u;
	Frame->SetFrameNumber(fn | 0x8000u);
	// fill in a silent codec2
	switch (Frame->GetFrameType() & 0x6u) {
	case 0x4u:
		{ //3200
			uint8_t silent[] = { 0x01u, 0x00u, 0x09u, 0x43u, 0x9cu, 0xe4u, 0x21u, 0x08u };
			memcpy(Frame->data.payload,   silent, 8);
			memcpy(Frame->data.payload+8, silent, 8);
		}
		break;
	case 0x6u:
		{ // 1600
			uint8_t silent[] = { 0x01u, 0x00u, 0x04u, 0x00u, 0x25u, 0x75u, 0xddu, 0xf2u };
			memcpy(Frame->data.payload, silent, 8);
		}
		break;
	default:
		break;
	}
	// calculate the crc
	g_Crc.setCRC(Frame->data.magic, IPFRAMESIZE);
	// send the packet
	Gate2Host.Push(std::move(Frame));
	// close the stream;
	currentStream.header.data.streamid = 0;
	gateState.Idle();
}

void CM17Gateway::Process()
{
	fd_set fdset;
	timeval tv;
	int max_nfds = 0;
	const auto ip4fd = ipv4.GetSocket();
	const auto ip6fd = ipv6.GetSocket();
	if ((EInternetType::ipv6only != internetType) && (ip4fd > max_nfds))
		max_nfds = ip4fd;
	if ((EInternetType::ipv4only != internetType) && (ip6fd > max_nfds))
		max_nfds = ip6fd;
	while (keep_running)
	{
		if (ELinkState::linked == mlink.state)
		{
			if (mlink.receivePingTimer.time() > 30) // is the reflector okay?
			{
				// looks like we lost contact
				LogInfo("Disconnected from %s, TIMEOUT...\n", mlink.cs.GetCS().c_str());
				mlink.state = ELinkState::unlinked;
				mlink.addr.Clear();
			}
		}
		else if (ELinkState::linking == mlink.state)
		{
			if (linkingTime.time() >= 5.0)
			{
				LogInfo("Link request to %s timeout.\n", mlink.cs.GetCS().c_str());
				mlink.state = ELinkState::unlinked;
			}
		}

		if (currentStream.header.data.streamid && currentStream.lastPacketTime.time() >= 2.0)
		{
			streamTimeout(); // current stream has timed out
		}
		//PlayVoiceFile(); // play if there is any msg to play

		FD_ZERO(&fdset);
		if (EInternetType::ipv6only != internetType)
			FD_SET(ip4fd, &fdset);
		if (EInternetType::ipv4only != internetType)
			FD_SET(ip6fd, &fdset);
		tv.tv_sec = 0;
		tv.tv_usec = 10000;	// wait up to 10 ms for something to happen
		auto rval = select(max_nfds + 1, &fdset, 0, 0, &tv);
		if (0 > rval)
		{
			LogError("gateway select() error: %s", strerror(errno));
			return;
		}

		bool is_packet = false;
		uint8_t buf[100];
		socklen_t fromlen = sizeof(struct sockaddr_storage);
		int length;

		if (keep_running && (ip4fd >= 0) && FD_ISSET(ip4fd, &fdset))
		{
			length = recvfrom(ip4fd, buf, 100, 0, from17k.GetPointer(), &fromlen);
			is_packet = true;
			FD_CLR(ip4fd, &fdset);
		}

		if (keep_running && (ip6fd >= 0) && FD_ISSET(ip6fd, &fdset))
		{
			length = recvfrom(ip6fd, buf, 100, 0, from17k.GetPointer(), &fromlen);
			is_packet = true;
			FD_CLR(ip6fd, &fdset);
		}

		if (keep_running && is_packet)
		{
			switch (length)
			{
			case 4:  				// DISC, ACKN or NACK
				if ((ELinkState::unlinked != mlink.state) && (from17k == mlink.addr))
				{
					if (0 == memcmp(buf, "ACKN", 4))
					{
						mlink.state = ELinkState::linked;
						LogInfo("Connected to %s\n", mlink.cs.GetCS().c_str());
						mlink.receivePingTimer.start();
					}
					else if (0 == memcmp(buf, "NACK", 4))
					{
						mlink.state = ELinkState::unlinked;
						LogInfo("Link request refused from %s\n", mlink.cs.GetCS().c_str());
						mlink.state = ELinkState::unlinked;
					}
					else if (0 == memcmp(buf, "DISC", 4))
					{
						LogInfo("Disconnected from %s\n", mlink.cs.GetCS().c_str());
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
				if ((ELinkState::linked == mlink.state) && (from17k == mlink.addr))
				{
					if (0 == memcmp(buf, "PING", 4))
					{
						sendPacket(mlink.pongPacket.magic, 10, mlink.addr);
						mlink.receivePingTimer.start();
					}
					else if (0 == memcmp(buf, "DISC", 4))
					{
						mlink.state = ELinkState::unlinked;
					}
					else
					{
						is_packet = false;
					}
				}
				break;
			case IPFRAMESIZE:	// An M17 frame
				is_packet = processFrame(buf);
				break;
			default:
				is_packet = false;
				break;
			}
			if (! is_packet)
				Dump("Unknown packet", buf, length);
		}

		if (keep_running && FD_ISSET(hostfd, &fdset))
		{
			SIPFrame frame;
			length = Host2Gate.Read(frame.data.magic, IPFRAMESIZE);
			const CCallsign dest(frame.data.lich.addr_dst);
			if (0==dest.GetCS(3).compare("M17") || 0==dest.GetCS(3).compare("URF")) // Linking a reflector
			{
				switch (mlink.state)
				{
				case ELinkState::linked:
					if (mlink.cs == dest) // this is heading in to the correct desination
					{
						writePacket(frame.data.magic, IPFRAMESIZE, mlink.addr);
					}
					break;
				case ELinkState::unlinked:
					if ('L' == dest.GetCS().at(7))
					{
						std::string ref(dest.GetCS(7));
						ref.resize(8, ' ');
						ref.resize(9, dest.GetModule());
						const CCallsign d(ref);
						sendLinkRequest(d);
					}
					break;
				default:
					break;
				}
			}
			else if (0 == dest.GetCS().compare("U"))
			{
				SM17RefPacket disc;
				memcpy(disc.magic, "DISC", 4);
				thisCS.CodeOut(disc.cscode);
				writePacket(disc.magic, 10, mlink.addr);
			}
			else
			{
				writePacket(frame.data.magic, IPFRAMESIZE, destination);
			}
			FD_CLR(hostfd, &fdset);
		}
	}
}

void CM17Gateway::setDestAddress(const std::string &address, uint16_t port)
{
	if (std::string::npos == address.find(':'))
		destination.Initialize(AF_INET, port, address.c_str());
	else
		destination.Initialize(AF_INET6, port, address.c_str());
}

void CM17Gateway::sendLinkRequest(const CCallsign &ref)
{
	mlink.addr = destination;
	mlink.cs = ref;
	mlink.from_mod = thisCS.GetModule();

	// make a CONN packet
	SM17RefPacket conn;
	memcpy(conn.magic, "CONN", 4);
	thisCS.CodeOut(conn.cscode);
	conn.mod = ref.GetModule();
	writePacket(conn.magic, 11, mlink.addr);	// send the link request
	// go ahead and make the pong packet
	memcpy(mlink.pongPacket.magic, "PONG", 4);
	thisCS.CodeOut(mlink.pongPacket.cscode);

	// finish up
	mlink.state = ELinkState::linking;
	linkingTime.start();
}

bool CM17Gateway::processFrame(const uint8_t *buf)
{
	SIPFrame frame;
	memcpy(frame.data.magic, buf, IPFRAMESIZE);
	if (currentStream.header.data.streamid)
	{
		if (currentStream.header.data.streamid == frame.data.streamid)
		{
			Gate2Host.Write(frame.data.magic, IPFRAMESIZE);
			currentStream.header.SetFrameNumber(frame.GetFrameNumber());
			uint16_t fn = frame.GetFrameNumber();
			if (fn & 0x8000u)
			{
				LogInfo("Close stream id=0x%04x, duration=%.2f sec\n", frame.GetStreamID(), 0.04f * (0x7fffu & fn));
				currentStream.header.SetFrameNumber(0); // close the stream
				currentStream.header.data.streamid = 0;
				streamLock.unlock();
			}
			else
			{
				currentStream.lastPacketTime.start();
			}
		}
		else
		{
			return false;
		}
	}
	else
	{
		// here comes a first packet, so try to lock it
		if (streamLock.try_lock())
		{
			// then init the currentStream
			if (g_Crc.checkCRC(frame.data.magic, IPFRAMESIZE))
				LogWarning("CRC of frame# %u doesn't match", frame.GetFrameNumber());
			memcpy(currentStream.header.data.magic, frame.data.magic, IPFRAMESIZE);
			Gate2Host.Write(frame.data.magic, IPFRAMESIZE);
			const CCallsign call(frame.data.lich.addr_src);
			LogInfo("Open stream id=0x%04x from %s at %s\n", frame.GetStreamID(), call.GetCS().c_str(), from17k.GetAddress());
			currentStream.lastPacketTime.start();
		}
		else
		{
			return false;
		}
	}
	return true;
}

void CM17Gateway::writePacket(const void *buf, const size_t size, const CSockAddress &addr) const
{
	if (AF_INET6 == addr.GetFamily())
		ipv6.Write(buf, size, addr);
	else
		ipv4.Write(buf, size, addr);
}

void CM17Gateway::sendPacket(const void *buf, size_t size, const CSockAddress &addr) const
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
