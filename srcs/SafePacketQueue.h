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

	void Push(T &t)
	{
		std::scoped_lock<std::mutex> lock(m);
		q.push(std::move(t));
		c.notify_one();
	}

	unsigned Size(void)
	{
		std::scoped_lock<std::mutex> lock(m);
		return q.size();
	}

	// wait for some time, or until an element is available.
	T PopWaitFor(int ms)
	{
		std::unique_lock<std::mutex> lock(m);
		T val;
		if (c.wait_for(lock, std::chrono::milliseconds(ms), [this] { return not q.empty(); }))
		{
			val = std::move(q.front());
			q.pop();
		}
		return val;
	}

	bool IsEmpty(void)
	{
		std::scoped_lock<std::mutex> lock(m);
		return q.empty();
	}

private:
	std::queue<T> q;
	mutable std::mutex m;
	std::condition_variable c;
};

using IPFrameFIFO  = CSafePacketQueue<std::unique_ptr<CPacket>>;
