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

#include "SafePacketQueue.h"
#include "M17Gateway.h"
#include "Configure.h"
#include "Log.h"

extern CConfigure g_Cfg;
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
	destMap.ReadAll();
	std::string cs(g_Cfg.GetString(g_Keys.general.callsign));
	cs.resize(8, ' ');
	cs.append(1, g_Cfg.GetString(g_Keys.general.module).at(0));
	thisCS.CSIn(cs);
	LogInfo("Station Callsign: %s", cs.c_str());

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

	mlink.state = ELinkState::unlinked;
	keep_running = true;
	currentStream.header.data.streamid = 0;
	mlink.maintainLink = g_Cfg.GetBoolean(g_Keys.gateway.maintainLink);
	LogInfo("Gateway will%s try to re-establish a dropped link", mlink.maintainLink ? "" : " NOT");
	if (g_Cfg.IsString(g_Keys.gateway.startupLink))
	{
		setDestination(g_Cfg.GetString(g_Keys.gateway.startupLink));
	}
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
	// send the packet
	Gate2Host.Push(std::move(Frame));
	// close the stream;
	currentStream.header.data.streamid = 0;
	gateState.Idle();
}

void CM17Gateway::Process()
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
			if (linkingTime.time() >= 30)
			{
				LogInfo("Link request to %s timeout.\n", mlink.cs.GetCS().c_str());
				mlink.state = ELinkState::unlinked;
			}
		}
		else // ELinkState is unlinked
		{
			if (mlink.maintainLink and not mlink.addr.AddressIsZero())
			{
				sendLinkRequest();
			}
		}

		if (currentStream.header.data.streamid && currentStream.lastPacketTime.time() >= 2.0)
		{
			streamTimeout(); // current stream has timed out
		}
		//PlayVoiceFile(); // play if there is any msg to play

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

		if (rval)
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
			case IPFRAMESIZE:
				if (memcmp(buf, "M17 ", 4))
				{
					is_packet = false;
					break;
				}
				// only process if we can set the GateState
				if (gateState.TryState(EGateState::gatein))
					processGate(buf);
				break;
			default:
				is_packet = false;
				break;
			}
			if (! is_packet)
				Dump("Unknown packet", buf, length);
		}

		auto Frame = Host2Gate.Pop();

		if (keep_running)
			processHost();
	}
}

void CM17Gateway::processHost()
{
	auto Frame = Host2Gate.Pop();
	if (! Frame)
		return; // nothing to process

	const CCallsign dest(Frame->data.lich.addr_dst);

}

void CM17Gateway::sendLinkRequest()
{
	mlink.from_mod = thisCS.GetModule();

	// make a CONN packet
	SM17RefPacket conn;
	memcpy(conn.magic, "CONN", 4);
	thisCS.CodeOut(conn.cscode);
	conn.mod = mlink.cs.GetModule();
	writePacket(conn.magic, 11, mlink.addr);	// send the link request
	// go ahead and make the pong packet
	memcpy(mlink.pongPacket.magic, "PONG", 4);
	thisCS.CodeOut(mlink.pongPacket.cscode);

	// finish up
	mlink.state = ELinkState::linking;
	linkingTime.start();
}

void CM17Gateway::processGate(const uint8_t *buf)
{
	auto Frame = std::make_unique<SIPFrame>();
	memcpy(Frame->data.magic, buf, IPFRAMESIZE);
	if (0 == currentStream.header.data.streamid)
	{
		const CCallsign src(Frame->data.lich.addr_src);	// we need this for the log
		const CCallsign dst("#ALL");	// set the destination to #ALL
		dst.CodeOut(Frame->data.lich.addr_src);

		memcpy(currentStream.header.data.magic, Frame->data.magic, IPFRAMESIZE);
		LogInfo("Open stream id=0x%04x from %s at %s\n", Frame->GetStreamID(), src.GetCS().c_str(), from17k.GetAddress());
		Gate2Host.Push(std::move(Frame));
		currentStream.lastPacketTime.start();
	}
	else
	{
		if (currentStream.header.GetStreamID() == Frame->GetStreamID())
		{
			auto sid = Frame->GetStreamID();
			auto fn = Frame->GetFrameNumber();
			currentStream.header.SetFrameNumber(fn);
			Gate2Host.Push(std::move(Frame));
			if (fn & 0x8000u)
			{
				LogInfo("Close stream id=0x%04x, duration=%.2f sec\n", sid, 0.04f * (0x7fffu & fn));
				currentStream.header.SetFrameNumber(0); // close the stream
				currentStream.header.data.streamid = 0;
				gateState.Idle();
			}
			else
			{
				currentStream.lastPacketTime.start();
			}
		}
		else
		{
			return;
		}
	}
	return;
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

// returns false if successful
bool CM17Gateway::setDestination(const std::string &cs)
{
	auto phost = destMap.Find(cs);

	if (phost)
	{
		// prefer IPv6
		if (EInternetType::ipv4only != internetType and not phost->ip6addr.empty())
		{
			mlink.addr.Initialize(phost->ip6addr, phost->port);
			mlink.cs.CSIn(cs);
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
		mlink.cs.CSIn(cs);
		return false;
	}

	LogWarning("Host '%s' not found", cs.c_str());
	return true;
}
