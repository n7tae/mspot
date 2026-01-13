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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cerrno>
#include <atomic>
#include <math.h>
#include <stdarg.h>

#include <netinet/ip_icmp.h>
#include <netinet/udp.h>
#include <netinet/tcp.h>
#include <netinet/ip.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <arpa/inet.h>

#include <fcntl.h> 
#include <sys/ioctl.h>
#include <time.h>
#include <signal.h>
//libm17
#include <m17.h>

#include "SafePacketQueue.h"
#include "RingBuffer.h"
#include "Configure.h"
#include "GateState.h"
#include "Gateway.h"
#include "CC1200.h"
#include "Random.h"
#include "CRC.h"

extern CGateState g_GateState;
extern CConfigure g_Cfg;
extern CGateway   g_Gateway;
extern CRandom    g_RNG;
extern CCRC       g_Crc;

extern IPFrameFIFO Modem2Gate;
extern IPFrameFIFO Gate2Modem;

//CC1200 commands
enum cmd_t
{
	CMD_PING,

	//SET
	CMD_SET_RX_FREQ,
	CMD_SET_TX_FREQ,
	CMD_SET_TX_POWER,
	CMD_SET_RESERVED,
	CMD_SET_FREQ_CORR,
	CMD_SET_AFC,
	CMD_TX_START,
	CMD_RX_START,
	CMD_RX_DATA,
	CMD_TX_DATA,
	CMD_DBG_ENABLE,
	CMD_DBG_TXT,

	//GET
	CMD_GET_IDENT = 0x80,
	CMD_GET_CAPS,
	CMD_GET_RX_FREQ,
	CMD_GET_TX_FREQ,
	CMD_GET_TX_POWER,
	CMD_GET_FREQ_CORR,
	CMD_GET_BSB_BUFF,
	CMD_GET_RSSI
};

#define RX_SYMBOL_SCALING_COEFF	(1.0f/(0.8f/(40.0e3f/2097152*0xAD)*130.0f))
// CC1200 User's Guide, p. 24
// 0xAD is `DEVIATION_M`, 2097152=2^21
// +1.0 is the symbol for +0.8kHz
// 40.0e3 is F_TCXO in kHz
// 129 is `CFM_RX_DATA_OUT` register value at max. F_DEV (130 is 1 off but offers a better symbol map)
// datasheet might have this wrong (it says 64)

#define TX_SYMBOL_SCALING_COEFF	(0.8f/((40.0e3f/2097152)*0xAD)*64.0f)
// 0xAD is `DEVIATION_M`, 2097152=2^21
// +0.8kHz is the deviation for symbol +1
// 40.0e3 is F_TCXO in kHz
// 64 is `CFM_TX_DATA_IN` register value for max. F_DEV

enum class ERxState { idle, str, ptk };

enum class ETxState { idle, active };

enum err_t
{
	ERR_OK,					//all good
	ERR_TRX_PLL,			//TRX PLL lock error
	ERR_TRX_SPI,			//TRX SPI comms error
	ERR_RANGE,				//value out of range
	ERR_CMD_MALFORM,		//malformed command
	ERR_BUSY,				//busy!
	ERR_BUFF_FULL,			//buffer full
	ERR_NOP,				//nothing to do
	ERR_OTHER
};

std::atomic<bool> keep_running = true;
time_t last_refl_ping;

//debug printf

uint32_t CCC1200::getMS(void)
{
	struct timespec spec;

	clock_gettime(CLOCK_REALTIME, &spec);

	time_t s = spec.tv_sec;
	uint32_t ms = roundf(spec.tv_nsec/1.0e6); //convert nanoseconds to milliseconds
	if (ms>999)
	{
		s++;
		ms=0;
	}

	return s*1000 + ms;
}

speed_t CCC1200::getBaud(unsigned baud)
{
	switch(baud)
	{
		default:
			return B0;
		case 9600:
			return B9600;
		case 19200:
			return B19200;
		case 38400:
			return B38400;
		case 57600:
			return B57600;
		case 115200:
			return B115200;
		case 230400:
			return B230400;
		case 460800:
			return B460800;
		case 500000:
			return B500000;
		case 576000:
			return B576000;
		case 921600:
			return B921600;
		case 1000000:
			return B1000000;
		case 1152000:
			return B1152000;
		case 1500000:
			return B1500000;
		case 2000000:
			return B2000000;
		case 2500000:
			return B2500000;
		case 3000000:
			return B3000000;
		case 3500000:
			return B3500000;
		case 4000000:
			return B4000000;
	}
}

bool CCC1200::setAttributes(unsigned speed, int parity)
{
	struct termios tty;
	if (tcgetattr(fd, &tty))
	{
		printMsg(nullptr, TC_RED, "tcgetattr() error: %s\n", strerror(errno));
		return true;
 	}

	auto baud = getBaud(speed);
	if (B0 == baud)
	{
		printMsg(nullptr, TC_YELLOW, "%u is not a valid baud rate, trying 460800 ", speed);
		baud = B460800;
	}
	cfsetospeed(&tty, baud);
	cfsetispeed(&tty, baud);

	tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;	//8-bit chars
	//disable IGNBRK for mismatched speed tests; otherwise receive break
	//as \000 chars
	tty.c_iflag &= ~IGNBRK;			//disable break processing
	tty.c_lflag = 0;				//no signaling chars, no echo,
									//no canonical processing
	tty.c_oflag = 0;				//no remapping, no delays
	tty.c_cc[VMIN]  = 1;			//read returns when 1 byte available
	tty.c_cc[VTIME] = 5;			//5*0.5=0.5 seconds read timeout

	tty.c_iflag &= ~(IXON | IXOFF | IXANY); // shut off xon/xoff ctrl

	tty.c_cflag |= (CLOCAL | CREAD);	//ignore modem controls,
										//enable reading
	tty.c_cflag &= ~(PARENB | PARODD);	//shut off parity
	tty.c_cflag |= parity;
	tty.c_cflag &= ~CSTOPB;
	tty.c_cflag &= ~CRTSCTS;

	if (tcsetattr(fd, TCSANOW, &tty))
	{		
		printMsg(nullptr, TC_RED, " tcsetattr() error: %s\n", strerror(errno));
		return true; 
	}
	
	return false;
}

