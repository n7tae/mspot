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

#include "M17Gateway.h"
#include "Configure.h"
#include "JsonKeys.h"
#include "Log.h"

extern CConfigure g_Cfg;
extern SJsonKeys  g_Keys;

void CM17Gateway::Close()
{
	Host2Gate.Close();
	ipv4.Close();
	ipv6.Close();
}

bool CM17Gateway::TryLock()
{
	return streamLock.try_lock();
}

void CM17Gateway::ReleaseLock()
{
	streamLock.unlock();
}

bool CM17Gateway::Initialize()
{
	std::string cs(g_Cfg.GetString(g_Keys.general.callsign));
	cs.resize(8, ' ');
	cs.append(1, g_Cfg.GetString(g_Keys.general.module).at(0));
	thisCS.CSIn(cs);

	if (g_Cfg.GetBoolean(g_Keys.gate.ipv4))
	{
		internetType = g_Cfg.GetBoolean(g_Keys.gate.ipv6) ? EInternetType::both : EInternetType::ipv4only;
	}
	else if (g_Cfg.GetBoolean(g_Keys.gate.ipv6))
	{
		internetType = EInternetType::ipv6only;
	}
	else
	{
		LogError("Neither IPv4 or IPV6 is enabled!");
		return true;
	}

	mlink.state = ELinkState::unlinked;
	if (Host2Gate.Open("host2gate"))
		return true;
	Gate2Host.SetUp("gate2host");

	if (EInternetType::ipv6only != internetType)
	{
		if (ipv4.Open(CSockAddress(AF_INET, 0, "any")))  // use ephemeral port
			return true;
	}
	if (EInternetType::ipv4only != internetType)
	{
		if (ipv6.Open(CSockAddress(AF_INET6, 0, "any"))) // use ephemeral port
			return true;
	}
	keep_running = true;
	currentStream.header.streamid = 0;
	CConfigure config;
	return false;
}

void CM17Gateway::linkCheck()
{
	if (mlink.receivePingTimer.time() > 30) // is the reflector okay?
	{
		// looks like we lost contact
		LogInfo("Disconnected from %s, TIMEOUT...\n", mlink.cs.GetCS().c_str());
		mlink.state = ELinkState::unlinked;
		mlink.addr.Clear();
	}
}

void CM17Gateway::StreamTimeout()
{
	// set the frame number
	uint16_t fn = (currentStream.header.GetFrameNumber() + 1) % 0x8000u;
	currentStream.header.SetFrameNumber(fn | 0x8000u);
	// fill in a silent codec2
	switch (currentStream.header.GetFrameType() & 0x6u) {
	case 0x4u:
		{ //3200
			uint8_t silent[] = { 0x01u, 0x00u, 0x09u, 0x43u, 0x9cu, 0xe4u, 0x21u, 0x08u };
			memcpy(currentStream.header.payload,   silent, 8);
			memcpy(currentStream.header.payload+8, silent, 8);
		}
		break;
	case 0x6u:
		{ // 1600
			uint8_t silent[] = { 0x01u, 0x00u, 0x04u, 0x00u, 0x25u, 0x75u, 0xddu, 0xf2u };
			memcpy(currentStream.header.payload, silent, 8);
		}
		break;
	default:
		break;
	}
	// calculate the crc
	currentStream.header.SetCRC(crc.CalcCRC(currentStream.header));
	// send the packet
	Gate2Host.Write(currentStream.header.magic, sizeof(SM17Frame));
	// close the stream;
	currentStream.header.streamid = 0;
	streamLock.unlock();
}

void CM17Gateway::PlayVoiceFile()
{
		// play a qnvoice file if it is specified
		// this could be coming from qnvoice or qngateway (connected2network or notincache)
		std::ifstream voicefile(qnvoice_file.c_str(), std::ifstream::in);
		if (voicefile)
		{
			if (keep_running)
			{
				char line[FILENAME_MAX];
				voicefile.getline(line, FILENAME_MAX);
				// trim whitespace
				char *start = line;
				while (isspace(*start))
					start++;
				char *end = start + strlen(start) - 1;
				while (isspace(*end))
					*end-- = (char)0;
				// anthing reasonable left?
				if (strlen(start) > 2)
					PlayAudioNotifyMessage(start);
			}
			//clean-up
			voicefile.close();
			remove(qnvoice_file.c_str());
		}

}

void CM17Gateway::PlayAudioNotifyMessage(const char *msg)
{
	if (strlen(msg) > sizeof(SM17Frame) - 5)
	{
		fprintf(stderr, "Audio Message string too long: %s", msg);
		return;
	}
	SM17Frame frame;
	memcpy(frame.magic, "PLAY", 4);
	memcpy(frame.magic+4, msg, strlen(msg)+1);	// copy the terminating NULL
	Gate2Host.Write(frame.magic, sizeof(SM17Frame));
}

