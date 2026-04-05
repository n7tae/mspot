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
 
#include "Target.h"
#include "MspotDB.h"

extern CMspotDB g_DataBase;

void CTarget::TargetInit(const CCallsign &callsign, ERefType eRef, EDataType type, ETypeVersion version, const std::string m, const std::string ms, CSockAddress &sa, const CCallsign &mspot)
{
	state = ELinkState::unlinked;
	cs = callsign;
	eRefType = eRef;
	eDataType = type;
	eTypeVersion = version;
	mods.assign(m);
	smods.assign(ms);
	addr = sa;
	if (ERefType::none != eRef)
	{
		memcpy(pongPacket.magic, "PONG", 4);
		mspot.CodeOut(pongPacket.cscode);
	}
}

void CTarget::ChangeState(ELinkState newstate)
{
	state = newstate;
	switch (state)
	{
		case ELinkState::unlinked:
			addr.Clear();
			cs.Clear();
			g_DataBase.ClearTable("linkstatus");
			break;
		case ELinkState::linking:
			break;
		case ELinkState::linked:
			receivePingTimer.start();
			Log(EUnit::target, "Connected to %s at %s\n", cs.c_str(), addr.GetAddress());
			g_DataBase.UpdateLS(addr.GetAddress(), addr.GetPort(), cs.c_str());
			break;
	}
}

const SM17RefPacket *CTarget::GetPongPacket(void)
{
	receivePingTimer.start();
	return &pongPacket;
}
