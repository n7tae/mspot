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

#include <mutex>

enum class EGateState { idle, gatestreamin, gatepacketin, messagein, modemin };

class CGateState
{
public:
	CGateState() : currentState(EGateState::idle) {}
	~CGateState() {}

	const char *GetStateName()
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
			default:
			return "idle";
		}
	}

	EGateState GetState()
	{
		std::lock_guard<std::mutex> lg(mtx);
		return currentState;
	}

	void Set2IdleIfGateIn(void)
	{
		std::lock_guard<std::mutex> lg(mtx);
		if (EGateState::gatepacketin==currentState or EGateState::gatestreamin==currentState)
			currentState = EGateState::idle;
	}

	void Set2IdleIf(EGateState state)
	{
		std::lock_guard<std::mutex> lg(mtx);
		if (currentState == state)
			currentState = EGateState::idle;
	}

	void Idle()
	{
		std::lock_guard<std::mutex> lg(mtx);
		currentState = EGateState::idle;
	}

	// return true if sucessful
	bool SetStateToOnlyIfFrom(EGateState tostate, EGateState fromstate)
	{
		std::lock_guard<std::mutex> lg(mtx);
		if (fromstate == currentState)
		{
			currentState = tostate;
			return true;
		}
		return false;
	}

	// returns true if successful
	bool TryState(EGateState newstate)
	{
		std::lock_guard<std::mutex> lg(mtx);
		if (newstate == currentState)
			return true;
		if (EGateState::idle == currentState)
		{
			currentState = newstate;
			return true;
		}
		return false;
	}

private:
	std::mutex mtx;
	EGateState currentState;
};
