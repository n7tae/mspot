/****************************************************************
 *                                                              *
 *             More - An M17-only Repeater/HotSpot              *
 *                                                              *
 *         Copyright (c) 2024 by Thomas A. Early N7TAE          *
 *                                                              *
 * See the LICENSE file for details about the software license. *
 *                                                              *
 ****************************************************************/


#include <iostream>

#include "Callsign.h"
#include "Log.h"

#define M17CHARACTERS " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-/."

CCallsign::CCallsign()
{
	memset(cs, 0u, sizeof(cs));
	memset(code, 0u, sizeof(code));
}

CCallsign::CCallsign(const std::string &callsign)
{
	CSIn(callsign);
}

CCallsign::CCallsign(const uint8_t *in)
{
	CodeIn(in);
}

void CCallsign::CSIn(const std::string &callsign)
{
	memset(cs, 0u, sizeof(cs));

	if (0 == callsign.compare("#ALL"))
	{
		memcpy(cs, callsign.c_str(), 4);
		for (unsigned i=0u; i<6u; i++)
			code[i] = 0xffu;
		return;
	}

	const std::string m17_alphabet(M17CHARACTERS);

	memset(code, 0u, sizeof(code));

	int i = int(callsign.length()) - 1;	// start at the end

	while (i > -1)
	{
		// skip space or bad char
		auto pos = m17_alphabet.find(cs[i]);
		if (pos == std::string::npos)
			pos = 0;
		if (pos)
			break;
		i--;
	}

	uint64_t encoded = 0;
	while (i > -1) // start calculating the encoded value
	{
		auto pos = m17_alphabet.find(cs[i]);
		if (pos == std::string::npos)
			pos = 0;
		cs[i] = m17_alphabet[pos];	// set the cs character
		encoded = 40u * encoded + pos;
		i--;
	}

	i = 5;	// start at the end
	while (encoded) // set the 6-byte coded value
	{
		code[i--] = encoded % 40u;
		encoded /= 40u;
	}
}

void CCallsign::CodeIn(const uint8_t *in)
{
	const std::string m17_alphabet(M17CHARACTERS);
	memset(cs, 0, 10);

	uint64_t coded = in[0];
	for (int i=1; i<6; i++)
		coded = (coded << 8) | in[i];

	if (coded == 0xffffffffffffu) {
		strcpy(cs, "#ALL");
		for(int i=0; i<6; i++)
			code[i] = 0xffu;
		return;
	}

	if (coded > 0xee6b27ffffffu) {
		std::cerr << "Callsign code is too large, 0x" << std::hex << coded << std::endl;
		return;
	}
	memcpy(code, in, 6);
	int i = 0;
	while (coded) {
		cs[i++] = m17_alphabet[coded % 40];
		coded /= 40;
	}
}

const std::string CCallsign::GetCS(unsigned len) const
{
	std::string rval(cs);
	if (len)
		rval.resize(len, ' ');
	return rval;
}

char CCallsign::GetModule() const
{
	if (cs[8])
		return cs[8];
	else
		return ' ';
}

bool CCallsign::operator==(const CCallsign &rhs) const
{
	return (0 == memcmp(code, rhs.code, 6));
}
