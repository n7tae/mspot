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

#include <string>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <thread>
#include <chrono>

#include "SafePacketQueue.h"
#include "SteadyTimer.h"
#include "FrameType.h"
#include "Configure.h"
#include "Gateway.h"
#include "Random.h"
#include "CRC.h"
#include "Log.h"

extern CCRC g_Crc;
extern CRandom g_RNG;
extern CConfigure g_Cfg;
extern IPFrameFIFO Modem2Gate;
extern IPFrameFIFO Gate2Modem;

static const std::string m17alphabet(" ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-/.");

static const std::string fnames[39]
{
	"ECHO",  "alpha",  "bravo"     "charlie", "delta", "echo",    "foxtrot",  "golf",
	"hotel", "india",  "juliette", "kilo",    "lima",  "mike",    "november", "oscar",
	"papa",  "quebec", "romeo",    "sierra",  "tango", "uniform", "victor",   "whiskey",
	"x-ray", "yankee", "zulu",     "zero",    "one",   "two",     "three",    "four",
	"five",  "six",    "seven",    "eight",   "nine",  "dash",    "slash",    "dot"
};

void CGateway::wait4end(std::unique_ptr<CPacket> &pack)
{
	// check the starting packet first
	if (pack->IsLastPacket())
		return;

	CSteadyTimer ptime;

	// we're only going to reset the packet timer if subsequent incoming packets have this SID
	auto streamid = pack->GetStreamId();

	while (keep_running)
	{
		pack = Modem2Gate.PopWaitFor(100);
		if (pack and (pack->GetStreamId() == streamid))
		{
			ptime.start();
			if (pack->IsLastPacket())
				break;
		}
		if (ptime.time() > 0.5)
		{
			break;
		}
	}
	return;
}

void CGateway::doStatus(std::unique_ptr<CPacket> &pack)
{
	wait4end(pack);
	if (ELinkState::linked == mlink.state)
		addMessage("repeater is_linked_to destination");
	else if (ELinkState::linking == mlink.state)
		addMessage("repeater is_linking");
	else
		addMessage("repeater is_unlinked");
}

void CGateway::doUnlink(std::unique_ptr<CPacket> &pack)
{
	wait4end(pack);
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

void CGateway::doEcho(std::unique_ptr<CPacket> &pack)
{
	if (pack->IsLastPacket())
	{
		return;
	}
	auto streamID = pack->GetStreamId();

	doRecord(' ', streamID);
}

void CGateway::doRecord(std::unique_ptr<CPacket> &pack)
{
	if (pack->IsLastPacket())
	{
		return;
	}
	CCallsign dst(pack->GetCDstAddress());
	auto streamID = pack->GetStreamId();
	doRecord(dst.GetModule(), streamID);
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
		auto pack = Modem2Gate.PopWaitFor(100);
		if (nullptr == pack)
			continue;
		if (streamID != pack->GetStreamId())
			continue;
		if (timer.time() > 2.0)
			break;
		timer.start();
		if (++fn < 3000) // only collect up to 2 minutes
		{
			auto newpl = std::make_unique<payload>();
			memcpy(newpl->data(), pack->GetCPayload(), 16);
			fifo.Push(newpl);
		}
		if (pack->IsLastPacket())
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

void CGateway::doPlay(std::unique_ptr<CPacket> &pack)
{
	wait4end(pack);
	std::this_thread::sleep_for(std::chrono::milliseconds(300));
	CCallsign dst(pack->GetCDstAddress());
	doPlay(dst.GetModule());
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

	uint16_t fn = 0;
	std::ifstream ifs(pathname, std::ios::binary);
	auto clock = std::chrono::steady_clock::now(); // start the packet clock
	if (ifs.is_open())
	{
		while (fn < count)
		{
			auto pack = std::make_unique<CPacket>();
			pack->Initialize(EPacketType::stream, master.GetCData(), 54);
			ifs.read((char *)(pack->GetPayload()), 16);
			pack->SetFrameNumber((fn < count) ? fn++ : fn++ & 0x8000u);
			pack->CalcCRC();
			clock = clock + std::chrono::milliseconds(40);
			std::this_thread::sleep_until(clock);
			Gate2Modem.Push(pack);
		}
		ifs.close();
	}
	else
		LogError("Could not open file '%s'", pathname.c_str());
}