void CM17Gateway::Process()
{
	fd_set fdset;
	timeval tv;
	int max_nfds = 0;
	const auto ip4fd = ipv4.GetSocket();
	const auto ip6fd = ipv6.GetSocket();
	const auto amfd = Host2Gate.GetFD();
	if ((EInternetType::ipv6only != internetType) && (ip4fd > max_nfds))
		max_nfds = ip4fd;
	if ((EInternetType::ipv4only != internetType) && (ip6fd > max_nfds))
		max_nfds = ip6fd;
	if (amfd > max_nfds)
		max_nfds = amfd;
	while (keep_running)
	{
		if (ELinkState::linked == mlink.state)
		{
			linkCheck();
		}
		else if (ELinkState::linking == mlink.state)
		{
			if (linkingTime.time() >= 5.0)
			{
				LogInfo("Link request to %s timeout.\n", mlink.cs.GetCS().c_str());
				mlink.state = ELinkState::unlinked;
			}
		}

		if (currentStream.header.streamid && currentStream.lastPacketTime.time() >= 2.0)
		{
			StreamTimeout(); // current stream has timed out
		}
		PlayVoiceFile(); // play if there is any msg to play

		FD_ZERO(&fdset);
		if (EInternetType::ipv6only != internetType)
			FD_SET(ip4fd, &fdset);
		if (EInternetType::ipv4only != internetType)
			FD_SET(ip6fd, &fdset);
		FD_SET(amfd, &fdset);
		tv.tv_sec = 0;
		tv.tv_usec = 40000;	// wait up to 40 ms for something to happen
		auto rval = select(max_nfds + 1, &fdset, 0, 0, &tv);
		if (0 > rval)
		{
			std::cerr << "select() error: " << strerror(errno) << std::endl;
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
						Send(mlink.pongPacket.magic, 10, mlink.addr);
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
			case sizeof(SM17Frame):	// An M17 frame
				is_packet = ProcessFrame(buf);
				break;
			default:
				is_packet = false;
				break;
			}
			if (! is_packet)
				Dump("Unknown packet", buf, length);
		}

		if (keep_running && FD_ISSET(amfd, &fdset))
		{
			SM17Frame frame;
			length = Host2Gate.Read(frame.magic, sizeof(SM17Frame));
			const CCallsign dest(frame.lich.addr_dst);
			//printf("DEST=%s=0x%02x%02x%02x%02x%02x%02x\n", dest.GetCS().c_str(), frame.lich.addr_dst[0], frame.lich.addr_dst[1], frame.lich.addr_dst[2], frame.lich.addr_dst[3], frame.lich.addr_dst[4], frame.lich.addr_dst[5]);
			//std::cout << "Read " << length << " bytes with dest='" << dest.GetCS() << "'" << std::endl;
			if (0==dest.GetCS(3).compare("M17") || 0==dest.GetCS(3).compare("URF")) // Linking a reflector
			{
				switch (mlink.state)
				{
				case ELinkState::linked:
					if (mlink.cs == dest) // this is heading in to the correct desination
					{
						Write(frame.magic, sizeof(SM17Frame), mlink.addr);
					}
					break;
				case ELinkState::unlinked:
					if ('L' == dest.GetCS().at(7))
					{
						std::string ref(dest.GetCS(7));
						ref.resize(8, ' ');
						ref.resize(9, dest.GetModule());
						const CCallsign d(ref);
						SendLinkRequest(d);
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
				Write(disc.magic, 10, mlink.addr);
			} else {
				Write(frame.magic, sizeof(SM17Frame), destination);
			}
			FD_CLR(amfd, &fdset);
		}
	}
}

void CM17Gateway::SetDestAddress(const std::string &address, uint16_t port)
{
	if (std::string::npos == address.find(':'))
		destination.Initialize(AF_INET, port, address.c_str());
	else
		destination.Initialize(AF_INET6, port, address.c_str());
}

void CM17Gateway::SendLinkRequest(const CCallsign &ref)
{
	mlink.addr = destination;
	mlink.cs = ref;
	mlink.from_mod = thisCS.GetModule();

	// make a CONN packet
	SM17RefPacket conn;
	memcpy(conn.magic, "CONN", 4);
	thisCS.CodeOut(conn.cscode);
	conn.mod = ref.GetModule();
	Write(conn.magic, 11, mlink.addr);	// send the link request
	// go ahead and make the pong packet
	memcpy(mlink.pongPacket.magic, "PONG", 4);
	thisCS.CodeOut(mlink.pongPacket.cscode);

	// finish up
	mlink.state = ELinkState::linking;
	linkingTime.start();
}

bool CM17Gateway::ProcessFrame(const uint8_t *buf)
{
	SM17Frame frame;
	memcpy(frame.magic, buf, sizeof(SM17Frame));
	if (currentStream.header.streamid)
	{
		if (currentStream.header.streamid == frame.streamid)
		{
			Gate2Host.Write(frame.magic, sizeof(SM17Frame));
			currentStream.header.SetFrameNumber(frame.GetFrameNumber());
			uint16_t fn = frame.GetFrameNumber();
			if (fn & 0x8000u)
			{
				LogInfo("Close stream id=0x%04x, duration=%.2f sec\n", frame.GetStreamID(), 0.04f * (0x7fffu & fn));
				currentStream.header.SetFrameNumber(0); // close the stream
				currentStream.header.streamid = 0;
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
			auto check = crc.CalcCRC(frame);
			if (frame.GetCRC() != check)
				std::cout << "Header Packet crc=0x" << std::hex << frame.GetCRC() << " calculate=0x" << std::hex << check << std::endl;
			memcpy(currentStream.header.magic, frame.magic, sizeof(SM17Frame));
			Gate2Host.Write(frame.magic, sizeof(SM17Frame));
			const CCallsign call(frame.lich.addr_src);
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

void CM17Gateway::Write(const void *buf, const size_t size, const CSockAddress &addr) const
{
	if (AF_INET6 == addr.GetFamily())
		ipv6.Write(buf, size, addr);
	else
		ipv4.Write(buf, size, addr);
}

void CM17Gateway::PlayAudioMessage(const char *msg)
{
	auto len = strlen(msg);
	if (len > sizeof(SM17Frame)-5)
	{
		fprintf(stderr, "Audio Message string too long: %s", msg);
		return;
	}
	SM17Frame m17;
	memcpy(m17.magic, "PLAY", 4);
	memcpy(m17.magic+4, msg, len+1);	// copy the terminating NULL
	Gate2Host.Write(m17.magic, sizeof(SM17Frame));
}

void CM17Gateway::Send(const void *buf, size_t size, const CSockAddress &addr) const
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
