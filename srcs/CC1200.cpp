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

#include <thread>

#include <unistd.h>
#include <fcntl.h>

#include "SafePacketQueue.h"
#include "GateState.h"
#include "Callsign.h"
#include "Gateway.h"
#include "CC1200.h"
#include "Random.h"
#include "LSF.h"
#include "Log.h"

extern CGateway    g_Gate;
extern CRandom     g_RNG;
extern CCRC        g_Crc;
extern CGateState  g_GateState;
extern IPFrameFIFO Modem2Gate;
extern IPFrameFIFO Gate2Modem;

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

static const char *err_str[9] { "Okay", "TRX PLL", "TRX SPI", "Range", "Command Malformed", "Busy", "Buffer Full", "NOP", "Other" };

uint32_t CCC1200::getMilliseconds(void)
{
	struct timespec spec;

	clock_gettime(CLOCK_REALTIME, &spec);

	time_t s = spec.tv_sec;
	uint32_t ms = roundf(spec.tv_nsec/1.0e6); //convert nanoseconds to milliseconds
	if(ms>999)
	{
		s++;
		ms=0;
	}

	return s*1000 + ms;
}

speed_t CCC1200::getBaud(uint32_t baud)
{
	switch(baud)
	{
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
		default: 
			return B0;
	}
}

bool CCC1200::setIinterface(uint32_t speed, int parity)
{
	struct termios tty;
	auto rval = tcgetattr(fd, &tty);
	if (rval < 0)
	{
		LogError("tcgetattr() error: ", strerror(errno));
		return true;
 	} else if (rval > 0) {
		LogError("tcgetattr() returned unexpected value: %d", rval);
		return true;
	}

	auto brate = getBaud(speed);
	if (brate == B0)
	{
		LogError("Could not set baud rate to %u", speed);
		return 0;
	}

	cfsetospeed(&tty, brate);
	cfsetispeed(&tty, brate);

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

	rval = tcsetattr(fd, TCSANOW, &tty);
	if (rval < 0)
	{
		LogError("tcsetattr() error: ", strerror(errno));
		return true;
 	} else if (rval > 0) {
		LogError("tcsetattr() returned unexpected value: %d", rval);
		return true;
	}
	
	return false;
}

void CCC1200::loadConfig()
{
	extern CConfigure g_Cfg;
	cfg.gpioDev  = g_Cfg.GetString(  g_Keys.modem.section,    g_Keys.modem.gpiochipDevice);
	cfg.uartDev  = g_Cfg.GetString(  g_Keys.modem.section,    g_Keys.modem.uartDevice);
	cfg.baudRate = g_Cfg.GetUnsigned(g_Keys.modem.section,    g_Keys.modem.uartBaudRate);
	cfg.rxFreq   = g_Cfg.GetUnsigned(g_Keys.modem.section,    g_Keys.modem.rxFreq);
	cfg.txFreq   = g_Cfg.GetUnsigned(g_Keys.modem.section,    g_Keys.modem.txFreq);
	cfg.afc      = g_Cfg.GetBoolean( g_Keys.modem.section,    g_Keys.modem.afc);
	cfg.freqCorr = g_Cfg.GetInt(     g_Keys.modem.section,    g_Keys.modem.freqCorr);
	cfg.power    = g_Cfg.GetFloat(   g_Keys.modem.section,    g_Keys.modem.txPower);
	cfg.boot0    = g_Cfg.GetUnsigned(g_Keys.modem.section,    g_Keys.modem.boot0);
	cfg.nrst     = g_Cfg.GetUnsigned(g_Keys.modem.section,    g_Keys.modem.nrst);
	cfg.can      = g_Cfg.GetUnsigned(g_Keys.repeater.section, g_Keys.repeater.can);
	cfg.debug    = g_Cfg.GetBoolean( g_Keys.repeater.section, g_Keys.repeater.debug);
	cfg.isV3     = g_Cfg.GetBoolean( g_Keys.repeater.section, g_Keys.repeater.radioTypeIsV3);
	// the callsign has to be assembled from two items
	cfg.callSign.CSIn(g_Cfg.GetString(g_Keys.repeater.section, g_Keys.repeater.callsign));
	cfg.callSign.SetModule(g_Cfg.GetString(g_Keys.repeater.section, g_Keys.repeater.module).at(0));
}

struct gpiod_line_request *CCC1200::gpioLineRequest(unsigned int offset, int value, const std::string &consumer)
{
	struct gpiod_request_config *req_cfg = nullptr;
	struct gpiod_line_request *request = nullptr;
	struct gpiod_line_settings *settings;
	struct gpiod_line_config *line_cfg;

	if (nullptr == gpioChip) 
		return nullptr;

