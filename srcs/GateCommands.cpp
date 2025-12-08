/*

         mspot - an M17-only HotSpot using an RPi CC1200 hat
            Copyright (C) 2025 Thomas A. Early N7TAE

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

*/

#include <string>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <thread>
#include <chrono>

#include "SafePacketQueue.h"
#include "SteadyTimer.h"
#include "Gateway.h"
#include "Configure.h"
#include "CRC.h"
#include "Log.h"

extern CCRC g_Crc;
extern CConfigure g_Cfg;
extern SFrameFIFO SFrameModem2Gate;
extern SFrameFIFO SFrameGate2Modem;
extern PFrameFIFO PFrameModem2Gate;
extern PFrameFIFO PFrameGate2Modem;

static const std::string m17alphabet(" ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-/.");

static const std::string fnames[39]
{
	"ECHO",  "alpha",  "bravo"     "charlie", "delta", "echo",    "foxtrot",  "golf",
	"hotel", "india",  "juliette", "kilo",    "lima",  "mike",    "november", "oscar",
	"papa",  "quebec", "romeo",    "sierra",  "tango", "uniform", "victor",   "whiskey",
	"x-ray", "yankee", "zulu",     "zero",    "one",   "two",     "three",    "four",
	"five",  "six",    "seven",    "eight",   "nine",  "dash",    "slash",    "dot"
};

uint16_t CGateway::makeStreamID()
{
		std::uniform_int_distribution<uint16_t> dist(0x0001, 0xFFFE);
		return dist(m_random);
}

void CGateway::wait4end(std::unique_ptr<CPacket> &Packet)
{
	// check the starting packet first
	if (EOTFNMask & Packet->GetFrameNumber())
		return;

	CSteadyTimer ptime;

	// we're only going to reset the packet timer if subsequent incoming packets have this SID
	// this is all internal, so we don't need to convert the SID from network byte order.
	auto streamid = Packet->GetStreamId();

	while (keep_running)
	{
		Packet = Host2Gate.PopWaitFor(100);

		if (Packet and (Packet->GetStreamId() == streamid))
		{
			ptime.start();
			if (0x8000 & Packet->GetFrameNumber())
				break;
		}
		if (ptime.time() > 0.5)
		{
			break;
		}
	}
	return;
}

void CGateway::doStatus(std::unique_ptr<CPacket> &Packet)
{
	wait4end(Packet);
	if (ELinkState::linked == mlink.state)
		addMessage("repeater is_linked_to destination");
	else if (ELinkState::linking == mlink.state)
		addMessage("repeater is_linking");
	else
		addMessage("repeater is_unlinked");
}

void CGateway::doUnlink(std::unique_ptr<CPacket> &Packet)
{
	wait4end(Packet);
	if (ELinkState::unlinked == mlink.state)
	{
		addMessage("repeater is_already_unlinked");
		LogInfo("%s is already unlinked", thisCS.c_str());
	}
	else
	{
		// make and send the DISConnect packet
		SM17RefPacket disc;
		memcpy(disc.magic, "DISC", 4);
		thisCS.CodeOut(disc.cscode);
		sendPacket(disc.magic, 10, mlink.addr);
		LogInfo("DISConnect packet sent to %s", mlink.cs.c_str());
		// the gateway proccess loop will disconnect when is receives the confirming DISC packet.
	}
}

void CGateway::doEcho(std::unique_ptr<CPacket> &Packet)
{
	if (Packet->GetFrameNumber() & 0x8000)
	{
		return;
	}
	auto streamID = Packet->GetStreamId();

	doRecord(' ', streamID);
}

void CGateway::doRecord(std::unique_ptr<CPacket> &Packet)
{
	if (Packet->GetFrameNumber() & EOTFNMask)
	{
		return;
	}
	CCallsign dest(Packet->GetCDstAddress());
	auto streamID = Packet->GetStreamId();
	doRecord(dest.GetModule(), streamID);
}