bool CCC1200::loadConfig()
{
	//load defaults
	cfg.baudRate = g_Cfg.GetUnsigned(g_Keys.modem.section,    g_Keys.modem.uartBaudRate);
	cfg.rxFreq   = g_Cfg.GetUnsigned(g_Keys.modem.section,    g_Keys.modem.rxFreq);
	cfg.txFreq   = g_Cfg.GetUnsigned(g_Keys.modem.section,    g_Keys.modem.txFreq);
	cfg.nrst     = g_Cfg.GetUnsigned(g_Keys.modem.section,    g_Keys.modem.nrst);
	cfg.boot0    = g_Cfg.GetUnsigned(g_Keys.modem.section,    g_Keys.modem.boot0);
	cfg.can      = g_Cfg.GetUnsigned(g_Keys.repeater.section, g_Keys.repeater.can);
	cfg.uartDev  = g_Cfg.GetString  (g_Keys.modem.section,    g_Keys.modem.uartDevice);
	cfg.gpioDev  = g_Cfg.GetString  (g_Keys.modem.section,    g_Keys.modem.gpiochipDevice);
	cfg.freqCorr = g_Cfg.GetInt     (g_Keys.modem.section,    g_Keys.modem.freqCorr);
	cfg.power    = g_Cfg.GetFloat   (g_Keys.modem.section,    g_Keys.modem.txPower);
	cfg.afc      = g_Cfg.GetBoolean (g_Keys.modem.section,    g_Keys.modem.afc);
	cfg.isV3     = g_Cfg.GetBoolean (g_Keys.repeater.section, g_Keys.repeater.radioTypeIsV3);
	cfg.debug    = g_Cfg.GetBoolean (g_Keys.modem.section,    g_Keys.modem.debug);
	cfg.callSign.CSIn(g_Cfg.GetString(g_Keys.repeater.section, g_Keys.repeater.callsign));
	cfg.callSign.SetModule(g_Cfg.GetString(g_Keys.repeater.section, g_Keys.repeater.module).at(0));
	return false;
}

struct gpiod_line_request *CCC1200::gpioLineRequest(unsigned offset, int value, const char *consumer)
{
	struct gpiod_request_config *req_cfg = nullptr;
	struct gpiod_line_request *request = nullptr;
	struct gpiod_line_settings *settings;
	struct gpiod_line_config *line_cfg = nullptr;

	if (nullptr == gpio_chip) 
		return nullptr;

	settings = gpiod_line_settings_new();
	if (nullptr == settings)
	{
		printMsg(nullptr, TC_RED, "Could not create settings for gpio line #%u\n", offset);
	} else {
		if (gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_OUTPUT) or gpiod_line_settings_set_output_value(settings, value ? GPIOD_LINE_VALUE_ACTIVE : GPIOD_LINE_VALUE_INACTIVE))
		{
			printMsg(nullptr, TC_RED, "Could not adjust settings for gpio line #%u\n", offset);
		} else {
			line_cfg = gpiod_line_config_new();
			if (nullptr == line_cfg)
			{
				printMsg(nullptr, TC_RED, "Could not create new config for gpio line #%u\n", offset);
			} else {
				if (gpiod_line_config_add_line_settings(line_cfg, &offset, 1, settings))
				{
					printMsg(nullptr, TC_RED, "could not add settings to config of gpio line #%u\n", offset);
				} else {
					req_cfg = gpiod_request_config_new();
					if (req_cfg)
					{
						gpiod_request_config_set_consumer(req_cfg, (not consumer) ? "SPOT" : consumer);
						request = gpiod_chip_request_lines(gpio_chip, req_cfg, line_cfg);
						if (nullptr == request)
							printMsg(nullptr, TC_RED, "Could not open offset %u on configured gpio device\n", offset);
					}
				}
			}
		}
	}

	if (req_cfg)
		gpiod_request_config_free(req_cfg);
	if (line_cfg)
		gpiod_line_config_free(line_cfg);
	if (settings)
		gpiod_line_settings_free(settings);

	return request;
}

// returns true on error
bool CCC1200::gpioSetValue(unsigned offset, int value)
{
	gpiod_line_request *lr = nullptr;
	if (cfg.boot0 == offset)
		lr = boot0_line;
	else if (cfg.nrst == offset)
		lr = nrst_line;
	else {
		printMsg(nullptr, TC_RED, "gpioSetValue error: offset %u not confiugred\n", offset);
		return true;
	}

	if (gpiod_line_request_set_value(lr, offset, value ? GPIOD_LINE_VALUE_ACTIVE : GPIOD_LINE_VALUE_INACTIVE))
	{
		printMsg(nullptr, TC_RED, "Could not set gpio line #%u to %d\n", offset, value);
		return true;
	}
	return false;
}

bool CCC1200::gpioInit(const std::string &consumer)
{
	gpio_chip = gpiod_chip_open(cfg.gpioDev.c_str());
	if (nullptr == gpio_chip) {
		printMsg(nullptr, TC_RED, "Could not open %s\n", cfg.gpioDev.c_str());
		return true;
	}
	boot0_line = gpioLineRequest(cfg.boot0, 0, consumer.c_str());
	if (nullptr == boot0_line)
		return true;
	nrst_line = gpioLineRequest(cfg.nrst, 0, consumer.c_str());
	if (nullptr == nrst_line)
		return true;
	return false;
}

// Release GPIO resources
void CCC1200::gpioCleanup()
{
	printMsg(TC_CYAN, TC_DEFAULT, "GPIO reset: ");
	if (boot0_line)
	{
		gpioSetValue(cfg.boot0, 0);
		gpiod_line_request_release(boot0_line);
	}
	if (nrst_line)
	{
		gpioSetValue(cfg.nrst, 0);
		gpiod_line_request_release(nrst_line);
	}
	if (gpio_chip)
		gpiod_chip_close(gpio_chip);
	printMsg(nullptr, TC_GREEN, "GPIO lines set to low and resources released\n");
}

bool CCC1200::readDev(void *vbuf, int size)
{
	uint8_t *buf = static_cast<uint8_t *>(vbuf);
	int rd = 0;
	while (rd < size)
	{
		int r = read(fd, buf + rd, size - rd);
		if (r < 0) {
			printMsg(TC_CYAN, TC_RED, "read() %s returned error: %s", cfg.uartDev.c_str(), strerror(errno));
			return true;
		} else if (r == 0) {
			printMsg(TC_CYAN, TC_RED, "read() %s returned zero bytes\n", cfg.uartDev.c_str());
			return true;
		}
		rd += r;
	}
	return false;
}

void CCC1200::writeDev(void *buf, int size, const char *where)
{
	ssize_t n = write(fd, buf, size);
	if (n < 0) {
		printMsg(TC_CYAN, TC_YELLOW, "In %s, write() error: %s\n", where, strerror(errno));
	} else if (n != size) {
		printMsg(TC_CYAN, TC_YELLOW, "write() only wrote %d of %d in %s\n", n, size, where);
	}
	return;
}

