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

#pragma once

#include <atomic>

#include "SockAddress.h"
#include "SteadyTimer.h"
#include "Packet.h"

enum class ELinkState    { unlinked, linking, linked };
enum class EInternetType { ipv4only, ipv6only, both };
enum class EDataType     { str_only, pkt_only, both };
enum class ETypeVersion  { deprecated, v3, both };

class CTarget : public CBase
{
public:
	// initialization
	void TargetInit(const CCallsign &cs, ERefType eRef, EDataType dType, ETypeVersion tVersion, const std::string m, const std::string ms, CSockAddress &sa, const CCallsign &mspot);
	// get various target data
	const CCallsign &GetCS(void) const { return cs; }
	ETypeVersion GetTypeVersion(void) const { return eTypeVersion; }
	EDataType GetDataType(void) const { return eDataType; }
	ELinkState GetState(void) const { return state; }
	ERefType GetType(void) const { return eRefType; }
	const CSockAddress &GetAddress(void) const { return addr; }
	const SM17RefPacket *GetPongPacket(void);
	// some states for the target
	bool HasAddress(void) const { return not addr.AddressIsZero(); }
	bool TimedOut(void) const { return receivePingTimer.time() > 30.0; }

	// change state
	void Linked(void);
	void Linking(void);
	void Unlinked(void);

private:
	SM17RefPacket pongPacket;
	CSockAddress addr;
	CCallsign cs;

	std::string mods, smods;
	std::atomic<ELinkState> state;
	CSteadyTimer receivePingTimer;
	ERefType eRefType;
	EDataType eDataType;
	ETypeVersion eTypeVersion;
};

