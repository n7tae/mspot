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

#include <future>
#include <mutex>
#include <cstdint>
#include <string>
#include <termios.h>
#include <unistd.h>
#include <gpiod.h>

#include "RingBuffer.h"
#include "FrameType.h"
#include "Callsign.h"
#include "Base.h"
#include "LSF.h"

enum class ERxState { idle, str, pkt };

enum class ETxState { idle, active };

using SConfig = struct config_tag
{
	std::string gpioDev, uartDev;
	CCallsign callSign;
	unsigned baudRate, rxFreq, txFreq;
	unsigned boot0, nrst;
	unsigned can;
	int freqCorr;
	float power;
	bool afc, isV3, debug;
};

class CCC1200 : public CBase
{
public:
	bool Start();
	void Stop();

private:
	void rxProcess(void);
	void txProcess(void);
	bool loadConfig(void);
	uint32_t getMS(void);
	speed_t getBaud(unsigned baud);
	bool setAttributes(unsigned speed, int parity);
	struct gpiod_line_request *gpioLineRequest(unsigned offset, int value, const char *consumer);
	bool gpioSetValue(unsigned offset, int value);
	bool gpioInit(const std::string &consumer);
	void gpioCleanup(void);
	bool readDev(void *buf, int size);
	void writeDev(void *buf, int size, const char *where);
	bool getFwVersion();
	bool pingDev(void);
	bool setRxFreq(uint32_t freq);
	bool setTxFreq(uint32_t freq);
	bool setFreqCorr(int16_t corr);
	bool setAfc(bool afc);
	bool setTxPower(float pow);
	void txrxControl(uint8_t cid, uint8_t onoff, const char *what);
	void start_tx(void);
	void start_rx(void);
	void stop_tx(void);
	void stop_rx(void);
	float sed(const float *v1, const int8_t *v2, const unsigned len) const;
	void filterSymbols(int8_t* __restrict out, const int8_t* __restrict in, const float* __restrict flt, uint8_t phase_inv);

	int fd = -1; // the handle to the CC1200
	int ud = -1; // the handle to the unix read socket

	SConfig cfg;

	// gpiod pointers
	struct gpiod_chip *gpio_chip = nullptr;
	struct gpiod_line_request *boot0_line = nullptr;
	struct gpiod_line_request *nrst_line = nullptr;

	std::mutex mux;

	std::atomic<bool> keep_running;
	std::future<void> txFuture, rxFuture;
};