//device config funcs
bool CCC1200::pingDev()
{
	uint8_t cid = CMD_PING;
	uint8_t cmd[3] = { cid, 3, 0 };
	uint8_t resp[7] = { 0 };

	uart_lock = true;      // prevent main loop from reading
    tcflush(fd, TCIFLUSH); // clear leftover bytes

    writeDev(cmd, 3, "pingDev");

    if (readDev(resp, sizeof(resp)))
	{
		uart_lock = false;
		return true;
	}

    uart_lock = false;

	const uint8_t good[7] { cid, 7, 0, 0, 0, 0, 0 };
    if (0 == memcmp(resp, good, 7))
	{
		printMsg(nullptr, TC_GREEN, "PONG OK\n"); //OK
        return false;
    }

	uint32_t dev_err;
	memcpy((uint8_t*)&dev_err, &resp[3], sizeof(uint32_t));
    printMsg(nullptr, TC_RED, "%02x %02x %02x PONG error code: 0x%04X\n", resp[0], resp[1], resp[2], dev_err);
    return true;
}

bool CCC1200::setRxFreq(uint32_t freq)
{
	uint8_t cid = CMD_SET_RX_FREQ;
	uint8_t cmd[3+4] = {cid, 7, 0};
	memcpy(&cmd[3], (uint8_t*)&freq, sizeof(freq));
	uint8_t resp[4] = { 0 };

	uart_lock = true;            //prevent main loop from reading
    tcflush(fd, TCIFLUSH);    //clear leftover bytes

    writeDev(cmd, 7, "setRxFreq");

    if (readDev(resp, sizeof(resp)))
	{
		uart_lock = false;
		return true;
	}

    uart_lock = false;

	printMsg(TC_CYAN, TC_DEFAULT, "RX frequency: ");
	const uint8_t good[4] = { cid, 4, 0, ERR_OK };
    if (0 == memcmp(resp, good, 4))
	{
		printMsg(nullptr, TC_GREEN, "%lu Hz\n", freq); //OK
        return false;
    }

    printMsg(nullptr, TC_RED, "Error %d setting RX frequency: %u Hz\n", resp[3], freq); //error
    return true;
}

bool CCC1200::setTxFreq(uint32_t freq)
{
	uint8_t cid = CMD_SET_TX_FREQ;
	uint8_t cmd[3+4] = {cid, 7, 0};
	memcpy(&cmd[3], (uint8_t*)&freq, sizeof(freq));
	uint8_t resp[4] = { 0 };

	uart_lock = true;      // prevent main loop from reading
    tcflush(fd, TCIFLUSH);    //clear leftover bytes

    writeDev(cmd, 7, "setTxFreq");

    if (readDev(resp, sizeof(resp)))
	{
		uart_lock = false;
		return true;
	}

    uart_lock = false;

	printMsg(TC_CYAN, TC_DEFAULT, "TX frequency: ");
	const uint8_t good[4] { cid, 4, 0, ERR_OK };
    if (0 == memcmp(resp, good, 4))
	{
		printMsg(nullptr, TC_GREEN, "%lu Hz\n", freq); //OK
        return false;
    }

    printMsg(nullptr, TC_RED, "Error %d setting TX frequency: %u Hz\n", resp[3], freq); //error
    return true;
}

bool CCC1200::setFreqCorr(int16_t corr)
{
	uint8_t cid = CMD_SET_FREQ_CORR;
	uint8_t cmd[5] = {cid, 5, 0, uint8_t(corr&0xffu), uint8_t((corr>>8)&0xffu)};
	uint8_t resp[4] = { 0 };

	uart_lock = true;            //prevent main loop from reading
    tcflush(fd, TCIFLUSH);    //clear leftover bytes

    writeDev(cmd, 5, "setFreqCorr");

	if (readDev(resp, sizeof(resp)))
	{
		uart_lock = false;
		return true;
	}

	uart_lock = false;

	printMsg(TC_CYAN, TC_DEFAULT, "Frequency correction: ");
	const uint8_t good[4] { cid, 4, 0, ERR_OK };
    if (0 == memcmp(resp, good, 4))
	{
		printMsg(nullptr, TC_GREEN, "%d\n", corr); //OK
        return false;
    }

    printMsg(nullptr, TC_RED, "Error %d setting frequency correction: %d\n", resp[3], corr); //error
    return true;
}

bool CCC1200::setAfc(bool en)
{
	uint8_t cid = CMD_SET_AFC;
	uint8_t cmd[3+1] = { cid, 4, 0, uint8_t(en ? 0 : 1) };
	uint8_t resp[4] = { 0 };

	uart_lock = true;            //prevent main loop from reading
    tcflush(fd, TCIFLUSH);    //clear leftover bytes

    writeDev(cmd, 4, "setAfc");

    if (readDev(resp, sizeof(resp)))
	{
		uart_lock = false;
		return true;
	}

    uart_lock = false;

	printMsg(TC_CYAN, TC_DEFAULT, "AFC: ");
	const uint8_t good[4] { cid, 4, 0, ERR_OK };
    if (0 == memcmp(resp, good, 4))
	{
		printMsg(nullptr, TC_GREEN, "%s\n", en==0?"disabled":"enabled"); //OK
        return false;
    }

    printMsg(nullptr, TC_RED, "Error setting AFC\n"); //error
    return true;
}

bool CCC1200::setTxPower(float power) //powr in dBm
{
	uint8_t cid = CMD_SET_TX_POWER;
	uint8_t cmd[4] = { cid, 4, 0, uint8_t(roundf(power*4.0f)) };
	uint8_t resp[4] = { 0 };

	uart_lock = true;            //prevent main loop from reading
    tcflush(fd, TCIFLUSH);    //clear leftover bytes

    writeDev(cmd, 4, "setTxPower");

    if (readDev(resp, sizeof(resp)))
	{
		uart_lock = false;
		return true;
	}

    uart_lock = false;

	printMsg(TC_CYAN, TC_DEFAULT, "TX power: ");
	uint8_t good[4] { cid, 4, 0, ERR_OK };
    if (0 == memcmp(resp, good, 4))
	{
		printMsg(nullptr, TC_GREEN, "%2.2f dBm\n", power); //OK
        return false;
    }

    printMsg(nullptr, TC_RED, "Error %d setting TX power: %2.2f dBm\n", resp[3], power); //error
    return true;
}

