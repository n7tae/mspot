/****************************************************************
 *                                                              *
 *             More - An M17-only Repeater/HotSpot              *
 *                                                              *
 *         Copyright (c) 2024 by Thomas A. Early N7TAE          *
 *                                                              *
 * See the LICENSE file for details about the software license. *
 *                                                              *
 ****************************************************************/

#include <ctype.h>
#include "Callsign.h"
#include "Log.h"

const std::string m17_alphabet(" ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-/.");

CCallsign::CCallsign()
{
	Clear();
}

CCallsign::CCallsign(const std::string &callsign)
{
	CSIn(callsign);
}

CCallsign::CCallsign(const uint8_t *in)
{
	CodeIn(in);
}

CCallsign &CCallsign::operator=(const CCallsign &rhs)
{
	coded = rhs.coded;
	memcpy(cs, rhs.cs, sizeof(cs));
	return *this;
}

void CCallsign::Clear()
{
	memset(cs, 0, 10);
	coded = 0;
}

void CCallsign::CSIn(const std::string &callsign)
{
	Clear();
	if(0 == callsign.find("#ALL"))
	{
		strcpy(cs, "#ALL");
		coded = 0xffffffffffffu;
		return;
	}

	strncpy(cs, callsign.c_str(), 9); // shorter callsigns will have trailing NULLs, but there might be trailing spaces
	coded = 0; //initialize the encoded value

	// 'skip' will be used to delay encoding until a char with pos > 0 is found;
	bool skip = true;
	for(int i=8; i>=0; i--) // processing the cs  backwards, from the end to the beginning
	{
		while (0 == cs[i])
			i--; // skip traling nulls
		auto pos = m17_alphabet.find(cs[i]);
		if (std::string::npos == pos)
			pos = 0; // the char wasn't found in the M17 char set, so it becomes a space
		if (skip)
		{
			if ( 0 == pos) // unless it's at the end
			{
				cs[i] = 0;	// this ensures trailing spaces will actually be NULL
				continue;
			}
			else
			{
				skip = false; // okay, here is our first char that's not a space
			}
		}
		cs[i] = m17_alphabet.at(pos);	// replace with valid character
		coded *= 40u;
		coded += pos;
	}
}

const std::string CCallsign::GetCS(unsigned len) const
{
	std::string rval(cs);
	if (len > 9)
		len = 9;
	if (len)
		rval.resize(len, ' ');
	return rval;
}

void CCallsign::CodeIn(const uint8_t *in)
{
	Clear();
	// input array of unsigned chars are in network byte order
	for (int i=0; i<6; i++)
	{
		coded *= 0x100u;
		coded += in[i];
	}

	if (coded == 0xffffffffffffu)
	{
		strcpy(cs, "#ALL");
		return;
	}

	if (coded > 0xee6b27ffffffu)
	{
		LogWarning("Callsign code is too large, 0x%x", coded);
		coded = 0;
		return;
	}

	auto c = coded;
	int i = 0;
	while (c)
	{
		cs[i++] = m17_alphabet[c % 40u];
		c /= 40u;
	}
}

void CCallsign::CodeOut(uint8_t *out) const
{
	memset(out, 0, 6);
	auto c = coded;
	auto pout = out+5;
	while (c)
	{
		*pout-- = c % 0x100u;
		c /= 0x100u;
	}
}

char CCallsign::GetModule() const
{
	if (cs[8])
		return cs[8];
	else
		return ' ';
}

uint64_t CCallsign::GetBase() const
{
	if (cs[8])
	{
		auto pos = m17_alphabet.find(cs[8]);
		return coded - (pos * 0x5f5e1000000u); // 0x5f5e1000000 = 40^8
	}
	return coded;
}

bool CCallsign::operator==(const CCallsign &rhs) const
{
	return coded == rhs.coded;
}

bool CCallsign::operator!=(const CCallsign &rhs) const
{
	return coded != rhs.coded;
}

void CCallsign::SetModule(char m)
{
	if (islower(m))
		m = toupper(m);
	if (not isupper(m))
	{
		if (isprint(m))
			LogWarning("'%c' is not a vaild module", m);
		else
			LogWarning("0x%02x is not a valid module character", unsigned(m));
		return;
	}
	std::string call(cs);
	call.resize(8, ' ');
	call.append(1, m);
	CSIn(call);
}
