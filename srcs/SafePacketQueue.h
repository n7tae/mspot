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

#include <queue>
#include <mutex>
#include <condition_variable>
#include <memory>

#include "Packet.h"

template <class T>
class CSafePacketQueue
{
public:
 	CSafePacketQueue(void) : q() , m() , c() {}

	~CSafePacketQueue(void) {}

	void Push(T t)
	{
		std::lock_guard<std::mutex> lock(m);
		q.push(std::move(t));
		c.notify_one();
	}

	T Pop(void)
	{
		std::lock_guard<std::mutex> lock(m);
		if (q.empty())
			return nullptr;
		else
		{
			T val = std::move(q.front());
			q.pop();
			return val;
		}
	}

	// If the queue is empty, wait until an element is available.
	T PopWait(void)
	{
		std::unique_lock<std::mutex> lock(m);
		while(q.empty())
		{
			// release lock as long as the wait and reacquire it afterwards.
			c.wait(lock);
		}
		T val = std::move(q.front());
		q.pop();
		return val;
	}

	bool IsEmpty(void)
	{
		std::unique_lock<std::mutex> lock(m);
		return q.empty();
	}

private:
	std::queue<T> q;
	mutable std::mutex m;
	std::condition_variable c;
};

using IPFrameFIFO = CSafePacketQueue<std::unique_ptr<SIPFrame>>;