bool CCC1200::txrxControl(uint8_t cid, uint8_t onoff, const char *what)
{
	uint8_t cmd[4] { cid, 4, 0, onoff };
	uint8_t resp[4] = { 0 };

	uart_lock = true;          //prevent main loop from reading
	tcflush(fd, TCIFLUSH);    //clear leftover bytes

	writeDev(cmd, 4, what);

	if (readDev(resp, sizeof(resp)))
	{
		uart_lock = false;
		return true;
	}

	uart_lock = false;

	const uint8_t good[3] { cid, 4, 0 };
	if (memcmp(resp, good, 3) or (ERR_OK != resp[3] and ERR_NOP != resp[3]))
	{
		printMsg(TC_CYAN, TC_RED, "Doing %s, cmd returned %02x %02x %02x %02x\n", what, resp[0], resp[1], resp[2], resp[3]);
		return true;
	}

	return false;
}

bool CCC1200::startRx(void)
{
	return txrxControl(CMD_RX_START, 1, "startRx");
}

bool CCC1200::stopRx(void)
{
	return txrxControl(CMD_RX_START, 0, "stopRx");
}

bool CCC1200::startTx(void)
{
	return txrxControl(CMD_TX_START, 1, "startTx");
}

bool CCC1200::stopTx(void)
{
	return txrxControl(CMD_TX_START, 0, "stopTx");
}

//new, polyphase filter implementation
void CCC1200::filterSymbols(int8_t* __restrict out, const int8_t* __restrict in, const float* __restrict flt, uint8_t phase_inv)
{
	#define FLT_LEN 41
    #define TAPS_PER_PHASE 9

	//history
	static float sr[TAPS_PER_PHASE * 2] = { 0 };
	static uint8_t w = 0;

	//flush filter state
	if (in == nullptr)
	{
		memset(sr, 0, sizeof(sr));
		w = 0;
		return;
	}

	//precompute gain and sign once
    static const float gain = TX_SYMBOL_SCALING_COEFF*sqrtf(5.0f);
	const float sign = phase_inv ? -1.0f : 1.0f;

	for (uint16_t i = 0; i < SYM_PER_FRA; i++)
	{
		//insert new sample per symbol
		const float x = (float)in[i] * sign;

		//store once, duplicated for linear access
		float * __restrict hp = &sr[w];
		hp[0]			   = x;
		hp[TAPS_PER_PHASE] = x;

		//phase pointer
		const float * __restrict tp = flt;

		//generate sps (5) output samples
		for (uint8_t ph = 0; ph < 5; ph++)
		{
			float acc;

			//fully unrolled 9-tap dot product
			acc  = hp[0] * tp[0];
			acc += hp[1] * tp[1];
			acc += hp[2] * tp[2];
			acc += hp[3] * tp[3];
			acc += hp[4] * tp[4];
			acc += hp[5] * tp[5];
			acc += hp[6] * tp[6];
			acc += hp[7] * tp[7];
			acc += hp[8] * tp[8];

			out[i*5 + ph] = (int8_t)(acc * gain);

			//advance to next phase coefficients
			tp += TAPS_PER_PHASE;
		}

		//circular index update without modulo
		if (w == 0)
			w = TAPS_PER_PHASE-1;
		else
			w--;
	}
}

bool CCC1200::Start()
{
	printMsg(TC_CYAN, TC_GREEN, "Starting up mspot\n");

	// get config'ed params
	if (loadConfig())
		return true;

	//------------------------------------gpio init------------------------------------
	printMsg(TC_CYAN, TC_DEFAULT, "GPIO init: ");
	if (gpioInit("mspot"))
		return true;
	if (gpioSetValue(cfg.nrst, 0)) //both pins should be at logic low already, but better be safe than sorry
		return true;
	usleep(250000U); //250ms
	if (gpioSetValue(cfg.nrst, 1))
		return true;
	usleep(1000000U); //1s for device boot-up
	printMsg(nullptr, TC_GREEN, " OK\n");

	//-----------------------------------device part-----------------------------------
	printMsg(TC_CYAN, TC_DEFAULT, "UART init: %s at %d baud: ", (char*)cfg.uartDev.c_str(), cfg.baudRate);
	fd = open((char*)cfg.uartDev.c_str(), O_RDWR | O_NOCTTY | O_SYNC);
	if (fd < 0)
	{
		printMsg(nullptr, TC_RED, "open(%s) error: %s\n", cfg.uartDev.c_str(), strerror(errno));
		return true;
	} else if (setAttributes(cfg.baudRate, 0))
		return true;
	printMsg(nullptr, TC_GREEN, " OK\n");

	//PING-PONG test
	printMsg(TC_CYAN, TC_DEFAULT, "Radio board's reply to PING... ");
	if (pingDev())
		return true;

	//config the device
	if (setRxFreq(cfg.rxFreq) or
		setTxFreq(cfg.txFreq) or
		setFreqCorr(cfg.freqCorr) or
		setTxPower(cfg.power) or
		setAfc(cfg.afc))
	{
		return true;
	}

	txFuture = std::async(std::launch::async, &CCC1200::txProcess, this);
	if (not txFuture.valid())
	{
		printMsg(TC_CYAN, TC_RED, "Could not start Tx thread\n");
		keep_running = false;
		return true;
	}
	rxFuture = std::async(std::launch::async, &CCC1200::rxProcess, this);
	if (not rxFuture.valid())
	{
		printMsg(TC_CYAN, TC_RED, "Could not start RX thread\n");
		keep_running = false;
		return true;
	}

	//start RX
	while (stopTx())
		usleep(40e3);
	while (startRx())
		usleep(40e3);
	printMsg(TC_CYAN, TC_GREEN, "Device start - RX\n");
	return false;
}

void CCC1200::Stop()
{
	keep_running = false;
	if (rxFuture.valid())
		rxFuture.get();
	if (txFuture.valid())
		txFuture.get();
	gpioCleanup();
	printMsg(TC_CYAN, TC_GREEN, "All resources closed\n");
}

