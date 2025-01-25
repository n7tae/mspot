/*

         mspot - an M17-only HotSpot using an MMDVM device
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
#pragma once

#include <mutex>

enum class EGateState { idle, gatein, modemin, messagein };

class CGateState
{
public:
	CGateState() : currentState(EGateState::idle) {}
	~CGateState() {}

	const char *GetState()
	{
		std::lock_guard<std::mutex> lg(mtx);
		switch (currentState)
		{
			case EGateState::gatein:
			return "gatein";
			case EGateState::messagein:
			return "messagein";
			case EGateState::modemin:
			return "modemin";
			default:
			return "idle";
		}
	}

	void Idle()	{ SetState(EGateState::idle); }

	void SetState(EGateState newstate)
	{
		std::lock_guard<std::mutex> lg(mtx);
		currentState = newstate;
	}

	bool SetStateOnlyIfIdle(EGateState newstate)
	{
		std::lock_guard<std::mutex> lg(mtx);
		if (EGateState::idle == currentState)
		{
			currentState = newstate;
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
