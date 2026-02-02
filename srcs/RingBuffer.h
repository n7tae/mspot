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

template <typename T, size_t size>
class RingBuffer
{
public:
	void Push(const T& item)
	{
		buffer[position++] = item;
		position %= capacity;
	}

	// Get an element from the buffer
	T operator[](unsigned index) const
	{
		return buffer[(index + position) % capacity];
	}

	size_t Size(void) const
	{
		return capacity;
	}

	void Clear(void)
	{
		for (size_t i = 0; i<size; i++)
			buffer[i] = 0;
		position = 0;
	}

private:
	T buffer[size] { 0 };
	const size_t capacity { size };
	size_t position = 0; // current position
};