	settings = gpiod_line_settings_new();
	if (nullptr == settings)
	{
		LogError("Could not create settings for gpio line #%u", offset);
	} else {
		if (gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_OUTPUT) or gpiod_line_settings_set_output_value(settings, value ? GPIOD_LINE_VALUE_ACTIVE : GPIOD_LINE_VALUE_INACTIVE))
		{
			LogError("Could not adjust settings for gpio line #%u", offset);
		} else {
			line_cfg = gpiod_line_config_new();
			if (nullptr == line_cfg)
			{
				LogError("Could not create new config for gpio line #%u", offset);
			} else {
				if (gpiod_line_config_add_line_settings(line_cfg, &offset, 1, settings))
				{
					LogError("could not add settings to config of gpio line #%u", offset);
				} else {
					req_cfg = gpiod_request_config_new();
					if (req_cfg)
					{
						gpiod_request_config_set_consumer(req_cfg, (consumer.empty()) ? "MSPOT" : consumer.c_str());
						request = gpiod_chip_request_lines(gpioChip, req_cfg, line_cfg);
						if (nullptr == request)
							LogError("Could not open offset %u on configured gpio device", offset);
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
		lr = reqBoot0;
	else if (cfg.nrst == offset)
		lr = reqNrst;
	else {
		LogError("gpioSetValue error: offset %u not confiugred", offset);
		return true;
	}

	if (gpiod_line_request_set_value(lr, offset, value ? GPIOD_LINE_VALUE_ACTIVE : GPIOD_LINE_VALUE_INACTIVE))
	{
		LogError("Could not set gpio line #%u to %d", offset, value);
		return true;
	}
	return false;
}

bool CCC1200::readDev(uint8_t *buf, int size)
{
	int rd = 0;
	while (rd < size)
	{
		int r = read(fd, buf + rd, size - rd);
		if (r < 0) {
			LogError("read() returned error: ", strerror(errno));
			return true;
		} else if (r == 0) {
			LogError("read() returned zero");
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
		LogError("In %s, write() error: %s", where, strerror(errno));
	} else if (n != size) {
		LogError("write() only wrote %d of %d in %s", n, size, where);
	}
	return;
}

//device config funcs
bool CCC1200::testPING(void)
{
	uint8_t cid = CMD_PING;
	uint8_t cmd[3] = { cid, 3, 0 };
	uint8_t resp[7] = { 0 };

	uart_lock = true;          //prevent main loop from reading
    tcflush(fd, TCIFLUSH);    //clear leftover bytes

    writeDev(cmd, 3, "testPING");

	if (readDev(resp, 7))
	{
		uart_lock = false;
		return true;
	}

    uart_lock = false;

	const uint8_t goodResp[] { cid, 7, 0, 0, 0, 0, 0 };
    if (memcmp(resp, goodResp, 7))
	{
		uint32_t dev_err;
		memcpy(&dev_err, resp + 3, 4);
		LogError("PONG error code: 0x%04X", dev_err);
		return true;
    }

	LogInfo("PONG OK"); //OK
    return false;
}

bool CCC1200::setRxFreq(uint32_t freq)
{
	uint8_t cid = CMD_SET_RX_FREQ;
	uint8_t cmd[7] = { cid, 7, 0 };
	memcpy(cmd+3, &freq, 4);
	uint8_t resp[4] = {0};

	uart_lock = true;            //prevent main loop from reading
    tcflush(fd, TCIFLUSH);    //clear leftover bytes

    writeDev(cmd, 7, "setRXFreq");

	if (readDev(resp, 4))
	{
		uart_lock = false;
		return true;
	}

    uart_lock = false;

	const uint8_t goodresp[] { cid, 4, 0, 0 };

    if (memcmp(resp, goodresp, 4))
	{
		LogError("Error setting Rx frequency: %s", err_str[resp[3]]);
		return true;
    }

	LogInfo("Rx frequency: %u", freq);
	return false;
}

bool CCC1200::setTxFreq(uint32_t freq)
{
	uint8_t cid = CMD_SET_TX_FREQ;
	uint8_t cmd[7] = { cid, 7, 0 };
	memcpy(cmd+3, &freq, 4);
	uint8_t resp[4] = { 0 };

	uart_lock = true;            //prevent main loop from reading
    tcflush(fd, TCIFLUSH);    //clear leftover bytes

	writeDev(cmd, 7, "setTxFreq");
	if (readDev(resp, 4))
	{
		uart_lock = false;
		return true;
	}

	uart_lock = false;

	const uint8_t goodresp[] { cid, 4, 0, 0 };

    if (memcmp(resp, goodresp, 4))
	{
		LogError("Error setting Tx frequency: %s", err_str[resp[3]]);
		return true;
	}
	
	LogInfo("Tx frequency: %u", freq);
	return false;
}

bool CCC1200::setFreqCorr(int16_t corr)
{
	uint8_t cid = CMD_SET_FREQ_CORR;
	uint8_t cmd[5] = { cid, 5, 0, uint8_t(corr & 0xff), uint8_t((corr>>8) & 0xff) };
	uint8_t resp[4] = { 0 };

	uart_lock = true;            //prevent main loop from reading
    tcflush(fd, TCIFLUSH);    //clear leftover bytes

    writeDev(cmd, 5, "setFreqCorr");

    if (readDev(resp, 4))
	{
		uart_lock = false;
		return true;
	}

    uart_lock = false;

	const uint8_t goodResp[] { cid, 4, 0, 0 };

    if (memcmp(resp, goodResp, 4))
	{
		LogError("Error setting frequency offset: %s", err_str[resp[3]]);
		return true;
    }

	LogInfo("Frequency correction: %d", corr);
    return false;
}

bool CCC1200::setAFC(bool en)
{
	uint8_t cid = CMD_SET_AFC;
	uint8_t cmd[4] = { cid, 4, 0, uint8_t(en ? 1 : 0) };
	uint8_t resp[4] = { 0 };

	uart_lock;            //prevent main loop from reading
    tcflush(fd, TCIFLUSH);    //clear leftover bytes

    writeDev(cmd, 4, "setAFC");

	if (readDev(resp, 4))
	{
		uart_lock = false;
		return true;
	}

    uart_lock = false;

	const uint8_t goodResp[] { cid, 4, 0, 0 };

    if (memcmp(resp, goodResp, 4))
	{
		LogError("Error setting AFC: %s", err_str[resp[3]]);
		return true;
    }

    LogInfo("AFC is%s enabled", en?"":" NOT");
	return false;
}

bool CCC1200::setTxPower(float power) //powr in dBm
{
	uint8_t cid = CMD_SET_TX_POWER;
	uint8_t cmd[4] = {cid, 4, 0, uint8_t(roundf(power*4.0f)) };
	uint8_t resp[4] = { 0 };

	uart_lock = true;		//prevent main loop from reading
    tcflush(fd, TCIFLUSH);	//clear leftover bytes

    writeDev(cmd, 4, "setTxPower");

	if (readDev(resp, 4))
	{
		uart_lock = false;
		return true;
	}

    uart_lock = false;

	const uint8_t goodResp[] { cid, 4, 0, 0 };

    if (memcmp(resp, goodResp, 4))
	{
		LogError("Error setting Tx Power: %s", err_str[resp[3]]);
		return true;
    }

	LogInfo("Tx Power: %.2f dBm", power);
	return false;
}

bool CCC1200::txrxControl(uint8_t cid, uint8_t onoff, const char *what)
{
	uint8_t cmd[4] { cid, 4, 0, onoff };
	uint8_t resp[4] = { 0 };

	uart_lock = true;          //prevent main loop from reading
	tcflush(fd, TCIFLUSH);    //clear leftover bytes

	writeDev(cmd, 4, what);

	if (readDev(resp, 4))
	{
		uart_lock = false;
		return true;
	}

	uart_lock = false;

	if (cid != resp[0] or 4u != resp[1] or 0 != resp[2] or (ERR_OK != resp[3] and ERR_NOP != resp[3]))
	{
		LogDebug("Doing %s, cmd returned %02x %02x %02x %02x", what, resp[0], resp[1], resp[2], resp[3]);
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
	LogInfo("Starting the CC1200");
	fd = -1;
	loadConfig();
	srand(time(nullptr));
	//------------------------------------gpio init------------------------------------
	LogInfo("GPIO Device reset");
	gpioChip = gpiod_chip_open(cfg.gpioDev.c_str());
	if (gpioChip) {
		LogMessage("%s opened", cfg.gpioDev.c_str());
	} else {
		LogError("Could not open %s", cfg.gpioDev.c_str());
		return true;
	}
	// create the two control lines!
	reqBoot0 = gpioLineRequest(cfg.boot0, 0, g_Gate.GetName());
	reqNrst  = gpioLineRequest(cfg.nrst,  0, g_Gate.GetName());
	if (reqBoot0 and reqNrst)
	{
		if (gpioSetValue(cfg.boot0, 0) or gpioSetValue(cfg.nrst, 0))
		{
			return true;
		} else {
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
			if (gpioSetValue(cfg.nrst, 1))
				return true;
			else
				// wait for device bootup
				std::this_thread::sleep_for(std::chrono::seconds(1));
		}
	} else
		return true;

	//-----------------------------------device part-----------------------------------
	fd = open(cfg.uartDev.c_str(), O_RDWR | O_NOCTTY | O_SYNC);
	if(fd < 0)
	{
		LogError("Could not open %s: %s", cfg.uartDev.c_str(), strerror(errno));
		return true;
	}
	
	if (setIinterface(cfg.baudRate, 0))
		return true;

	//PING-PONG test
	LogInfo("Sending %s a 'PING'", cfg.uartDev.c_str());
	if (testPING())
		return true;

	//config the device
	if (setRxFreq(cfg.rxFreq)
	or  setTxFreq(cfg.txFreq)
	or  setFreqCorr(cfg.freqCorr)
	or  setTxPower(cfg.power)
	or  setAFC(cfg.afc))
		return true;

	keep_running = true;
	rxFuture = std::async(std::launch::async, &CCC1200::rxProcess, this);
	if (not rxFuture.valid())
	{
		LogError("Could not start CCC1200::rxProcess thread");
		return true;
	}
	txFuture = std::async(std::launch::async, &CCC1200::txProcess, this);
	if (not txFuture.valid())
	{
		LogError("Could not start CCC1200::txProcess thread");
		return true;
	}

	return false;
}

void CCC1200::Stop()
{
	LogInfo("Shutting down the CC1200...");
	keep_running = false;

	// send any command to the modem so it will output something and break the select() block
	uint8_t cmd[3] = { CMD_PING, 3, 0 };
	writeDev(cmd, 3, "SHUTDOWN");
	// send a packet to the modem to break the Pop() block
	auto p = std::make_unique<CPacket>(); // an unintialized packet will be fine
	Gate2Modem.Push(p);

	if (rxFuture.valid())
		rxFuture.get();
	if (txFuture.valid())
		txFuture.get();
	LogDebug("tx/tx threads are closed...");
	if (reqBoot0)
	{
		gpioSetValue(cfg.boot0, 0);
		gpiod_line_request_release(reqBoot0);
	}
	if (reqNrst)
	{
		gpioSetValue(cfg.nrst, 0);
		gpiod_line_request_release(reqNrst);
	}
	LogDebug("GPIO lines set to low...");
	if (gpioChip)
		gpiod_chip_close(gpioChip);
	LogDebug("GPIO chip closed...");
	if (fd >= 0)
		close(fd);
	LogInfo("All resources released");
}

void CCC1200::rxProcess()
{
	ERxState rx_state = ERxState::idle;
	int8_t flt_buff[8*5+1];						//length of this has to match RRC filter's length
	float f_flt_buff[8*5+2*(8*5+4800/25*5)+2];	//8 preamble symbols, 8 for the syncword, and 960 for the payload.
                                                //floor(sps/2)=2 extra samples for timing error correction
	uint16_t sample_cnt = 0;					//sample counter (for RX sync timeout)
	uint16_t fn, last_fn=0xffffu;				//current and last received FN (stream mode)
	uint16_t sid;								// stream ID
	uint8_t payload[825];						// buffer for the packet payload
	uint8_t *ppayload = payload;				// ptr to the payload
	uint16_t plsize = 0;             		    // # of bytes in the payload

	bool first_frame = true;					//first decoded frame after SYNC?
	uint8_t lich_parts = 0;						//LICH chunks received (bit flags)
	bool got_lsf = false;						//got LSF? either from LSF or reconstructed from LICH
	uint8_t rx_samp_buff[963];
	int8_t raw_bsb_rx[960];
	uint16_t rx_buff_cnt = 0;
	bool uart_rx_sync = false;
	bool uart_rx_data_valid = false;
	const int8_t lsf_sync_ext[16] { 3, -3, 3, -3, 3, -3, 3, -3, 3, -3, 3, -3, 3, -3, 3, -3 };

	//start RX
	while (stopTx())
		std::this_thread::sleep_for(std::chrono::milliseconds(40));
	while (startRx())
		std::this_thread::sleep_for(std::chrono::milliseconds(40));
	LogInfo("Receiver Started");

	//UART comms
	uint8_t rx_bsb_sample = 0;

	float f_sample;

	while(keep_running)
	{
		fd_set rfds;
		FD_ZERO(&rfds);
		FD_SET(fd, &rfds);

		select(fd+1, &rfds, NULL, NULL, NULL);

		//are there any new baseband samples to process?
		if (!uart_lock && FD_ISSET(fd, &rfds))
		{
			read(fd, &rx_bsb_sample, 1);

			//wait for rx baseband data header
			if (!uart_rx_sync)
			{
				rx_samp_buff[0] = rx_samp_buff[1];
				rx_samp_buff[1] = rx_samp_buff[2];
				rx_samp_buff[2] = rx_bsb_sample;

				if (rx_samp_buff[0]==CMD_RX_DATA && rx_samp_buff[1]==0xC3 && rx_samp_buff[2]==0x03)
				{
					uart_rx_sync = true;
					rx_buff_cnt = 3;
				}
			}
			else
			{
				rx_samp_buff[rx_buff_cnt++] = rx_bsb_sample;
			}

			if (uart_rx_sync && rx_buff_cnt==963)
			{
				//LogDebug("Baseband packet received");
				memcpy(raw_bsb_rx, &rx_samp_buff[3], sizeof(raw_bsb_rx));
				memset(rx_samp_buff, 0, sizeof(rx_samp_buff));
				uart_rx_data_valid = true;
				uart_rx_sync = false;
				rx_buff_cnt = 0;
			}

			if (rx_buff_cnt > 963)
				LogWarning("Input buffer overflow");
		}

		if (uart_rx_data_valid)
		{
			for (uint16_t i=0; i<960; i++)
			{
				//push buffer TODO: please optimize this. eyes hurt
				for(uint8_t i=0; i<sizeof(flt_buff)-1; i++)
					flt_buff[i] = flt_buff[i+1];
				flt_buff[sizeof(flt_buff)-1] = raw_bsb_rx[i];

				f_sample=0.0f;
				for(uint8_t i=0; i<sizeof(flt_buff); i++)
					f_sample+=rrc_taps_5[i]*(float)flt_buff[i];
				f_sample*=RX_SYMBOL_SCALING_COEFF; //symbol map (works for CC1200 only)

				for(uint16_t i=0; i<sizeof(f_flt_buff)/sizeof(float)-1; i++)
					f_flt_buff[i]=f_flt_buff[i+1];
				f_flt_buff[sizeof(f_flt_buff)/sizeof(float)-1]=f_sample;

				//L2 norm check against syncword
				float symbols[16];
				for(uint8_t i=0; i<16; i++)
					symbols[i]=f_flt_buff[i*5];

				float dist_lsf=eucl_norm(&symbols[0], lsf_sync_ext, 16); //check against extended LSF syncword (8 symbols, alternating -3/+3)
				float dist_pkt=eucl_norm(&symbols[0], pkt_sync_symbols, 8);
				float dist_str_a=eucl_norm(&symbols[8], str_sync_symbols, 8);
				for(uint8_t i=0; i<16; i++)
					symbols[i]=f_flt_buff[960+i*5];
				float dist_str_b=eucl_norm(&symbols[8], str_sync_symbols, 8);
				float dist_str=sqrtf(dist_str_a*dist_str_a+dist_str_b*dist_str_b);

				//fwrite(&dist_str, 4, 1, fp);

				//LSF received at idle state
				if(dist_lsf <= 4.5f and rx_state == ERxState::idle)
				{
					//find L2's minimum
					uint8_t sample_offset=0;
					for(uint8_t i=1; i<=2; i++)
					{
						for(uint8_t j=0; j<16; j++)
							symbols[j]=f_flt_buff[j*5+i];

						float d=eucl_norm(symbols, lsf_sync_ext, 16);

						if(d<dist_lsf)
						{
							dist_lsf=d;
							sample_offset=i;
						}
					}

					float pld[SYM_PER_PLD];

					for(uint16_t i=0; i<SYM_PER_PLD; i++)
					{
						pld[i]=f_flt_buff[16*5+i*5+sample_offset]; //add symbol timing correction
					}

					uint32_t e = decode_LSF((lsf_t *)lsf.GetData(), pld);

					const CCallsign dst(lsf.GetCDstAddress());
					const CCallsign src(lsf.GetCSrcAddress());
					frameTYPE.SetFrameType(lsf.GetFrameType());

					if(lsf.CheckCRC()) {
						LogWarning("RF LSF failed CRC check");
					} else {
						got_lsf = true;
						rx_state = ERxState::sync;	//change RX state
						sample_cnt=0;		//reset rx timeout timer

						last_fn=0xFFFFU;

						LogInfo("RF LSF DST: %s SRC: %s TYPE: 0x%04x CAN=%u MER: %-3.1f%%", dst.c_str(), src.c_str(), frameTYPE.GetOriginType(), unsigned(frameTYPE.GetCan()), (float)e/0xFFFFU/SYM_PER_PLD/2.0f*100.0f);

						if(EPayloadType::packet != frameTYPE.GetPayloadType()) //if stream
						{
							sid = g_RNG.Get();

							LogDash("\"%s\" \"%s\" \"RF\" \"%u\" \"%3.1f%%\"", src.c_str(), dst.c_str(), unsigned(frameTYPE.GetCan()), (float)e/0xFFFFU/SYM_PER_PLD/2.0f*100.0f);
						}
						g_GateState.TryState(EGateState::modemin);
					}
				}

				//stream frame received
				else if(dist_str <= 5.0f)
				{
					rx_state = ERxState::sync;
					sample_cnt=0;		//reset rx timeout timer

					//find L2's minimum
					uint8_t sample_offset=0;
					for(uint8_t i=1; i<=2; i++)
					{
						for(uint8_t j=0; j<16; j++)
							symbols[j]=f_flt_buff[j*5+i];
						
						float tmp_a=eucl_norm(&symbols[8], str_sync_symbols, 8);
						for(uint8_t j=0; j<16; j++)
							symbols[j]=f_flt_buff[960+j*5+i];
						
						float tmp_b=eucl_norm(&symbols[8], str_sync_symbols, 8);

						float d=sqrtf(tmp_a*tmp_a+tmp_b*tmp_b);

						if(d<dist_str)
						{
							dist_str=d;
							sample_offset=i;
						}
					}

					float pld[SYM_PER_PLD];
					
					for(uint16_t i=0; i<SYM_PER_PLD; i++)
					{
						pld[i]=f_flt_buff[16*5+i*5+sample_offset];
					}

					uint8_t lich[6];
					uint8_t lich_cnt;
					uint8_t frame_data[16];
					uint32_t e = decode_str_frame(frame_data, lich, &fn, &lich_cnt, pld);
					if (got_lsf)
					{
						frameTYPE.SetFrameType(lsf.GetFrameType());
						if (EPayloadType::packet != frameTYPE.GetPayloadType())
						{
							if (g_GateState.TryState(EGateState::modemin))
							{
								auto p = std::make_unique<CPacket>();
								p->Initialize(EPacketType::stream);
								p->SetStreamId(sid);
								memcpy(p->GetDstAddress(), lsf.GetCData(), 28);
								p->SetFrameNumber(fn);
								memcpy(p->GetPayload(), frame_data, 16);
								p->CalcCRC();
								Modem2Gate.Push(p);
								LogDebug("RF Frame: FN:0x%04x | LICH_CNT:%u | MER: %-3.1f%%", fn, lich_cnt, float(e)/0xffffu/SYM_PER_PLD/2.0f*100.0f);
							}
						}
					}
					
					//set the last FN number to FN-1 if this is a late-join and the frame data is valid
					if(first_frame && (fn%6)==lich_cnt)
					{
						last_fn=fn-1;
					}
					
					if(((last_fn+1)&0xFFFFU)==fn) //new frame. TODO: maybe a timeout would be better
					{
						if(lich_parts!=0x3FU) //6 chunks = 0b111111
						{
							//reconstruct LSF chunk by chunk
							memcpy(lich_lsf.GetData()+(5*lich_cnt), lich, 5); //40 bits
							lich_parts |= (1<<lich_cnt);
							if(lich_parts==0x3FU)
							{
								// we have a complete LICH LSF
								lich_parts = 0; // clear this so it can rebuild again
								if(lich_lsf.CheckCRC()) {
									LogWarning("Lich LSF CRC check failed");
								} else {
									// the LICH LSF has a correct CRC
									frameTYPE.SetFrameType(lich_lsf.GetFrameType());
									if (EPayloadType::packet == frameTYPE.GetPayloadType()) {
										LogWarning("Lich LSF says it's a PM LSF");
									} else {
										// everything is good, copy the lich LSF to lsf
										memcpy(lsf.GetData(), lich_lsf.GetCData(), 30);
										if (got_lsf) {
											LogDebug("LICH LSF TYPE: 0x%04x", frameTYPE.GetOriginType());
										} else {
											got_lsf = true;
											sid = g_RNG.Get();
											const CCallsign dst(lsf.GetCDstAddress());
											const CCallsign src(lsf.GetCSrcAddress());
											LogInfo("Lich LSF DST: %s SRC: %s TYPE: 0x%04x CAN: %u", dst.c_str(), src.c_str(), frameTYPE.GetOriginType(), unsigned(frameTYPE.GetCan()));
											LogDash("\"%s\" \"%s\" \"RF\" \"%u\" \"--\"\n", src.c_str(), dst.c_str(), unsigned(frameTYPE.GetCan()));
										}
									}
								}
							}
						}
						last_fn=fn;
					}
					first_frame = false;
				}

				//TODO: handle packet mode reception over RF
				else if(dist_pkt <= 5.0f and rx_state == ERxState::sync)
				{
					//find L2's minimum
					uint8_t sample_offset=0;
					for(uint8_t i=1; i<=2; i++)
					{
						for(uint8_t j=0; j<8; j++)
							symbols[j]=f_flt_buff[j*5+i];
							
						float d=eucl_norm(symbols, pkt_sync_symbols, 8);
						
						if(d<dist_pkt)
						{
							dist_pkt=d;
							sample_offset=i;
						}
					}

					float pld[SYM_PER_PLD];
					uint8_t eof = 0, pkt_fn = 0;
					
					for(uint16_t i=0; i<SYM_PER_PLD; i++)
					{
						pld[i]=f_flt_buff[8*5+i*5+sample_offset];
					}

					//debug data dump
					//fwrite((uint8_t*)&f_flt_buff[sample_offset], SYM_PER_FRA*5*sizeof(float), 1, fp);

					/*uint32_t e = */decode_pkt_frame(ppayload, &eof, &pkt_fn, pld);
					plsize += eof ? pkt_fn : 25;
					ppayload += 25;
					sample_cnt = 0;

					if(eof)
					{
						if (plsize < 825u)
							memset(payload+plsize, 0, 825u-plsize);
						if (g_Crc.CheckCRC(payload, plsize))
						{
							LogWarning("RF PKT: Payload CRC failed");
						} else {
							if (got_lsf)
							{
								if (g_GateState.TryState(EGateState::modemin)) {
									auto pkt = std::make_unique<CPacket>();
									pkt->Initialize(EPacketType::packet, plsize+34);
									memcpy(pkt->GetData()+4, lsf.GetCData(), 30);
									memcpy(pkt->GetData()+34, payload, plsize);
									Modem2Gate.Push(pkt);
								} else {
									LogWarning("Could not lock the gateway, PM data not sent");
								}
							} else {
								LogWarning("Got a Packet Payload, but not the LSF!");
							}
							if (0x5u == *payload and 0u == payload[plsize-3])
								LogInfo("RF SMS Msg: %s", (char *)(payload+1));
						}
					}
				}
				
				//RX sync timeout
				if(rx_state == ERxState::sync)
				{
					if(++sample_cnt==960*2)
					{
						rx_state = ERxState::idle;
						sample_cnt=0;
						first_frame = true;
						last_fn=0xFFFFU; //TODO: there's a small chance that this will cause problems (it's a valid frame number)
						ppayload = payload;
						lich_parts=0;
						plsize = 0;
						got_lsf = false;
					}
				}
			}

			//all data has been used
			uart_rx_data_valid = false;
		}
	}
}

void CCC1200::txProcess()
{
	ETxState tx_state = ETxState::idle;
	uint32_t tx_timer = 0;

	while (keep_running)
	{
		//receive a packet
		auto pack = Gate2Modem.PopWait();
		if (pack)
		{
			if(EPacketType::stream == pack->GetType())
			{
				tx_timer = getMilliseconds();

				int8_t frame_symbols[SYM_PER_FRA];					//raw frame symbols
				int8_t bsb_samples[963] = { CMD_TX_DATA, -61, 3 };	//baseband samples wrapped in a frame

				if(tx_state == ETxState::idle) //first received frame
				{
					tx_state = ETxState::active;

					//TODO: this needs to happen every time a new transmission appears
					//dev_stop_rx();
					//dbg_print(0, "RX stop\n");
					std::this_thread::sleep_for(std::chrono::milliseconds(10));

					//set TYPE field
					CFrameType type(pack->GetFrameType());
					type.SetMetaDataType(EMetaDatType::ecd);
					pack->SetFrameType(type.GetFrameType(cfg.isV3 ? EVersionType::v3 : EVersionType::legacy));

					//generate META field
					cfg.callSign.CodeOut(pack->GetMetaData());
					g_Gate.GetLink().CodeOut(pack->GetMetaData()+6);
					memset(pack->GetMetaData()+12, 0, 2);

					memcpy(lsf.GetData(), pack->GetCDstAddress(), 28);
					lsf.CalcCRC();

					//append CRC
					pack->CalcCRC();
					const CCallsign dst(pack->GetCDstAddress());
					const CCallsign src(pack->GetCSrcAddress());

					//log to file
					LogDash("\"%s\" \"%s\" \"Internet\" \"--\" \"--\"", src.c_str(), dst.c_str());

					LogInfo("Stream TX start");

					//stop RX, set PA_EN=1 and initialize TX
					while (stopRx())
						std::this_thread::sleep_for(std::chrono::milliseconds(40));
					std::this_thread::sleep_for(std::chrono::milliseconds(20));

					while (startTx())
						std::this_thread::sleep_for(std::chrono::milliseconds(40));
					std::this_thread::sleep_for(std::chrono::milliseconds(10));

					//flush the RRC baseband filter
					filterSymbols(nullptr, nullptr, nullptr, 0);
				
					//generate frame symbols, filter them and send out to the device
					//we need to prepare 3 frames to begin the transmission - preamble, LSF and stream frame 0
					//let's start with the preamble
					uint32_t frame_buff_cnt=0;
					gen_preamble_i8(frame_symbols, &frame_buff_cnt, PREAM_LSF);

					//filter and send out to the device
					filterSymbols(bsb_samples+3, frame_symbols, rrc_taps_5_poly, 0);
					writeDev(bsb_samples, sizeof(bsb_samples), "SM LSF preamble");

					//now the LSF
					gen_frame_i8(frame_symbols, nullptr, FRAME_LSF, (lsf_t *)(lsf.GetCData()), 0, 0);

					//filter and send out to the device
					filterSymbols(bsb_samples+3, frame_symbols, rrc_taps_5_poly, 0);
					writeDev(bsb_samples, sizeof(bsb_samples), "SM LSF");

					//finally, the first frame
					gen_frame_i8(frame_symbols, pack->GetCPayload(), FRAME_STR, (lsf_t *)(lsf.GetCData()), (pack->GetFrameNumber()&0x7fffu)%6, pack->GetFrameNumber());

					//filter and send out to the device
					filterSymbols(bsb_samples+3, frame_symbols, rrc_taps_5_poly, 0);
					writeDev(bsb_samples, sizeof(bsb_samples), "SM First Frame");
				}
				else
				{
					auto fn = pack->GetFrameNumber();
					uint8_t lich_cnt = (fn & 0x7fffu) % 6;
					if (0 == lich_cnt)
					{
						memcpy(lsf.GetData(), pack->GetCDstAddress(), 28);
						lsf.CalcCRC();
					}
					//only one frame is needed
					gen_frame_i8(frame_symbols, pack->GetCPayload(), FRAME_STR, (lsf_t *)(lsf.GetCData()), lich_cnt, fn);

					//filter and send out to the device
					filterSymbols(bsb_samples+3, frame_symbols, rrc_taps_5_poly, 0);
					writeDev(bsb_samples, sizeof(bsb_samples), "SM Frame");
				}
				//LogDebug("Stream packet: FN=0x%04x", pack->GetFrameNumber());
				if(pack->IsLastPacket()) //last stream frame
				{
					//send the final EOT marker
					uint32_t frame_buff_cnt=0;
					gen_eot_i8(frame_symbols, &frame_buff_cnt);

					//filter and send out to the device
					filterSymbols(bsb_samples+3, frame_symbols, rrc_taps_5_poly, 0);
					writeDev(bsb_samples, sizeof(bsb_samples), "SM EOT");

					LogInfo("Stream TX end");
					//wait 320ms (8 M17 frames) - let the transmitter consume all the buffered samples
					std::this_thread::sleep_for(std::chrono::milliseconds(320));

					//restart RX
					while (stopTx())
						std::this_thread::sleep_for(std::chrono::milliseconds(40));
					while (startRx())
						std::this_thread::sleep_for(std::chrono::milliseconds(40));
					LogInfo("RX start");

					tx_state = ETxState::idle;
					g_GateState.Set2IdleIf(EGateState::gatestreamin);
				}
			}

			//M17 packet data - "Packet Mode IP Packet"
			else if(EPacketType::packet == pack->GetType())
			{
				LogInfo("M17 Inet PM received");

				const CCallsign dst(pack->GetCDstAddress());
				const CCallsign src(pack->GetCSrcAddress());
				CFrameType type(pack->GetFrameType());
				
				LogInfo(" ├ DST: %s", dst.c_str());
				LogInfo(" ├ SRC: %s", src.c_str());
				LogInfo(" ├ CAN: %u", type.GetCan());
				if(*pack->GetCPayload() != 0x5u) //assuming 1-byte type specifier
				{
					LogInfo(" ├ TYPE: %u", *pack->GetCPayload());
					LogInfo(" └ SIZE: %u", pack->GetSize()-34u);
				}
				else
				{
					auto pnull = pack->GetPayload()+pack->GetSize()-3u;
					LogInfo(" ├ TYPE: SMS");
					if (*pnull) //this should be a null byte
					{
						LogWarning(" ├ SMS msg not properly terminated!");
						pnull = 0;
						pack->CalcCRC();
					}
					LogInfo(" └ MSG: %s", (char *)pack->GetCPayload());
				}

				//TODO: handle TX here
				int8_t frame_symbols[SYM_PER_FRA];						//raw frame symbols
				int8_t bsb_samples[963] = {CMD_TX_DATA, -61, 3 };		//baseband samples wrapped in a frame

				//log to file
				LogDash("\"%s\" \"%s\" \"Internet\" \"--\" \"--\"", src.c_str(), dst.c_str());
				
				LogInfo("Packet TX start");

				//stop RX, set PA_EN=1 and initialize TX
				while (stopRx())
					std::this_thread::sleep_for(std::chrono::milliseconds(40));
				std::this_thread::sleep_for(std::chrono::milliseconds(2));
				while (startTx())
					std::this_thread::sleep_for(std::chrono::milliseconds(40));
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
				
				//flush the RRC baseband filter
				filterSymbols(nullptr, nullptr, nullptr, 0);
				
				//generate frame symbols, filter them and send out to the device
				//we need to prepare 3 frames to begin the transmission - preamble, LSF and stream frame 0
				//let's start with the preamble
				uint32_t frame_buff_cnt=0;
				gen_preamble_i8(frame_symbols, &frame_buff_cnt, PREAM_LSF);
				
				//filter and send out to the device
				filterSymbols(bsb_samples+3, frame_symbols, rrc_taps_5_poly, 0);
				writeDev(bsb_samples, sizeof(bsb_samples), "PM Preamble");
				
				//now the LSF
				gen_frame_i8(frame_symbols, nullptr, FRAME_LSF, (lsf_t*)(pack->GetCDstAddress()), 0, 0);
				
				//filter and send out to the device
				filterSymbols(bsb_samples+3, frame_symbols, rrc_taps_5_poly, 0);
				writeDev(bsb_samples, sizeof(bsb_samples), "PM LSF");
				
				//packet frames
				uint16_t pld_len = pack->GetSize() - 34u; //"M17P" plus 240-bit LSD
				uint8_t frame = 0;
				uint8_t pld[26];
				
				while(pld_len > 25)
				{
					memcpy(pld, pack->GetCPayload()+frame*25, 25);
					pld[25] = frame<<2;
					gen_frame_i8(frame_symbols, pld, FRAME_PKT, nullptr, 0, 0);
					filterSymbols(bsb_samples+3, frame_symbols, rrc_taps_5_poly, 0);
					writeDev(bsb_samples, sizeof(bsb_samples), "PM Frame");
					pld_len -= 25;
					frame++;
					std::this_thread::sleep_for(std::chrono::milliseconds(40));
				}
				memset(pld, 0, 26);
				memcpy(pld, pack->GetCPayload()+frame*25, pld_len);
				pld[25] = (1<<7) | (pld_len<<2); //EoT flag set, amount of remaining data in the 'frame number' field
				gen_frame_i8(frame_symbols, pld, FRAME_PKT, nullptr, 0, 0);
				filterSymbols(bsb_samples+3, frame_symbols, rrc_taps_5_poly, 0);
				writeDev(bsb_samples, sizeof(bsb_samples), "PM Final Frame");
				std::this_thread::sleep_for(std::chrono::milliseconds(40));

				//now the final EOT marker
				frame_buff_cnt=0;
				gen_eot_i8(frame_symbols, &frame_buff_cnt);

				//filter and send out to the device
				filterSymbols(bsb_samples+3, frame_symbols, rrc_taps_5_poly, 0);
				writeDev(bsb_samples, sizeof(bsb_samples), "PM EOT");

				LogInfo("PKT TX end");
				std::this_thread::sleep_for(std::chrono::milliseconds(120)); //wait 120ms (3 M17 frames)

				//restart RX
				while (stopTx())
					std::this_thread::sleep_for(std::chrono::milliseconds(40));
				while (startRx())
					std::this_thread::sleep_for(std::chrono::microseconds(40));
				LogInfo("RX start");

				tx_state = ETxState::idle;
				g_GateState.Set2IdleIf(EGateState::gatepacketin);
			}
		}

		//tx timeout
		if(tx_state == ETxState::active and (getMilliseconds()-tx_timer) > 240) //240ms timeout
		{
			LogInfo("TX timeout");
			
			//restart RX
			while (stopTx())
				std::this_thread::sleep_for(std::chrono::milliseconds(40));
			while (startRx())
				std::this_thread::sleep_for(std::chrono::milliseconds(40));
			LogInfo("RX start");

			tx_state = ETxState::idle;
			g_GateState.Set2IdleIf(EGateState::gatestreamin);
			g_GateState.Set2IdleIf(EGateState::gatepacketin);
		}
	}
	LogInfo("Modem run() process end");
}


