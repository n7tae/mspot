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

enum class EUnit { null, target, call, cc12, gate, host, sock, udp, db };

class CBase
{
public:
	CBase(void) {}
	virtual ~CBase(void) {}
protected:
	void Log(EUnit unit, const char *fmt, ...) const;
	void Dump(const char *title, const void *data, unsigned length) const;

private:
#ifndef NO_TS
	void timeStamp() const;
#endif
};
