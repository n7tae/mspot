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

#include <random>
#include <chrono>
#include <cstdint>

class CRandom
{
public:
	uint16_t Get()
	{
		return distribution(engine);
	}
private:
	std::random_device rd;
	std::minstd_rand engine { rd() };
	std::uniform_int_distribution<uint16_t> distribution { 0x1u, 0xffffu };
};
