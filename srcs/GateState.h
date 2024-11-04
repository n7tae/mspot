/****************************************************************
 *                                                              *
 *             More - An M17-only Repeater/HotSpot              *
 *                                                              *
 *         Copyright (c) 2024 by Thomas A. Early N7TAE          *
 *                                                              *
 * See the LICENSE file for details about the software license. *
 *                                                              *
 ****************************************************************/
#pragma once

#include <mutex>

enum class EGateState { idle, gatein, modemin, record, play, linking };

class CGateState
{
public:
	CGateState() : currentState(EGateState::idle) {}
	~CGateState() {}

	EGateState GetState()
	{
		std::lock_guard<std::mutex> lg(mtx);
		return currentState;
	}

	void Idle()	{ SetState(EGateState::idle); }

	void SetState(EGateState newstate)
	{
		std::lock_guard<std::mutex> lg(mtx);
		currentState = newstate;
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
