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

#include <filesystem>
#include <sstream>
#include <fstream>
#include <string>
#include <thread>
#include <chrono>

#include <poll.h>

#include "SafePacketQueue.h"
#include "SteadyTimer.h"
#include "FrameType.h"
#include "Configure.h"
#include "GateState.h"
#include "Gateway.h"
#include "Random.h"
#include "CRC.h"

extern CCRC g_Crc;
extern CRandom g_RNG;
extern CConfigure g_Cfg;
extern CGateState g_GateState;
extern IPFrameFIFO Modem2Gate;
extern IPFrameFIFO Gate2Modem;

static const std::string m17alphabet(" ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-/.");

static const std::string fnames[40]{
	"ECHO", "alpha", "bravo", "charlie", "delta", "echo", "foxtrot", "golf",
	"hotel", "india", "juliette", "kilo", "lima", "mike", "november", "oscar",
	"papa", "quebec", "romeo", "sierra", "tango", "uniform", "victor", "whiskey",
	"x-ray", "yankee", "zulu", "zero", "one", "two", "three", "four",
	"five", "six", "seven", "eight", "nine", "dash", "slash", "dot"
};

void CGateway::wait4end(std::unique_ptr<CPacket> &p)
{
	// check the starting packet first
	if (p->IsLastPacket())
		return;

	// we're only going to reset the packet timer if subsequent incoming packets have this SID
	auto streamid = p->GetStreamId();

	CSteadyTimer ptime; // the timeout timer
	while (keep_running)
	{
		auto p = Modem2Gate.PopWaitFor(40);
		if (p)
		{
			p->Initialize(EPacketType::stream);
			if (p->GetStreamId() == streamid)
			{
				if (p->IsLastPacket())
					break;
			}
			ptime.start();
		}
		if (ptime.time() > 0.5)
			break; // a timeout!
	}
	return;
}

void CGateway::doStatus(std::unique_ptr<CPacket> &p)
{
	wait4end(p);
	if (ELinkState::linked == mlink.state)
		addMessage("repeater is_linked_to destination");
	else if (ELinkState::linking == mlink.state)
		addMessage("repeater is_linking");
	else
		addMessage("repeater is_unlinked");
}

void CGateway::doUnlink(std::unique_ptr<CPacket> &p)
{
	wait4end(p);
	if (ELinkState::unlinked == mlink.state)
	{
		addMessage("repeater is_already_unlinked");
		printMsg(TC_MAGENTA, TC_YELLOW, "%s is already unlinked\n", thisCS.c_str());
	}
	else
	{
		// make and send the DISConnect packet
		SM17RefPacket disc;
		memcpy(disc.magic, "DISC", 4);
		thisCS.CodeOut(disc.cscode);
		sendPacket(disc.magic, 10, mlink.addr);
		printMsg(TC_MAGENTA, TC_GREEN, "DISConnect packet sent to %s", mlink.cs.c_str());
		// the gateway proccess loop will disconnect when is receives the confirming DISC packet.
	}
}

void CGateway::doEcho(std::unique_ptr<CPacket> &p)
{
	if (p->IsLastPacket())
	{
		return;
	}
	auto streamID = p->GetStreamId();

	doRecord(' ', streamID);
}

void CGateway::doRecord(std::unique_ptr<CPacket> &p)
{
	if (p->IsLastPacket())
	{
		return;
	}
	const CCallsign dst(p->GetCDstAddress());
	auto streamID = p->GetStreamId();
	doRecord(dst.GetModule(), streamID);
}

