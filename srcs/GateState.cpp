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

#define DEBUG

#ifdef DEBUG
#include <stdio.h>
#endif

#include "GateState.h"

// the one and only Tx/Rx state
CGateState g_GateState;

const char *CGateState::GetStateName()
{
	std::lock_guard<std::mutex> lg(mtx);
	switch (currentState)
	{
		case EGateState::gatestreamin:
			return "gatestreamin";
		case EGateState::gatepacketin:
			return "gatepacketin";
		case EGateState::messagein:
			return "messagein";
		case EGateState::modemin:
			return "modemin";
		case EGateState::bootup:
			return "bootup";
		default:
			return "idle";
	}
}

EGateState CGateState::GetState()
{
	std::lock_guard<std::mutex> lg(mtx);
	return currentState;
}

void CGateState::Set2IdleIfGateIn(void)
{
	std::lock_guard<std::mutex> lg(mtx);
	if (EGateState::messagein==currentState or EGateState::gatestreamin==currentState or EGateState::gatepacketin==currentState)
	{
		currentState = EGateState::idle;
		#ifdef DEBUG
		printf("GateState: Idle\n");
		#endif
	}
}

void CGateState::Idle()
{
	#ifdef DEBUG
	if (EGateState::idle != currentState)
		printf("GateState: Idle\n");
	#endif
	std::lock_guard<std::mutex> lg(mtx);
	currentState = EGateState::idle;
}

bool CGateState::HandleRfCommand(EGateState toState)
{
	std::lock_guard<std::mutex> lg(mtx);
	if (EGateState::modemin==currentState or EGateState::rftimeout==currentState)
	{
		currentState = toState;
		#ifdef DEBUG
		printf("GateState: %s", GetStateName());
		#endif
		return true;
	}
	return false;
}

// return true if sucessful
bool CGateState::SetStateToOnlyIfFrom(EGateState tostate, EGateState fromstate)
{
	std::lock_guard<std::mutex> lg(mtx);
	if (fromstate == currentState)
	{
		currentState = tostate;
		#ifdef DEBUG
		printf("GateState: %s", GetStateName());
		#endif
		return true;
	}
	return false;
}

bool CGateState::IsRxReady(void)
{
	std::lock_guard<std::mutex> lg(mtx);
	switch (currentState)
	{
		case EGateState::modemin:
		case EGateState::idle:
			return true;
		default:
			return false;
	}
}

bool CGateState::IsTxReady(void)
{
	std::lock_guard<std::mutex> lg(mtx);
	switch (currentState)
	{
		case EGateState::modemin:
			return false;
		default:
			return true;
	}
}

// returns true if successful
bool CGateState::TryState(EGateState newstate)
{
	std::lock_guard<std::mutex> lg(mtx);
	if (newstate == currentState)
		return true;
	if (EGateState::idle == currentState)
	{
		currentState = newstate;
		#ifdef DEBUG
		printf("GateState: %s", GetStateName());
		#endif
		return true;
	}
	return false;
}
