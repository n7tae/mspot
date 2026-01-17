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

#include <cstdint>
#include <string>
#include <termios.h>
#include <unistd.h>
#include <gpiod.h>

#include "UnixDgramSocket.h"
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
	void Run();

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
	bool pingDev(void);
	bool setRxFreq(uint32_t freq);
	bool setTxFreq(uint32_t freq);
	bool setFreqCorr(int16_t corr);
	bool setAfc(bool afc);
	bool setTxPower(float pow);
	bool txrxControl(uint8_t cid, uint8_t onoff, const char *what);
	bool startTx(void);
	bool startRx(void);
	bool stopTx(void);
	bool stopRx(void);
	float sed(const float *v1, const int8_t *v2, const unsigned len) const;
	void filterSymbols(int8_t* __restrict out, const int8_t* __restrict in, const float* __restrict flt, uint8_t phase_inv);

	CUnixDgramReader g2m;
	CUnixDgramWriter m2g;
	int fd = -1; // the handle to the CC1200
	int ud = -1; // the handle to the unix read socket

	SConfig cfg;

	// gpiod pointers
	struct gpiod_chip *gpio_chip = nullptr;
	struct gpiod_line_request *boot0_line = nullptr;
	struct gpiod_line_request *nrst_line = nullptr;

	bool uart_lock = false;

	/*######################################## items for rxProcess ###########################################*/
	//UART comms
	bool uart_rx_data_valid = false;
	bool got_lsf = false;
	int8_t rx_bsb_sample = 0;
	int8_t raw_bsb_rx[960];
	uint8_t lsf_b[30];
	bool first_frame = true;
	uint16_t fn;
	uint16_t last_fn = 0xffffu;
	uint16_t sid;
	uint16_t sample_cnt = 0;
	RingBuffer<uint8_t, 3> rx_header;
	RingBuffer<int8_t, 41> flt_buff;
	RingBuffer<float, 2042> f_flt_buff;
	// why 2042? 8*5+2*(8*5+4800/25*5)+2 = 2042
	// 8 preamble symbols, 8 for the syncword, and 960 for the payload.
	// floor(sps/2)=2 extra samples for timing error correction
	const int8_t lsf_sync_ext[16] { +3, -3, +3, -3, +3, -3, +3, -3, +3, +3, +3, +3, -3, -3, +3, -3 };
	const int8_t eot_symbols[8]   { +3, +3, +3, +3, +3, +3, -3, +3 };
	const float escale = 4.14647334e-6f; // 100%/0xffff/SYM_PER_PLD/2
	SLSF rxlsf;
	CFrameType rxType;
	ERxState rx_state = ERxState::idle;
	// for stream mode
	uint8_t lich_parts = 0;
	// for packet mode
	uint8_t pkt_pld[825];
	uint8_t *ppkt = pkt_pld;
	uint16_t plsize = 0;
	fd_set rfds;
	
	/*######################################## items for txProcess ###########################################*/
	uint32_t tx_timer = 0;
	ETxState tx_state = ETxState::idle;
	SLSF txlsf;
	CFrameType txType;
	uint16_t frame_count;
};