void CCC1200::txProcess()
{
	uint32_t tx_timer = 0;
	ETxState tx_state = ETxState::idle;
	SLSF lsf;
	CFrameType lsfType;
	uint16_t frame_count;
	
	while (keep_running)
	{
		auto pack = Gate2Modem.PopWaitFor(40);

		if (pack)
		{
			if (EPacketType::stream == pack->GetType())
			{
				int8_t frame_symbols[SYM_PER_FRA];					// raw frame symbols
				int8_t bsb_samples[963] = {CMD_TX_DATA, -61, 3};	// baseband samples wrapped in a frame

				if (tx_state == ETxState::idle and (not pack->IsLastPacket())) // first received frame
				{
					tx_state = ETxState::active;

					usleep(10e3);

					frame_count = 0; // we'll renumber each frame starting from zero
					// now we'll make the LSF
					memcpy(lsf.GetData(), pack->GetCDstAddress(), 12); // copy the dst & src
					lsfType.SetFrameType(lsf.GetFrameType());          // get the TYPE
					lsfType.SetMetaDataType(EMetaDatType::ecd);        // set the META to extended c/s data
					// the next line will set the frame TYPE according to the configured user's radio
					lsf.SetFrameType(lsfType.GetFrameType(cfg.isV3 ? EVersionType::v3 : EVersionType::legacy));
					auto meta = lsf.GetMetaData();             // save the address to the meta array
					g_Gateway.GetLink().CodeOut(meta);            // put the linked reflect into the 1st position
					memcpy(meta+6, pack->GetCSrcAddress(), 6); // and the src c/s in the 2nd position
					memset(meta+12, 0, 2);                     // zero the last 2 bytes
					lsf.CalcCRC();                             // this LSF is done!

					printMsg(TC_CYAN, TC_GREEN, " Stream TX start\n");

					//stop RX, set PA_EN=1 and initialize TX
					while (stopRx())
						usleep(40e3);
					usleep(2e3);
					while (startTx())
						usleep(40e3);
					usleep(10e3);

					//flush the RRC baseband filter
					filterSymbols(nullptr, nullptr, nullptr, 0);
				
					//generate frame symbols, filter them and send out to the device
					//we need to prepare 3 frames to begin the transmission - preamble, LSF and stream frame 0
					//let's start with the preamble
					uint32_t frame_buff_cnt=0;
					gen_preamble_i8(frame_symbols, &frame_buff_cnt, PREAM_LSF);

					//filter and send out to the device
					filterSymbols(bsb_samples+3, frame_symbols, rrc_taps_5_poly, 0);
					writeDev(bsb_samples, sizeof(bsb_samples), "SM Pream");

					//now the LSF
					gen_frame_i8(frame_symbols, nullptr, FRAME_LSF, (lsf_t *)(lsf.GetCData()), 0, 0);

					//filter and send out to the device
					filterSymbols(bsb_samples+3, frame_symbols, rrc_taps_5_poly, 0);
					writeDev(bsb_samples, sizeof(bsb_samples), "SM LSF");

					//finally, the first frame
					gen_frame_i8(frame_symbols, pack->GetCPayload(), FRAME_STR, (const lsf_t *)(lsf.GetCData()), 0, 0);

					//filter and send out to the device
					filterSymbols(bsb_samples+3, frame_symbols, rrc_taps_5_poly, 0);
					writeDev(bsb_samples, sizeof(bsb_samples), "SM first Frame");
				}
				else
				{
					if (0 == ++frame_count % 6u)
					{
						// make a LSF from the LSD in this packet
						memcpy(lsf.GetData(), pack->GetCDstAddress(), 28);
						lsfType.SetFrameType(pack->GetFrameType());
						pack->SetFrameType(lsfType.GetFrameType(cfg.isV3 ? EVersionType::v3 : EVersionType::legacy));
						lsf.CalcCRC();
					}
					uint8_t lich_count = frame_count % 6u;
					if (pack->IsLastPacket())
						frame_count |= 0x8000u;

					//only one frame is needed
					gen_frame_i8(frame_symbols, pack->GetCPayload(), FRAME_STR, (lsf_t *)(lsf.GetCData()), lich_count, frame_count);

					//filter and send out to the device
					filterSymbols(bsb_samples+3, frame_symbols, rrc_taps_5_poly, 0);
					writeDev(bsb_samples, sizeof(bsb_samples), "SM Frame");
				}

				if (pack->IsLastPacket()) //last stream frame
				{
					//send the final EOT marker
					uint32_t frame_buff_cnt=0;
					gen_eot_i8(frame_symbols, &frame_buff_cnt);

					//filter and send out to the device
					filterSymbols(bsb_samples+3, frame_symbols, rrc_taps_5_poly, 0);
					writeDev(bsb_samples, sizeof(bsb_samples), "SM EOT");

					printMsg(TC_CYAN, TC_GREEN, " Stream TX end\n");
					usleep(8*40e3); //wait 320ms (8 M17 frames) - let the transmitter consume all the buffered samples

					//restart RX
					while (stopTx())
						usleep(40e3);
					while (startRx())
						usleep(40e3);
					printMsg(TC_CYAN, TC_GREEN, " RX start\n");
					g_GateState.Set2IdleIf(EGateState::gatestreamin);

					tx_state = ETxState::idle;
				}
				tx_timer = getMS();
			}

			//M17 packet data - "Packet Mode IP Packet"
			else if (EPacketType::packet == pack->GetType())
			{
				printMsg(TC_CYAN, TC_GREEN, " M17 Inet packet received\n");

				const CCallsign dst(pack->GetCDstAddress());
				const CCallsign src(pack->GetCSrcAddress());
				CFrameType TYPE(pack->GetFrameType());
				if ((cfg.isV3 ? EVersionType::v3 : EVersionType::legacy) != TYPE.GetVersion())
				{
					pack->SetFrameType(TYPE.GetFrameType(cfg.isV3 ? EVersionType::v3 : EVersionType::legacy));
					pack->CalcCRC();
				}
				const auto can = TYPE.GetCan();
				const unsigned type = *(pack->GetCPayload());
				
				printMsg(TC_CYAN, TC_DEFAULT, " ├ ");
				printMsg(nullptr, TC_YELLOW, "DST: ");
				printMsg(nullptr, TC_DEFAULT, "%s\n", dst.c_str());
				printMsg(TC_CYAN, TC_DEFAULT, " ├ ");
				printMsg(nullptr, TC_YELLOW, "SRC: ");
				printMsg(nullptr, TC_DEFAULT, "%s\n", src.c_str());
				printMsg(TC_CYAN, TC_DEFAULT, " ├ ");
				printMsg(nullptr, TC_YELLOW, "CAN: ");
				printMsg(nullptr, TC_DEFAULT, "%u\n", unsigned(can));
				if (type != 5u or *(pack->GetCPayload()+pack->GetSize()-3)) //assuming 1-byte type specifier
				{
					printMsg(TC_CYAN, TC_DEFAULT, " └ ");
					printMsg(nullptr, TC_YELLOW, "TYPE: ");
					printMsg(nullptr, TC_DEFAULT, "%u\n", unsigned(pack->GetCPayload()[0]));
				}
				else
				{
					printMsg(TC_CYAN, TC_DEFAULT, " ├ ");
					printMsg(nullptr, TC_YELLOW, "TYPE: ");
					printMsg(nullptr, TC_DEFAULT, "SMS\n");
					printMsg(TC_CYAN, TC_DEFAULT, " └ ");
					printMsg(nullptr, TC_YELLOW, "MSG: ");
					printMsg(nullptr, TC_DEFAULT, "%s\n", (char *)(pack->GetPayload()+1));
				}

				//TODO: handle TX here
				int8_t frame_symbols[SYM_PER_FRA];						//raw frame symbols
				int8_t bsb_samples[SYM_PER_FRA*5];						//filtered baseband samples = symbols*sps
				uint8_t bsb_chunk[963] = {CMD_TX_DATA, 0xC3, 0x03};		//baseband samples wrapped in a frame
				
				printMsg(TC_CYAN, TC_GREEN, " Packet TX start\n");

				//stop RX, set PA_EN=1 and initialize TX
				while (stopRx())
					usleep(40e3);
				usleep(2e3);

				while (startTx())
					usleep(40e3);
				usleep(10e3);
				
				//flush the RRC baseband filter
				filterSymbols(nullptr, nullptr, nullptr, 0);
				
				//generate frame symbols, filter them and send out to the device
				//we need to prepare 3 frames to begin the transmission - preamble, LSF and stream frame 0
				//let's start with the preamble
				uint32_t frame_buff_cnt = 0;
				gen_preamble_i8(frame_symbols, &frame_buff_cnt, PREAM_LSF);
				
				//filter and send out to the device
				filterSymbols(bsb_samples, frame_symbols, rrc_taps_5_poly, 0);
				memcpy(&bsb_chunk[3], bsb_samples, sizeof(bsb_samples));
				writeDev(bsb_samples, sizeof(bsb_samples), "PM LSF Pream");
				
				//now the LSF
				gen_frame_i8(frame_symbols, nullptr, FRAME_LSF, (lsf_t*)(pack->GetCDstAddress()), 0, 0);
				
				//filter and send out to the device
				filterSymbols(bsb_samples, frame_symbols, rrc_taps_5_poly, 0);
				memcpy(&bsb_chunk[3], bsb_samples, sizeof(bsb_samples));
				writeDev(bsb_samples, sizeof(bsb_samples), "PM LSF");
				
				//packet frames
				uint16_t pld_len = pack->GetSize() - 34u;
				uint8_t frame = 0;
				uint8_t pld[26];
				
				while(pld_len > 25)
				{
					memcpy(pld, pack->GetCPayload()+(frame*25), 25);
					pld[25] = frame<<2;
					gen_frame_i8(frame_symbols, pld, FRAME_PKT, nullptr, 0, 0);
					filterSymbols(bsb_samples, frame_symbols, rrc_taps_5_poly, 0);
					memcpy(&bsb_chunk[3], bsb_samples, sizeof(bsb_samples));
					writeDev(bsb_samples, sizeof(bsb_samples), "PM Frame");
					pld_len -= 25;
					frame++;
					usleep(40*1000U);
				}
				memset(pld, 0, 26);
				memcpy(pld, pack->GetCPayload()+(frame*25), pld_len);
				pld[25] = (1<<7)  | (pld_len<<2); //EoT flag set, amount of remaining data in the 'frame number' field
				gen_frame_i8(frame_symbols, pld, FRAME_PKT, nullptr, 0, 0);
				filterSymbols(bsb_samples, frame_symbols, rrc_taps_5_poly, 0);
				memcpy(&bsb_chunk[3], bsb_samples, sizeof(bsb_samples));
				writeDev(bsb_samples, sizeof(bsb_samples), "PM Frame");
				usleep(40*1000U);

				//now the final EOT marker
				frame_buff_cnt=0;
				gen_eot_i8(frame_symbols, &frame_buff_cnt);

				//filter and send out to the device
				filterSymbols(bsb_samples, frame_symbols, rrc_taps_5_poly, 0);
				memcpy(&bsb_chunk[3], bsb_samples, sizeof(bsb_samples));
				writeDev(bsb_samples, sizeof(bsb_samples), "PM EOT");

				printMsg(TC_CYAN, TC_GREEN, " PKT TX end\n");
				usleep(3*40e3); //wait 120ms (3 M17 frames)

				//restart RX
				while (stopTx())
					usleep(40e3);
				while (startRx())
					usleep(40e3);
				printMsg(TC_CYAN, TC_GREEN, " RX start\n");

				g_GateState.Set2IdleIf(EGateState::gatepacketin);
				tx_timer = getMS();

				tx_state = ETxState::idle;
			}
		}

		//tx timeout
		if ((tx_state == ETxState::active) and ((getMS()-tx_timer) > 240)) //240ms timeout
		{
			g_GateState.Set2IdleIf(EGateState::gatestreamin);
			printMsg(TC_CYAN, TC_RED, " TX timeout\n");
			//usleep(10*40e3); //wait 400ms (10 M17 frames)

			//restart RX
			while (stopTx())
				usleep(40e3);
			while (startRx())
				usleep(40e3);
			printMsg(TC_CYAN, TC_GREEN, " RX start\n");

			tx_state=ETxState::idle;
		}
	}
	printMsg(TC_CYAN, TC_GREEN, "Tx loop terminated\n");
}

