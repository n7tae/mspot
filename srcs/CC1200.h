/*

         mspot - an M17-only HotSpot using an RPi CC1200 hat
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

#include <future>

#include <termios.h>
#include <cstdint>
#include <atomic>
#include <string>
#include <memory>
#include <gpiod.h>
#include <arpa/inet.h>

#include <m17.h>

#include "Callsign.h"
#include "Gateway.h"
#include "Packet.h"

enum class ERxState
{
	idle,
	sync
};

enum class ETxState
{
	idle,
	active
};

using SConfig = struct config_tag
{
	std::string gpioDev, uartDev;
	CCallsign callSign;
	unsigned baudRate, rxFreq, txFreq;
	unsigned pa_en, boot0, nrst;
	unsigned can;
	int freqCorr;
	float power;
	bool afc, isprivate, debug;
};

class CCC1200
{
public:
	bool Start();
	void Stop();

private:
	void run();
	speed_t getBaud(uint32_t baud);
	uint32_t getMilliseconds(void);
	bool setIinterface(uint32_t speed, int parity);
	void loadConfig(void);
	bool sendPING(void);
	ssize_t getResponse(void);
	bool dev_set_freq(unsigned r_or_t, uint32_t freq);
	bool dev_set_freq_corr(int16_t corr);
	bool dev_set_afc(bool en);
	bool dev_set_tx_power(float power);
	bool dev_start_tx(void);
	bool dev_start_rx(void);
	bool dev_stop_rx(void);
	void writeBSB(int8_t *bsb_samples);
	void filter_symbols(int8_t out[SYM_PER_FRA*5], const int8_t in[SYM_PER_FRA], const float* flt, uint8_t phase_inv);
	struct gpiod_line_request *gpioLineRequest(unsigned int offset, int value, const std::string &consumer);
	bool gpioSetValue(unsigned offset, int value);

	// Class data
	SConfig cfg;
	std::future<void> runFuture;

	// data
	std::string progName;
	std::atomic<bool> keep_running;
	struct gpiod_chip *gpioChip;
	gpiod_line_request *reqBoot0, *reqNrst;
	int fd; // for the modem
	std::unique_ptr<CPacket> lsfPack;
};