void CGateway::doRecord(char c, uint16_t streamID)
{
	// record payload in queue
	using payload = std::array<uint8_t, 16>;
	CSafePacketQueue<std::unique_ptr<payload>> fifo;
	uint16_t fn = 0;
	CSteadyTimer timer;
	while (true) // record the payloads in fifo
	{
		auto frame = Host2Gate.PopWaitFor(100);
		if (nullptr == frame)
			continue;
		if (streamID != frame->GetStreamId())
			continue;
		if (timer.time() > 2.0)
			break;
		timer.start();
		if (++fn < 3000) // only collect up to 2 minutes
		{
			auto newpl = std::make_unique<payload>();
			memcpy(newpl->data(), frame->GetCPayload(), 16);
			fifo.Push(newpl);
		}
		if (frame->GetFrameNumber() & EOTFNMask)
			break;
	}
	if (fn > 3000)
	{
		LogWarning("Too long, did not save the last %.2f seconds of the transmission", 0.04f * (fn-3000));
	}
	if (fn < 25)
	{
		while (not fifo.IsEmpty())
			fifo.Pop().reset();
		LogInfo("Only recorded %d milliseconds, not saved", 40 * fn);
		return;
	}

	// now save the data

	// make the file pathname
	auto pos = m17alphabet.find(c);
	if (std::string::npos == pos)
	{
		if (isprint(c))
			LogError("'%c' is not a valid M17 character", c);
		else
			LogError("0x%02x is not a valid M17 character", unsigned(c));
		LogInfo("Invalid character will be replaced with ' '");
		pos = 0;
	}
	std::filesystem::path pathname(audioPath);
	pathname /= fnames[pos];

	// open the file and write the data
	std::ofstream ofs(pathname, std::ios::binary | std::ios::trunc);
	if (ofs.is_open())
	{
		while (true)
		{
			auto pl = fifo.Pop();
			if (not pl)
				break;
			ofs.write((const char *)pl->data(), 16);
		}
		ofs.close();
	}
	else
	{
		LogError("Could not open %s for writing", pathname.c_str());
	}

	// after a short wait
	std::this_thread::sleep_for(std::chrono::milliseconds(500));
	doPlay(c);
}

void CGateway::doPlay(std::unique_ptr<CPacket> &Packet)
{
	wait4end(Packet);
	std::this_thread::sleep_for(std::chrono::milliseconds(300));
	CCallsign dest(Packet->GetCDstAddress());
	doPlay(dest.GetModule());
}

void CGateway::doPlay(char c)
{
	// make the file pathname
	std::filesystem::path pathname(audioPath);
	auto pos = m17alphabet.find(c);
	if (std::string::npos == pos)
	{
		if (isprint(c))
			LogError("'%c' is not a valid M17 character", c);
		else
			LogError("0x%02x is not a valid M17 character", unsigned(c));
		return;
	}
	pathname /= fnames[pos];

	uintmax_t count = 0;

	// make sure the file exists and its size looks okay
	if (std::filesystem::exists(pathname))
	{
		count = std::filesystem::file_size(pathname);
		// no partial payloads or less than 1 sec duration or more than 2 minute
		if ((count % 16) or (count / 16 < 25) or (count / 16 > 3000))
		{
			LogError("'%s' has an unexpected file size of %lu", pathname.c_str(), count);
			return;
		}
	}

	count = count / 16 - 1; // this will be the closing frame number

	// make a frame template
	CPacket master(true);
	// memcpy(master.data.magic, "M17 ", 4);
	master.SetStreamId(makeStreamID());
	memset(master.GetDstAddress(), 0xffu, 6); // set destination to Broadcast
	thisCS.CodeOut(master.GetSrcAddress());
	master.SetFrameType(0x0005 | (can << 7));
	memset(master.GetMetaData(), 0, 14);

	uint16_t fn = 0;
	std::ifstream ifs(pathname, std::ios::binary);
	auto clock = std::chrono::steady_clock::now(); // start the packet clock
	if (ifs.is_open())
	{
		while (fn < count)
		{
			auto frame = std::make_unique<CPacket>(true);
			memcpy(frame->GetData()+4, master.GetCData()+4, IPFRAMESIZE-4);
			ifs.read((char *)(frame->GetPayload()), 16);
			frame->SetFrameNumber((fn < count) ? fn++ : fn++ & EOTFNMask);
			frame->CalcCRC();
			clock = clock + std::chrono::milliseconds(40);
			std::this_thread::sleep_until(clock);
			SFGate2Modem.Push(frame);
		}
		ifs.close();
	}
	else
		LogError("Could not open file '%s'", pathname.c_str());
}