void CCC1200::rxProcess()
{
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

	SLSF lsf;
	CFrameType TYPE;

	ERxState rx_state = ERxState::idle;

	// for stream mode
	uint8_t lich_parts = 0;

	// for packet mode
	uint8_t pkt_pld[825];
	uint8_t *ppkt = pkt_pld;
	uint16_t plsize = 0;

	//file for debug data dumping
	//FILE *fp=fopen("test_dump.bin", "wb");

	last_refl_ping = time(nullptr);

	fd_set rfds;

	while(keep_running)
	{
		FD_ZERO(&rfds);
		FD_SET(fd, &rfds);
		struct timeval tv;
		tv.tv_sec = 0;
		tv.tv_usec = 40000;

		auto sval = select(fd+1, &rfds, nullptr, nullptr, &tv);
		if (sval < 0)
		{
			if (EINTR != errno)
				printMsg(TC_RED, "select() error: %s\n", strerror(errno));
			keep_running = false;
			break;
		}

		//are there any new baseband samples to process?
		if ((not uart_lock) and FD_ISSET(fd, &rfds))
		{
			if (readDev(&rx_bsb_sample, 1))
			{
				keep_running = false;
				break;
			}

			rx_header.Push(rx_bsb_sample);

			if (rx_header[0]==CMD_RX_DATA and rx_header[1]==0xC3 and rx_header[2]==0x03)
			{
				readDev(raw_bsb_rx, 960);
				uart_rx_data_valid = true;
			}
		}

		if (uart_rx_data_valid)
		{
			// we can clear this right away
			uart_rx_data_valid = false;
			for (uint16_t ii=0; ii<960; ii++)
			{
				//push the next sample into the buffer
				flt_buff.Push(raw_bsb_rx[ii]);

				// filter the buffer to get the new sample
				float f_sample = 0.0f;
				for (uint8_t i=0; i<flt_buff.Size(); i++)
					f_sample += rrc_taps_5[i] * float(flt_buff[i]);

				// push the sample on into the float buffer
				f_flt_buff.Push(f_sample*RX_SYMBOL_SCALING_COEFF);

				//L2 norm check against syncword
				float symbols[16];
				for (uint8_t i=0; i<16; i++)
					symbols[i]=f_flt_buff[i*5];
				float sed_lsf = sed(symbols, lsf_sync_ext,    16);
				float sed_sma = sed(symbols, str_sync_symbols, 8);
				float sed_pma = sed(symbols, pkt_sync_symbols, 8);
				for (uint8_t i=0; i<16; i++)
					symbols[i]=f_flt_buff[960+i*5];
				float sed_eot = sed(symbols, eot_symbols,      8);
				float sed_smb = sed(symbols, str_sync_symbols, 8);
				float sed_str = sed_sma + ((sed_smb < sed_eot) ? sed_smb : sed_eot);
				float sed_pmb = sed(symbols, pkt_sync_symbols, 8);
				float sed_pkt = sed_pma + ((sed_pmb < sed_eot) ? sed_pmb : sed_eot);
				// if (sed_lsf < lmin) {
				// 	lmin = sed_lsf;
				// 	printMsg(TC_CYAN, TC_GREEN, "lmin=%6.2f smin=%6.2f pmin=%6.2f\n", lmin, smin, pmin);
				// }
				// if (sed_str < smin) {
				// 	smin = sed_str;
				// 	printMsg(TC_CYAN, TC_GREEN, "lmin=%6.2f smin=%6.2f pmin=%6.2f\n", lmin, smin, pmin);
				// }
				// if (sed_pkt < pmin) {
				// 	pmin = sed_pkt;
				// 	printMsg(TC_CYAN, TC_GREEN, "lmin=%6.2f smin=%6.2f pmin=%6.2f\n", lmin, smin, pmin);
				// }

				//printMsg(TC_YELLOW, "%.3u %6.2f %6.2f %6.2f\n", ii, sed_lsf, sed_pkt, sed_str);

				//LSF received at idle state
				if ((sed_lsf <= 22.25f) and (rx_state == ERxState::idle))
				{
					//find minimum
					uint8_t sample_offset = 0;
					for (uint8_t i=1; i<=2; i++)
					{
						for (uint8_t j=0; j<16; j++)
							symbols[j] = f_flt_buff[j*5+i];

						float d = sed(symbols, lsf_sync_ext, 16);

						if (d < sed_lsf)
						{
							sed_lsf = d;
							sample_offset = i;
						}
					}

					float pld[SYM_PER_PLD];

					for (uint16_t i=0; i<SYM_PER_PLD; i++)
					{
						pld[i]=f_flt_buff[16*5+i*5+sample_offset]; //add symbol timing correction
					}

					uint32_t e = decode_LSF((lsf_t*)(lsf.GetData()), pld);


					printMsg(TC_CYAN, TC_MAGENTA, "RF LSF: ");

					if (lsf.CheckCRC()) //if CRC valid
					{
						printMsg(nullptr, TC_RED, "CRC ERR\n");
					}
					else
					{
						(void)g_GateState.TryState(EGateState::modemin);
						got_lsf = true;
						TYPE.SetFrameType(lsf.GetFrameType());
						rx_state = (EPayloadType::packet == TYPE.GetPayloadType()) ? ERxState::ptk : ERxState::str; // the LSF
						sample_cnt = 0; // the LSF

						const CCallsign dst(lsf.GetCDstAddress());
						const CCallsign src(lsf.GetCSrcAddress());

						printMsg(nullptr, TC_GREEN, "DST: %s SRC: %s TYPE: %04X (CAN=%d) ED^2: %5.2f MER: %4.1f%% ii: %d\n", dst.c_str(), src.c_str(), TYPE.GetOriginType(), TYPE.GetCan(), sed_lsf, float(e)*escale, ii);

						if (EPayloadType::packet != TYPE.GetPayloadType()) //if stream
						{
							// init values for stream mode
							fn = 0;
							sid = g_RNG.Get();
						}
					}
				}

				//stream frame received
				else if (sed_str <= 25.0f)
				{
					sample_cnt = 0; // packet frame
					//find L2's minimum
					uint8_t sample_offset=0;
					for (uint8_t i=1; i<=2; i++)
					{
						for (uint8_t j=0; j<16; j++)
							symbols[j]=f_flt_buff[j*5+i];
						
						float tmp_a = sed(symbols, str_sync_symbols, 8);
						// check the next frame, look for another data frame or EOT frame
						for (uint8_t j=0; j<16; j++)
							symbols[j] = f_flt_buff[960+j*5+i];
						float tmp_b = sed(symbols, str_sync_symbols, 8);
						float tmp_e = sed(symbols, eot_symbols,      8);
						float d = tmp_a + ((tmp_b < tmp_e) ? tmp_b : tmp_e);

						if (d < sed_str)
						{
							sed_str = d;
							sample_offset = i;
						}
					}

					float pld[SYM_PER_PLD];
					
					for (uint16_t i=0; i<SYM_PER_PLD; i++)
					{
						pld[i]=f_flt_buff[40+i*5+sample_offset];
					}

					uint8_t lich[6];
					uint8_t lich_cnt;
					uint8_t frame_data[16];
					uint32_t e = decode_str_frame(frame_data, lich, &fn, &lich_cnt, pld);
					if (0 == lich_cnt)
						lich_parts = 0;
					uint16_t frame_count = fn & 0x7fffu;
					if (first_frame) {
						last_fn = frame_count - 1;
						first_frame = false;
					}
					
					if (((last_fn+1) & 0x7fffu) == frame_count)
					{
						if (got_lsf) // send this data frame to the gateway
						{
							auto p = std::make_unique<CPacket>();
							p->Initialize(EPacketType::stream);
							p->SetStreamId(sid);
							memcpy(p->GetDstAddress(), lsf.GetCData(), 28);
							p->SetFrameNumber(fn);
							memcpy(p->GetPayload(), frame_data, 16);
							p->CalcCRC();
							if (g_GateState.TryState(EGateState::modemin))
								Modem2Gate.Push(p);

							if (cfg.debug)
							{
								printMsg(TC_CYAN, TC_YELLOW, "RF Stream Frame: ");
								printMsg(nullptr, TC_GREEN, "FN:%04X LICH_CNT:%d ED^2:%5.2f MER:%4.1f%% ii=%u\n", fn, lich_cnt, sed_str, float(e)*escale, ii);
							}
						}

						if (lich_parts != 0x3fu) // if the lich data is not complete
						{
							//reconstruct LSF chunk by chunk
							memcpy(lsf_b+(5u*lich_cnt), lich, 5); //40 bits
							lich_parts |= (1<<lich_cnt);
							if (0x3fu == lich_parts) //collected all of them?
							{
								if (g_Crc.CheckCRC(lsf_b, 30)) {
									if (cfg.debug)
									{
										printMsg(TC_CYAN, TC_MAGENTA, "RF LICH LSF: ");
										printMsg(nullptr, TC_RED, "CRC Error\n");
										Dump(nullptr, lsf_b, 30);
									}
								} else {
									memcpy(lsf.GetData(), lsf_b, 30);
									if (not got_lsf)
									{
										TYPE.SetFrameType(lsf.GetFrameType());
										(bool)g_GateState.TryState(EGateState::modemin);
										rx_state = (EPayloadType::packet == TYPE.GetPayloadType()) ? ERxState::ptk : ERxState::str; // the LICH
										sample_cnt = 0; // LICH LSF
										got_lsf = true;
										sid = g_RNG.Get();
										const CCallsign dst(lsf.GetCDstAddress());
										const CCallsign src(lsf.GetCSrcAddress());
										TYPE.SetFrameType(lsf.GetFrameType());
										if (cfg.debug)
										{
											printMsg(TC_CYAN, TC_MAGENTA, "LICH LSF: ");
											printMsg(nullptr, TC_GREEN, "DST: %s SRC: %s TYPE: 0x%04X (CAN=%d)\n", dst.c_str(), src.c_str(), lsf.GetFrameType(), TYPE.GetCan());
										}
									}
								}
								lich_parts = 0;
							}
						}
						last_fn = fn;
					}

					if (fn >> 15) // is this the last frame?
					{
						// this is the last packet
						rx_state = ERxState::idle; // last stream frame
						got_lsf = false;
						lich_parts = 0;
						last_fn = 0xfffu;
						first_frame = true;
					}
				}

				//TODO: handle packet mode reception over RF
				else if ((sed_pkt <= 25.0f) and (rx_state == ERxState::ptk))
				{
					//find L2's minimum
					uint8_t sample_offset = 0;
					for (uint8_t i=1; i<=2; i++)
					{
						for (uint8_t j=0; j<8; j++)
							symbols[j]=f_flt_buff[j*5+i];
							
						float tmp_a = sed(symbols, pkt_sync_symbols, 8);
						for (uint8_t j=0; j<16; j++)
							symbols[j] = f_flt_buff[960+j*5+i];
						float tmp_b = sed(symbols, pkt_sync_symbols, 8);
						float tmp_c = sed(symbols, eot_symbols, 8);
						float d = tmp_a + ((tmp_c > tmp_b) ? tmp_b : tmp_c);

						if (d < sed_pkt)
						{
							sed_pkt = d;
							sample_offset = i;
						}
					}

					float pld[SYM_PER_PLD];
					for (uint16_t i=0; i<SYM_PER_PLD; i++)
					{
						pld[i]=f_flt_buff[8*5+i*5+sample_offset];
					}

					uint8_t eof, pkt_fn;
					uint32_t e = decode_pkt_frame(ppkt, &eof, &pkt_fn, pld);
					sample_cnt = 0;

					if (cfg.debug) printMsg(TC_CYAN, TC_DEFAULT, "RF PacketFrame: EOF: %s FN: %u d^2:%5.2f MER: %4.1f ii:%u\n", (eof ? "true " : "false"), unsigned(pkt_fn), sed_pkt, e*escale, ii);

					// increment size and pointer
					plsize += eof ? pkt_fn : 25;
					ppkt += 25;

					if(eof)
					{
						if (g_Crc.CheckCRC(pkt_pld, plsize))
						{
							printMsg(TC_CYAN, TC_RED, "RF PKT: Payload CRC failed");
							Dump(nullptr, pkt_pld, plsize);
						} else {
							if (got_lsf)
							{
								if (g_GateState.TryState(EGateState::modemin)) {
									auto pkt = std::make_unique<CPacket>();
									pkt->Initialize(EPacketType::packet, plsize+34);
									memcpy(pkt->GetData()+4, lsf.GetCData(), 30);
									memcpy(pkt->GetData()+34, pkt_pld, plsize);
									// crc will be calulated by the gateway
									Modem2Gate.Push(pkt);
								}
							} else {
								printMsg(TC_CYAN, TC_RED, "Got a Packet Payload, but not the LSF!");
							}
							if (cfg.debug) {
								if (0x5u == *pkt_pld and 0u == pkt_pld[plsize-3]) {
									printMsg(TC_CYAN, TC_DEFAULT, "RF SMS Msg: %s", (char *)(pkt_pld+1));
								} else {
									printMsg(TC_CYAN, TC_DEFAULT, "Packet Payload:\n");
									Dump(nullptr, pkt_pld, plsize);
								}
							}
						}
					}
				}
				
				//RX sync timeout
				if (rx_state != ERxState::idle)
				{
					sample_cnt++;
					if (960*2 <= sample_cnt) // 80 ms without detecting anything in the sync'ed state
					{
						printMsg(TC_CYAN, TC_RED, "RF Timeout\n");
						rx_state = ERxState::idle; // timeout
						got_lsf = false;
						sample_cnt = 0;
						// stream mode reset
						lich_parts = 0;
						last_fn = 0xffffu;
						// packet mode reset
						ppkt = pkt_pld;
					}
				}
			}
		}
	}
	printMsg(TC_CYAN, TC_GREEN, "Rx loop terminated\n");
}

/**
 * @brief Calculate squared Euclidean distance between two n-dimensional vectors.
 * It is the sum of squared differences.
 *
 * @param v1 Vector 1 - floats.
 * @param v2 Vector 2 - signed ints.
 * @param n Vectors' size.
 * @return float Squared distance between two points.
 */
float CCC1200::sed(const float *v1, const int8_t *v2, const unsigned n) const
{
	float r = 0.0f;
	for (unsigned i=0; i<n; i++)
	{
		auto x = v1[i] - float(v2[i]);
		r += x * x;
	}
	return r;
}