void CGateway::doRecord(char c, uint16_t streamID)
{
	// record payload in queue
	uint16_t fn = 0;
	CSteadyTimer timer;
	while (true) // record the payloads in fifo
	{
		auto p = Modem2Gate.PopWaitFor(40);
		if (nullptr == p)
			continue;
		if (timer.time() > 2.0)
		{
			printMsg(TC_MAGENTA, TC_RED, "Voice Recorder timeout!\n");
			break;
		}
		if (EPacketType::stream != p->GetType())
			continue;
		if (streamID != p->GetStreamId())
			continue;
		timer.start();
		if (++fn < 3000) // only collect up to 2 minutes
		{
			fifo.emplace(p->GetPayload());
		}
		if (p->IsLastPacket())
			break;
	}
	if (fn > 3000)
	{
		printMsg(TC_MAGENTA, TC_YELLOW, "Too long, did not save the last %.2f seconds of the transmission\n", 0.04f * (fn - 3000));
	}
	if (fn < 25)
	{
		while (not fifo.empty())
			fifo.pop();
		printMsg(TC_MAGENTA, TC_RED, "Only recorded %d milliseconds, not saved\n", 40 * fn);
		return;
	}

	// now save the data

	// make the file pathname
	auto pos = m17alphabet.find(c);
	if (std::string::npos == pos)
	{
		if (isprint(c))
			printMsg(TC_MAGENTA, TC_RED, "'%c' is not a valid M17 character\n", c);
		else
			printMsg(TC_MAGENTA, TC_RED, "0x%02x is not a valid M17 character\n", unsigned(c));
		printMsg(TC_MAGENTA, TC_DEFAULT, "Invalid character will be replaced with ' '\n");
		pos = 0;
	}
	std::filesystem::path pathname(audioPath);
	pathname /= fnames[pos];

	// open the file and write the data
	std::ofstream ofs(pathname, std::ios::binary | std::ios::trunc);
	if (ofs.is_open())
	{
		while (not fifo.empty())
		{
			ofs.write((char *)fifo.front().Data(), 16);
			fifo.pop();
		}
		ofs.close();
	}
	else
	{
		printMsg(TC_MAGENTA, TC_RED, "Could not open %s for writing\n", pathname.c_str());
	}

	// after a short wait
	std::this_thread::sleep_for(std::chrono::milliseconds(500));
	if (g_GateState.SetStateToOnlyIfFrom(EGateState::gatestreamin, EGateState::modemin))
		doPlay(c);
	else
		printMsg(TC_MAGENTA, TC_RED, "Could not set state from ModemIn to GateStreamIn\n");
}

void CGateway::doPlay(std::unique_ptr<CPacket> &p)
{
	wait4end(p);
	if (g_GateState.SetStateToOnlyIfFrom(EGateState::gatestreamin, EGateState::modemin)) {
		std::this_thread::sleep_for(std::chrono::milliseconds(300));
		CCallsign dst(p->GetCDstAddress());
		doPlay(dst.GetModule());
	} else {
		printMsg(TC_MAGENTA, TC_RED, "Could not change state from ModemIn to GateStreamIn\n");
	}
}

void CGateway::doPlay(char c)
{
	// make the file pathname
	std::filesystem::path pathname(audioPath);
	auto pos = m17alphabet.find(c);
	if (std::string::npos == pos)
	{
		if (isprint(c))
			printMsg(TC_MAGENTA, TC_RED, "'%c' is not a valid M17 character\n", c);
		else
			printMsg(TC_MAGENTA, TC_RED, "0x%02x is not a valid M17 character\n", unsigned(c));
		return;
	}
	pathname /= fnames[pos];

	uint16_t fc = 0;

	// make sure the file exists and its size looks okay
	if (std::filesystem::exists(pathname))
	{
		auto size = std::filesystem::file_size(pathname);
		// no partial payloads or less than 1 sec duration or more than 2 minute
		if ((size % 16) or (size / 16 < 25) or (size / 16 > 3000))
		{
			printMsg(TC_MAGENTA, TC_RED, "'%s' has an unexpected file size of %u\n", pathname.c_str(), size);
			return;
		}
		fc = uint16_t(size / 16) - 1u; // this is the last frame number
	}


	// make the TYPE
	CFrameType ft;
	ft.SetPayloadType(EPayloadType::c2_3200);
	ft.SetEncryptType(EEncryptType::none);
	ft.SetSigned(false);
	ft.SetMetaDataType(EMetaDatType::none);
	ft.SetCan(can);
	// now build a master
	CPacket master;
	master.Initialize(EPacketType::stream);
	master.SetStreamId(g_RNG.Get());
	memset(master.GetDstAddress(), 0xffu, 6); // set destination to Broadcast
	thisCS.CodeOut(master.GetSrcAddress());
	master.SetFrameType(ft.GetFrameType(radioTypeIsV3 ? EVersionType::v3 : EVersionType::legacy));

	std::ifstream ifs(pathname, std::ios::binary);
	auto clock = std::chrono::steady_clock::now(); // start the packet clock
	if (ifs.is_open())
	{
		for (uint16_t fn=0; fn<=fc; fn++)
		{
			auto p = std::make_unique<CPacket>();
			p->Initialize(EPacketType::stream, master.GetCData());
			ifs.read((char *)(p->GetPayload()), 16);
			p->SetFrameNumber((fn==fc) ? (fn | 0x8000u) : fn);
			p->CalcCRC();
			clock = clock + std::chrono::milliseconds(40);
			std::this_thread::sleep_until(clock);
			Gate2Modem.Push(p);
		}
		ifs.close();
	}
	else
		printMsg(TC_MAGENTA, TC_RED, "Could not open file '%s'\n", pathname.c_str());
}
