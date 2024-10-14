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

#include <ctime>
#include <chrono>

class CSteadyTimer
{
public:
	CSteadyTimer()
	{
		start();
	}

	~CSteadyTimer() {}

	void start()
	{
		starttime = std::chrono::steady_clock::now();
	}

	double time()
	{
		std::chrono::duration<double> elapsed(std::chrono::steady_clock::now() - starttime);
		return elapsed.count();
	}

private:
	std::chrono::steady_clock::time_point starttime;
};
