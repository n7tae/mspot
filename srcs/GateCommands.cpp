/****************************************************************
 *                                                              *
 *             More - An M17-only Repeater/HotSpot              *
 *                                                              *
 *         Copyright (c) 2024 by Thomas A. Early N7TAE          *
 *                                                              *
 * See the LICENSE file for details about the software license. *
 *                                                              *
 ****************************************************************/

#include <string>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <thread>
#include <chrono>

#include "SafePacketQueue.h"
#include "SteadyTimer.h"
#include "M17Gateway.h"
#include "Configure.h"
#include "CRC.h"
#include "Log.h"

extern CCRC g_Crc;
extern CConfigure g_Cfg;
extern IPFrameFIFO Host2Gate;
extern IPFrameFIFO Gate2Host;

static const std::string m17alphabet(" ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-/.");

static const std::string fnames[39]
{
	"ECHO",  "alpha",  "bravo"     "charlie", "delta", "echo",    "foxtrot",  "golf",
	"hotel", "india",  "juliette", "kilo",    "lima",  "mike",    "november", "oscar",
	"papa",  "quebec", "romeo",    "sierra",  "tango", "uniform", "victor",   "whiskey",
	"x-ray", "yankee", "zulu",     "zero",    "one",   "two",     "three",    "four",
	"five",  "six",    "seven",    "eight",   "nine",  "dash",    "slash",    "dot"
};

uint16_t CM17Gateway::makeStreamID()
{
		std::uniform_int_distribution<uint16_t> dist(0x0001, 0xFFFE);
		return dist(m_random);
}

void CM17Gateway::wait4end(std::unique_ptr<SIPFrame> &Frame)
{
	// check the starting packet first
	if (EOTFNMask & Frame->GetFrameNumber())
		return;

	CSteadyTimer ptime;

	// we're only going to reset the packet timer if subsequent incoming packets have this SID
	// this is all internal, so we don't need to convert the SID from network byte order.
	auto streamid = Frame->data.streamid;

	while (keep_running)
	{
		Frame = Host2Gate.PopWaitFor(100);
		if (Frame and (Frame->data.streamid == streamid))
		{
			ptime.start();
			if (0x8000 & Frame->GetFrameNumber())
				break;
		}
		if (ptime.time() > 0.5)
		{
			break;
		}
	}
	return;
}

void CM17Gateway::doUnlink(std::unique_ptr<SIPFrame> &Frame)
{
	wait4end(Frame);
	if (ELinkState::unlinked == mlink.state)
	{
		addMessage("already_unlinked");
		LogInfo("%s is already unlinked", thisCS.c_str());
	}
	else
	{
		// make and send the DISConnect packet
		SM17RefPacket disc;
		memcpy(disc.magic, "DISC", 4);
		thisCS.CodeOut(disc.cscode);
		sendPacket(disc.magic, 10, mlink.addr);
		// the gateway proccess loop will disconnect when is receives the confirming DISC packet.
	}
}

void CM17Gateway::doEcho(std::unique_ptr<SIPFrame> &Frame)
{
	if (Frame->GetFrameNumber() & 0x8000)
	{
		return;
	}
	auto streamID = Frame->data.streamid;

	doRecord(' ', streamID);
}

void CM17Gateway::doRecord(std::unique_ptr<SIPFrame> &Frame)
{
	if (Frame->GetFrameNumber() & EOTFNMask)
	{
		return;
	}
	CCallsign dest(Frame->data.lich.addr_dst);
	auto streamID = Frame->data.streamid;
	doRecord(dest.GetModule(), streamID);
}

void CM17Gateway::doRecord(char c, uint16_t streamID)
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
		if (streamID != frame->data.streamid)
			continue;
		if (timer.time() > 2.0)
			break;
		timer.start();
		if (++fn < 3000) // only collect up to 2 minutes
		{
			auto newpl = std::make_unique<payload>();
			memcpy(newpl->data(), frame->data.payload, 16);
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

void CM17Gateway::doPlay(std::unique_ptr<SIPFrame> &Frame)
{
	wait4end(Frame);
	std::this_thread::sleep_for(std::chrono::milliseconds(300));
	CCallsign dest(Frame->data.lich.addr_dst);
	doPlay(dest.GetModule());
}

void CM17Gateway::doPlay(char c)
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
	SIPFrame master;
	memcpy(master.data.magic, "M17 ", 4);
	master.SetStreamID(makeStreamID());
	memset(master.data.lich.addr_dst, 0xffu, 6); // set destination to Broadcast
	thisCS.CodeOut(master.data.lich.addr_src);
	master.SetFrameType(0x0005 | (can << 7));
	memset(master.data.lich.meta, 0, 14);

	uint16_t fn = 0;
	std::ifstream ifs(pathname, std::ios::binary);
	if (ifs.is_open())
	{
		while (fn < count)
		{
			auto frame = std::make_unique<SIPFrame>();
			memcpy(frame->data.magic, master.data.magic, IPFRAMESIZE);
			ifs.read((char *)(frame->data.payload), 16);
			frame->SetFrameNumber((fn < count) ? fn++ : fn++ & EOTFNMask);
			g_Crc.setCRC(frame->data.magic, IPFRAMESIZE);
			Gate2Host.Push(frame);
		}
		ifs.close();
	}
	else
		LogError("Could not open file '%s'", pathname.c_str());
}
