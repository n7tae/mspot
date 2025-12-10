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

#include "Callsign.h"
#include "Gateway.h"
#include "CC1200.h"
#include "LSF.h"
#include "Log.h"

extern CGateway g_Gate;
extern CCRC     g_Crc;

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

static uint8_t tx_buff[512] = { 0 };
static uint8_t rx_buff[65536] = { 0 };
static int tx_len = 0, rx_len = 0;

//device stuff
uint8_t cmd[8];

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

static const char *err_str[9] { "Okay", "TRX PLL", "TRX SPI", "Range", "Command Malformed", "Busy", "Buffer Full", "NOP", "Other" };

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

static int8_t flt_buff[8*5+1];						//length of this has to match RRC filter's length
static float f_flt_buff[8*5+2*(8*5+4800/25*5)+2];	//8 preamble symbols, 8 for the syncword, and 960 for the payload.
													//floor(sps/2)=2 extra samples for timing error correction

static uint8_t rx_samp_buff[1024];
static int8_t raw_bsb_rx[960];
static uint16_t rx_buff_cnt = 0;
static bool uart_rx_sync = false;
static bool uart_rx_data_valid = false;

static ETxState tx_state = ETxState::idle;
static ERxState rx_state = ERxState::idle;

static int8_t lsf_sync_ext[16];				//extended LSF syncword
static uint16_t sample_cnt = 0;				//sample counter (for RX sync timeout)
static uint16_t fn, last_fn=0xffffu;		//current and last received FN (stream mode)
static uint8_t pkt_fn, last_pkt_fn = 0xff;	//current and last received FN (packet mode)
static uint8_t lsf_b[30];					//raw decoded LSF
static bool first_frame = true;				//first decoded frame after SYNC?
static uint8_t lich_parts = 0;				//LICH chunks received (bit flags)
static bool got_lsf = false;				//got LSF? either from LSF or reconstructed from LICH

//timer for timeouts
static uint32_t tx_timer = 0;

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

//UART magic
static int fd; //UART handle

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
	cfg.baudRate = g_Cfg.GetUnsigned(g_Keys.modem.section,    g_Keys.modem.uartSpeed);
	cfg.rxFreq   = g_Cfg.GetUnsigned(g_Keys.modem.section,    g_Keys.modem.rxFreq);
	cfg.txFreq   = g_Cfg.GetUnsigned(g_Keys.modem.section,    g_Keys.modem.txFreq);
	cfg.afc      = g_Cfg.GetBoolean( g_Keys.modem.section,    g_Keys.modem.afc);
	cfg.freqCorr = g_Cfg.GetInt(     g_Keys.modem.section,    g_Keys.modem.freqCorr);
	cfg.power    = g_Cfg.GetFloat(   g_Keys.modem.section,    g_Keys.modem.txPower);
	cfg.boot0    = g_Cfg.GetUnsigned(g_Keys.modem.section,    g_Keys.modem.boot0);
	cfg.nrst     = g_Cfg.GetUnsigned(g_Keys.modem.section,    g_Keys.modem.nrst);
	cfg.can      = g_Cfg.GetUnsigned(g_Keys.repeater.section, g_Keys.repeater.can);
	cfg.debug    = g_Cfg.GetBoolean( g_Keys.repeater.section, g_Keys.repeater.debug);
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

bool CCC1200::readDev(void *buf, int size)
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

	uartLock = true;          //prevent main loop from reading
    tcflush(fd, TCIFLUSH);    //clear leftover bytes

    writeDev(cmd, 3, "testPING");

	if (readDev(resp, 7))
	{
		uartLock = false;
		return true;
	}

    uartLock = false;

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

	uartLock = true;            //prevent main loop from reading
    tcflush(fd, TCIFLUSH);    //clear leftover bytes

    writeDev(cmd, 7, "setRXFreq");

	if (readDev(resp, 4))
	{
		uartLock = false;
		return true;
	}

    uartLock = false;

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

	uartLock = true;            //prevent main loop from reading
    tcflush(fd, TCIFLUSH);    //clear leftover bytes

	writeDev(cmd, 7, "setTxFreq");
	if (readDev(resp, 4))
	{
		uartLock = false;
		return true;
	}

	uartLock = false;

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
	uint8_t cmd[5] = {cid, 5, 0, corr&0xff, (corr>>8)&0xff};
	uint8_t resp[4] = { 0 };

	uartLock = true;            //prevent main loop from reading
    tcflush(fd, TCIFLUSH);    //clear leftover bytes

    writeDev(cmd, 5, "setFreqCorr");

    if (readDev(resp, 4))
	{
		uartLock = false;
		return true;
	}

    uartLock = false;

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
	uint8_t cmd[4] = { cid, 4, 0, en?1:0 };
	uint8_t resp[4] = { 0 };

	uartLock;            //prevent main loop from reading
    tcflush(fd, TCIFLUSH);    //clear leftover bytes

    writeDev(cmd, 4, "setAFC");

	if (readDev(resp, 4))
	{
		uartLock = false;
		return true;
	}

    uartLock = false;

	const char goodResp[] { cid, 4, 0, 0 };

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
	uint8_t cmd[4] = {cid, 4, 0, roundf(power*4.0f)};
	uint8_t resp[4] = { 0 };

	uartLock = true;		//prevent main loop from reading
    tcflush(fd, TCIFLUSH);	//clear leftover bytes

    writeDev(cmd, 4, "setTxPower");

	if (readDev(resp, 4))
	{
		uartLock = false;
		return true;
	}

    uartLock = false;

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

    uartLock = true;          //prevent main loop from reading
    tcflush(fd, TCIFLUSH);    //clear leftover bytes

    writeDev(cmd, 4, what);

	if (readDev(resp, 3))
	{
		uartLock = false;
		return true;
	}

    uartLock = false;

	if (cid != resp[0] or 4u != resp[1] or 0 != resp[2] or (ERR_OK != resp[3] and ERR_NOP != resp[3]))
	{
        LogError("Error doing %s, cmd returned %02x %02x %02x %02x", what, resp[0], resp[1], resp[2], resp[3]);
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

//samples per symbol (sps) = 5
void CCC1200::filterSymbols(int8_t out[SYM_PER_FRA*5], const int8_t in[SYM_PER_FRA], const float* flt, uint8_t phase_inv)
{
	#define FLT_LEN 41
	static int8_t last[FLT_LEN]; //memory for last symbols

	if(out!=NULL)
	{
		for(uint8_t i=0; i<SYM_PER_FRA; i++)
		{
			for(uint8_t j=0; j<5; j++)
			{
				for(uint8_t k=0; k<FLT_LEN-1; k++)
					last[k]=last[k+1];

				if(j==0)
				{
					if(phase_inv) //optional phase inversion
						last[FLT_LEN-1]=-in[i];
					else
						last[FLT_LEN-1]= in[i];
				}
				else
					last[FLT_LEN-1]=0;

				float acc=0.0f;
				for(uint8_t k=0; k<FLT_LEN; k++)
					acc+=last[k]*flt[k];

				out[i*5+j]=acc*TX_SYMBOL_SCALING_COEFF*sqrtf(5.0f); //crank up the gain
			}
		}
	}
	else
	{
		for(uint8_t i=0; i<FLT_LEN; i++)
			last[i]=0;
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
	LogInfo("Sending %s a 'PING", cfg.uartDev.c_str());
	if (testPING())
		return true;

	//config the device
	if (setRxFreq(cfg.rxFreq)
	or  setTxFreq(cfg.txFreq)
	or  setFreqCorr(cfg.freqCorr)
	or  setTxPower(cfg.power)
	or  setAFC(cfg.afc))
		return true;

	return false;
}

void CCC1200::Stop()
{
	LogInfo("Shutting down the CC1200");
	keep_running = false;
	runFuture.get();
	if (fd >= 0)
		close(fd);
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
	if (gpioChip)
		gpiod_chip_close(gpioChip);
	LogInfo("All resources released");
}

void CCC1200::run()
{
	//extend the LSF syncword pattern with 8 symbols from the preamble
	lsf_sync_ext[0]=3; lsf_sync_ext[1]=-3; lsf_sync_ext[2]=3; lsf_sync_ext[3]=-3;
	lsf_sync_ext[4]=3; lsf_sync_ext[5]=-3; lsf_sync_ext[6]=3; lsf_sync_ext[7]=-3;
	memcpy(&lsf_sync_ext[8], lsf_sync_symbols, 8);

	//start RX
	while (stopTx())
		std::this_thread::sleep_for(std::chrono::milliseconds(40));
	while (startRx())
		std::this_thread::sleep_for(std::chrono::milliseconds(40));
	LogInfo("Receiver Started");

	//UART comms
	int8_t rx_bsb_sample = 0;

	float f_sample;

	keep_running = true;

	while(keep_running)
	{
		fd_set rfds;
		FD_ZERO(&rfds);
		FD_SET(fd, &rfds);

		select(fd+1, &rfds, NULL, NULL, NULL);

		//are there any new baseband samples to process?
		if (keep_running and not uartLock and FD_ISSET(fd, &rfds))
		{
			read(fd, (uint8_t*)&rx_bsb_sample, 1);

			//wait for rx baseband data header
			if (uart_rx_sync)
			{
				rx_samp_buff[rx_buff_cnt] = rx_bsb_sample;
				rx_buff_cnt++;
			}
			else
			{
				rx_samp_buff[0] = rx_samp_buff[1];
				rx_samp_buff[1] = rx_samp_buff[2];
				rx_samp_buff[2] = rx_bsb_sample;

				if (rx_samp_buff[0]==CMD_RX_DATA and rx_samp_buff[1]==0xC3 and rx_samp_buff[2]==0x03)
				{
					uart_rx_sync = true;
					rx_buff_cnt = 3;
				}
			}

			if (uart_rx_sync and rx_buff_cnt==963)
			{
				//dbg_print(TERM_YELLOW, "Baseband packet received\n");
				memcpy(raw_bsb_rx, rx_samp_buff+3, sizeof(raw_bsb_rx));
				memset(rx_samp_buff, 0, sizeof(rx_samp_buff));
				uart_rx_data_valid = true;
				uart_rx_sync = false;
				rx_buff_cnt = 0;
			}

			if (rx_buff_cnt > 1024)
				LogError("Input buffer overflow");
		}

		if (uart_rx_data_valid)
		{
			for (uint16_t i=0; i<960; i++)
			{
				//push buffer
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
					symbols[i] = f_flt_buff[i*5];

				float dist_lsf = eucl_norm(&symbols[0], lsf_sync_ext, 16); //check against extended LSF syncword (8 symbols, alternating -3/+3)
				float dist_pkt = eucl_norm(&symbols[0], pkt_sync_symbols, 8);
				float dist_str_a = eucl_norm(&symbols[8], str_sync_symbols, 8);
				for(uint8_t i=0; i<16; i++)
					symbols[i] = f_flt_buff[960+i*5];
				float dist_str_b = eucl_norm(&symbols[8], str_sync_symbols, 8);
				float dist_str = sqrtf(dist_str_a*dist_str_a+dist_str_b*dist_str_b);

				//LSF received at idle state
				if(dist_lsf<=4.5f and rx_state==ERxState::idle)
				{
					//find L2's minimum
					uint8_t sample_offset=0;
					for(uint8_t i=1; i<=2; i++)
					{
						for(uint8_t j=0; j<16; j++)
							symbols[j] = f_flt_buff[j*5+i];

						float d = eucl_norm(symbols, lsf_sync_ext, 16);

						if(d<dist_lsf)
						{
							dist_lsf=d;
							sample_offset=i;
						}
					}

					float pld[SYM_PER_PLD];

					for(uint16_t i=0; i<SYM_PER_PLD; i++)
					{
						pld[i] = f_flt_buff[16*5+i*5+sample_offset]; //add symbol timing correction
					}

					SLSF lsf;
					CFrameType type;
					const CCallsign dst(lsf.GetCDstAddress());
					const CCallsign src(lsf.GetCSrcAddress());
					type.GetFrameType(lsf.GetFrameType());
					const bool crcIsOK = not lsf.CheckCRC();

					if (type.GetPayloadType()!=EPayloadType::packet and type.GetEncryptType()==EEncryptType::none and not lsf.CheckCRC()) // does this SM data work for me?
					{
						got_lsf = true;
						type.SetMetaDataType(EMetaDatType::none);
						sFrame = std::make_unique<SuperFrame>();
						sfCounter = 0u;
						sFrame->superFN = 0u;
						dst.CodeOut(sFrame->lsf.GetDstAddress());
						src.CodeOut(sFrame->lsf.GetSrcAddress());
						rx_state = ERxState::sync;	//change RX state
						sample_cnt = 0;		//reset rx timeout timer

						last_fn=0xFFFFU;

						LogInfo("CRC OK | DST: %-9s | SRC: %-9s | TYPE: %04X (CAN=%d) | MER: %-3.1f%%", dst.c_str(), src.c_str(), ft, can, (float)e/0xFFFFU/SYM_PER_PLD/2.0f*100.0f);

					} else {
						if (not crcIsOK)
							LogInfo("CRC is not valid");
						if (not isSM)
							LogError("Was evaluated as SM by TYPE says it is PM");
					}
				}

				//stream frame received
				else if(dist_str<=5.0f)
				{
					rx_state=RX_SYNCD;
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
					uint8_t frame_data[128/8];
					uint32_t e = decode_str_frame(frame_data, lich, &fn, &lich_cnt, pld);
					
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
							memcpy(&lsf_b[lich_cnt*5], lich, 40/8); //40 bits
							lich_parts|=(1<<lich_cnt);
							if(lich_parts==0x3FU && got_lsf==false) //collected all of them?
							{
								if(!CRC_M17(lsf_b, 30)) //CRC check
								{
									got_lsf = true;
									m17stream.sid=rand()%0x10000U;

									uint8_t call_dst[12]={0}, call_src[12]={0};
									uint16_t type=((uint16_t)lsf_b[12]<<8)|lsf_b[13];
									uint8_t can=(type>>7)&0xF;

									decode_callsign_bytes(call_dst, &lsf_b[0]);
									decode_callsign_bytes(call_src, &lsf_b[6]);

									time(&rawtime);
									timeinfo=localtime(&rawtime);
									dbg_print(TERM_SKYBLUE, "[%02d:%02d:%02d] ",
										timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
									dbg_print(TERM_YELLOW, "LSF REC: DST: %-9s | SRC: %-9s | TYPE: %04X (CAN=%d)\n",
										call_dst, call_src, type, can);

									if(logfile!=NULL)
									{
										time(&rawtime);
										timeinfo=localtime(&rawtime);
										fprintf(logfile, "\"%02d:%02d:%02d\" \"%s\" \"%s\" \"RF\" \"%d\" \"--\"\n",
											timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec,
											call_src, call_dst, can);
									}
								}
								else
								{
									dbg_print(TERM_YELLOW, "LSF CRC ERR\n");
									lich_parts=0; //reset flags
								}
							}
						}

						time(&rawtime);
						timeinfo=localtime(&rawtime);

						dbg_print(TERM_SKYBLUE, "[%02d:%02d:%02d]",
							timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
						dbg_print(TERM_YELLOW, " RF FRM: ");
						dbg_print(TERM_YELLOW, " FN:%04X | LICH_CNT:%d", fn, lich_cnt);
						/*dbg_print(TERM_YELLOW, " | PLD: ");
						for(uint8_t i=0; i<128/8; i++)
							dbg_print(TERM_YELLOW, "%02X", frame_data[2+i]);*/
						dbg_print(TERM_YELLOW, " | MER: %-3.1f%%\n",
							(float)e/0xFFFFU/SYM_PER_PLD/2.0f*100.0f);

						if(got_lsf)
						{
							m17stream.fn=(fn>>8)|((fn&0xFF)<<8);
							uint8_t refl_pld[(32+16+224+16+128+16)/8];					//single frame
							sprintf((char*)&refl_pld[0], "M17 ");						//MAGIC
							*((uint16_t*)&refl_pld[4])=m17stream.sid;					//SID
							memcpy(&refl_pld[6], &lsf_b[0], 224/8);						//LSD
							*((uint16_t*)&refl_pld[34])=m17stream.fn;					//FN
							memcpy(&refl_pld[36], frame_data, 128/8);					//payload
							uint16_t crc_val=CRC_M17(refl_pld, 52);						//CRC
							*((uint16_t*)&refl_pld[52])=(crc_val>>8)|(crc_val<<8);		//endianness swap
							refl_send(refl_pld, sizeof(refl_pld));						//send a single frame to the reflector
						}

						last_fn=fn;
					}

					first_frame = false;
				}

				//TODO: handle packet mode reception over RF
				else if(dist_pkt<=5.0f && rx_state==RX_SYNCD)
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
					uint8_t pkt_frame_data[25] = {0};
					uint8_t eof = 0;
					
					for(uint16_t i=0; i<SYM_PER_PLD; i++)
					{
						pld[i]=f_flt_buff[8*5+i*5+sample_offset];
					}

					//debug data dump
					//fwrite((uint8_t*)&f_flt_buff[sample_offset], SYM_PER_FRA*5*sizeof(float), 1, fp);

					/*uint32_t e = */decode_pkt_frame(pkt_frame_data, &eof, &pkt_fn, pld);

					//TODO: this will only properly decode single-framed packets
					if(last_pkt_fn==0xFF && eof==1 && CRC_M17(pkt_frame_data, strlen((char*)pkt_frame_data)+3)==0)
					{
						sample_cnt=0;		//reset rx timeout timer
						last_pkt_fn=pkt_fn;

						time(&rawtime);
						timeinfo=localtime(&rawtime);

						dbg_print(TERM_SKYBLUE, "[%02d:%02d:%02d]",
							timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
						dbg_print(TERM_YELLOW, " RF PKT: ");
						/*for(uint8_t i=0; i<25; i++)
							dbg_print(0, "%02X ", pkt_frame_data[i]);
						dbg_print(0, "\n");*/
						dbg_print(0, "%s\n", (char*)&pkt_frame_data[1]);
						uint8_t refl_pld[4+sizeof(lsf)+strlen((char*)pkt_frame_data)+3];					//single frame
						sprintf((char*)&refl_pld[0], "M17P");						//MAGIC
						memcpy(&refl_pld[4], &lsf, sizeof(lsf));					//LSF
						memcpy(&refl_pld[34], &pkt_frame_data, strlen((char*)pkt_frame_data)+3); //PKT data + CRC
						/*debug logging
						time(&rawtime);
						timeinfo=localtime(&rawtime);

						dbg_print(TERM_SKYBLUE, "[%02d:%02d:%02d]",
							timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
						dbg_print(TERM_YELLOW, " refl_pld: ");
						for(uint8_t i=0; i<sizeof(refl_pld); i++)
							dbg_print(0, "%02X ", refl_pld[i]);
						dbg_print(0, "\n");
						*/
						refl_send(refl_pld, 4+sizeof(lsf)+strlen((char*)pkt_frame_data)+3);						//send to the reflector
					}
				}
				
				//RX sync timeout
				if(rx_state==RX_SYNCD)
				{
					sample_cnt++;
					if(sample_cnt==960*2)
					{
						rx_state=RX_IDLE;
						sample_cnt=0;
						first_frame = true;
						last_fn=0xFFFFU; //TODO: there's a small chance that this will cause problems (it's a valid frame number)
						last_pkt_fn=0xFF;
						lich_parts=0;
						got_lsf = false;
					}
				}
			}

			//all data has been used
			uart_rx_data_valid = 0;
		}

		//receive a packet - blocking
		if (FD_ISSET(sockt, &rfds))
		{
			rx_len = recvfrom(sockt, rx_buff, MAX_UDP_LEN, 0, (struct sockaddr*)&saddr, (socklen_t*)&saddr_size);

			//debug
			//dbg_print(0, "Size:%d\nPayload:%s\n", rx_len, rx_buff);

			//PING-PONG
			if(strstr((char*)rx_buff, "PING")==(char*)rx_buff)
			{
				last_refl_ping = time(NULL);
				sprintf((char*)tx_buff, "PONGxxxxxx"); //that "xxxxxx" is just a placeholder
				memcpy(&tx_buff[4], config.enc_node, sizeof(config.enc_node));
				refl_send(tx_buff, 4+6); //PONG
				//dbg_print(TERM_YELLOW, "PING\n");
			}

			//M17 stream frame data - "Steaming Mode IP Packet, Single Packet Method"
			else if(strstr((char*)rx_buff, "M17 ")==(char*)rx_buff)
			{
				tx_timer=get_ms();

				m17stream.sid=((uint16_t)rx_buff[4]<<8)|rx_buff[5];
				m17stream.fn=((uint16_t)rx_buff[34]<<8)|rx_buff[35];
				static uint8_t dst_call[10]={0};
				static uint8_t src_call[10]={0};
				memcpy(m17stream.pld, &rx_buff[(32+16+224+16)/8U], 128/8);

				int8_t frame_symbols[SYM_PER_FRA];						//raw frame symbols
				int8_t bsb_samples[SYM_PER_FRA*5];						//filtered baseband samples = symbols*sps
				uint8_t bsb_chunk[963] = {CMD_TX_DATA, 0xC3, 0x03};		//baseband samples wrapped in a frame

				if(tx_state==TX_IDLE) //first received frame
				{
					tx_state=TX_ACTIVE;

					//TODO: this needs to happen every time a new transmission appears
					//dev_stop_rx();
					//dbg_print(0, "RX stop\n");
					usleep(10e3);

					//extract data
					memcpy(m17stream.lsf.dst, "\xFF\xFF\xFF\xFF\xFF\xFF", 6);
					memcpy(m17stream.lsf.src, &rx_buff[6+6], 6);
					decode_callsign_bytes(dst_call, m17stream.lsf.dst);
					decode_callsign_bytes(src_call, m17stream.lsf.src);

					//set TYPE field
					memcpy(m17stream.lsf.type, &rx_buff[18], 2);
					m17stream.lsf.type[1]|=0x2U<<5; //no encryption, so the subtype field defines the META field contents: extended callsign data

					//generate META field
					//remove trailing spaces and suffixes
					uint8_t trimmed_src[12], enc_trimmed_src[6];
					for(uint8_t i=0; i<12; i++)
					{
						if(src_call[i]!=' ')
							trimmed_src[i]=src_call[i];
						else
						{
							trimmed_src[i]=0;
							break;
						}
					}
					encode_callsign_bytes(enc_trimmed_src, trimmed_src);

					uint8_t ext_ref[12], enc_ext_ref[6];
					sprintf((char*)ext_ref, "%s %c", config.reflector, config.module);
					encode_callsign_bytes(enc_ext_ref, ext_ref);

					memcpy(&m17stream.lsf.meta[0], m17stream.lsf.src, 6); //originator
					memcpy(&m17stream.lsf.meta[6], enc_ext_ref, 6); //reflector
					memset(&m17stream.lsf.meta[12], 0, 2);
					memcpy(m17stream.lsf.src, enc_trimmed_src, 6);

					//append CRC
					uint16_t ccrc=LSF_CRC(&m17stream.lsf);
					m17stream.lsf.crc[0]=ccrc>>8;
					m17stream.lsf.crc[1]=ccrc&0xFF;

					//log to file
					if(logfile!=NULL)
					{
						time(&rawtime);
						timeinfo=localtime(&rawtime);
						fprintf(logfile, "\"%02d:%02d:%02d\" \"%s\" \"%s\" \"Internet\" \"--\" \"--\"\n",
							timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec,
							src_call, dst_call);
					}

					time(&rawtime);
					timeinfo=localtime(&rawtime);
					dbg_print(TERM_SKYBLUE, "[%02d:%02d:%02d]",
						timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
					dbg_print(TERM_GREEN, " Stream TX start\n");

					//stop RX, set PA_EN=1 and initialize TX
					while (dev_stop_rx() != 0) usleep(40e3);
					usleep(2e3);
					gpio_set(config.pa_en, 1);
					while (dev_start_tx() != 0) usleep(40e3);
					usleep(10e3);

					//flush the RRC baseband filter
					filterSymbols(NULL, NULL, NULL, 0);
				
					//generate frame symbols, filter them and send out to the device
					//we need to prepare 3 frames to begin the transmission - preamble, LSF and stream frame 0
					//let's start with the preamble
					uint32_t frame_buff_cnt=0;
					gen_preamble_i8(frame_symbols, &frame_buff_cnt, PREAM_LSF);

					//filter and send out to the device
					filterSymbols(bsb_samples, frame_symbols, rrc_taps_5, 0);
					memcpy(&bsb_chunk[3], bsb_samples, sizeof(bsb_samples));
					write(fd, (uint8_t*)bsb_chunk, sizeof(bsb_chunk));

					//now the LSF
					gen_frame_i8(frame_symbols, NULL, FRAME_LSF, &(m17stream.lsf), 0, 0);

					//filter and send out to the device
					filterSymbols(bsb_samples, frame_symbols, rrc_taps_5, 0);
					memcpy(&bsb_chunk[3], bsb_samples, sizeof(bsb_samples));
					write(fd, (uint8_t*)bsb_chunk, sizeof(bsb_chunk));

					//finally, the first frame
					gen_frame_i8(frame_symbols, m17stream.pld, FRAME_STR, &(m17stream.lsf), (m17stream.fn&0x7FFFU)%6, m17stream.fn);

					//filter and send out to the device
					filterSymbols(bsb_samples, frame_symbols, rrc_taps_5, 0);
					memcpy(&bsb_chunk[3], bsb_samples, sizeof(bsb_samples));
					write(fd, (uint8_t*)bsb_chunk, sizeof(bsb_chunk));
				}
				else
				{
					//only one frame is needed
					gen_frame_i8(frame_symbols, m17stream.pld, FRAME_STR, &(m17stream.lsf), (m17stream.fn&0x7FFFU)%6, m17stream.fn);

					//filter and send out to the device
					filterSymbols(bsb_samples, frame_symbols, rrc_taps_5, 0);
					memcpy(&bsb_chunk[3], bsb_samples, sizeof(bsb_samples));
					write(fd, (uint8_t*)bsb_chunk, sizeof(bsb_chunk));
				}

				time(&rawtime);
				timeinfo=localtime(&rawtime);

				/*dbg_print(TERM_YELLOW, "[%02d:%02d:%02d] NET FRM: ",
						timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
				dbg_print(TERM_YELLOW, "SID: %04X | FN: %04X | DST: %-9s | SRC: %-9s | TYPE: %04X | META: ",
						m17stream.sid, m17stream.fn&0x7FFFU, dst_call, src_call, ((uint16_t)m17stream.lsf.type[0]<<8)|m17stream.lsf.type[1]);
				for(uint8_t i=0; i<14; i++)
					dbg_print(TERM_YELLOW, "%02X", m17stream.lsf.meta[i]);
				dbg_print(TERM_YELLOW, "\n");*/

				if(m17stream.fn&0x8000U) //last stream frame
				{
					//send the final EOT marker
					uint32_t frame_buff_cnt=0;
					gen_eot_i8(frame_symbols, &frame_buff_cnt);

					//filter and send out to the device
					filterSymbols(bsb_samples, frame_symbols, rrc_taps_5, 0);
					memcpy(&bsb_chunk[3], bsb_samples, sizeof(bsb_samples));
					write(fd, (uint8_t*)bsb_chunk, sizeof(bsb_chunk));

					time(&rawtime);
					timeinfo=localtime(&rawtime);

					dbg_print(TERM_SKYBLUE, "[%02d:%02d:%02d]",
						timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
					dbg_print(TERM_GREEN, " Stream TX end\n");
					usleep(8*40e3); //wait 320ms (8 M17 frames) - let the transmitter consume all the buffered samples
					
					//disable TX
					gpio_set(config.pa_en, 0);

					//restart RX
					while (dev_stop_tx() != 0) usleep(40e3);
					while (dev_start_rx() != 0) usleep(40e3);
					time(&rawtime);
					timeinfo=localtime(&rawtime);
					dbg_print(TERM_SKYBLUE, "[%02d:%02d:%02d]",
						timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
					dbg_print(TERM_GREEN, " RX start\n");

					tx_state=TX_IDLE;
				}
			}

			//M17 packet data - "Packet Mode IP Packet"
			else if(strstr((char*)rx_buff, "M17P")==(char*)rx_buff)
			{
				time(&rawtime);
				timeinfo=localtime(&rawtime);
				dbg_print(TERM_SKYBLUE, "[%02d:%02d:%02d]", timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
				dbg_print(TERM_GREEN, " M17 Inet packet received\n");

				uint8_t call_dst[10], call_src[10], can, type;
				decode_callsign_bytes(call_dst, &rx_buff[4+0]);
				decode_callsign_bytes(call_src, &rx_buff[4+6]);
				can=(*((uint16_t*)&rx_buff[4+6+6])>>7)&0xF;
				type=rx_buff[4+240/8];
				
				dbg_print(TERM_DEFAULT, " ├ "); dbg_print(TERM_YELLOW, "DST: "); dbg_print(TERM_DEFAULT, "%s\n", call_dst);
				dbg_print(TERM_DEFAULT, " ├ "); dbg_print(TERM_YELLOW, "SRC: "); dbg_print(TERM_DEFAULT, "%s\n", call_src);
				dbg_print(TERM_DEFAULT, " ├ "); dbg_print(TERM_YELLOW, "CAN: "); dbg_print(TERM_DEFAULT, "%d\n", can);
				if(type!=5) //assuming 1-byte type specifier
				{
					dbg_print(TERM_DEFAULT, " └ "); dbg_print(TERM_YELLOW, "TYPE: "); dbg_print(TERM_DEFAULT, "%d\n", type);
				}
				else
				{
					dbg_print(TERM_DEFAULT, " ├ "); dbg_print(TERM_YELLOW, "TYPE: "); dbg_print(TERM_DEFAULT, "SMS\n");
					dbg_print(TERM_DEFAULT, " └ "); dbg_print(TERM_YELLOW, "MSG: "); dbg_print(TERM_DEFAULT, "%s\n", &rx_buff[4+240/8+1]);
				}

				//TODO: handle TX here
				int8_t frame_symbols[SYM_PER_FRA];						//raw frame symbols
				int8_t bsb_samples[SYM_PER_FRA*5];						//filtered baseband samples = symbols*sps
				uint8_t bsb_chunk[963] = {CMD_TX_DATA, 0xC3, 0x03};		//baseband samples wrapped in a frame

				//log to file
				FILE* logfile=fopen((char*)config.log_path, "awb");
				if(logfile!=NULL)
				{
					time(&rawtime);
					timeinfo=localtime(&rawtime);
					fprintf(logfile, "\"%02d:%02d:%02d\" \"%s\" \"%s\" \"Internet\" \"--\" \"--\"\n",
						timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec,
						call_src, call_dst);
				}
				
				time(&rawtime);
				timeinfo=localtime(&rawtime);
				dbg_print(TERM_SKYBLUE, "[%02d:%02d:%02d]",
					timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
				dbg_print(TERM_GREEN, " Packet TX start\n");

				//stop RX, set PA_EN=1 and initialize TX
				while (dev_stop_rx() != 0) usleep(40e3);
				usleep(2e3);
				gpio_set(config.pa_en, 1);
				while (dev_start_tx() != 0) usleep(40e3);
				usleep(10e3);
				
				//flush the RRC baseband filter
				filterSymbols(NULL, NULL, NULL, 0);
				
				//generate frame symbols, filter them and send out to the device
				//we need to prepare 3 frames to begin the transmission - preamble, LSF and stream frame 0
				//let's start with the preamble
				uint32_t frame_buff_cnt=0;
				gen_preamble_i8(frame_symbols, &frame_buff_cnt, PREAM_LSF);
				
				//filter and send out to the device
				filterSymbols(bsb_samples, frame_symbols, rrc_taps_5, 0);
				memcpy(&bsb_chunk[3], bsb_samples, sizeof(bsb_samples));
				write(fd, (uint8_t*)bsb_chunk, sizeof(bsb_chunk));
				
				//now the LSF
				gen_frame_i8(frame_symbols, NULL, FRAME_LSF, (lsf_t*)&rx_buff[4], 0, 0);
				
				//filter and send out to the device
				filterSymbols(bsb_samples, frame_symbols, rrc_taps_5, 0);
				memcpy(&bsb_chunk[3], bsb_samples, sizeof(bsb_samples));
				write(fd, (uint8_t*)bsb_chunk, sizeof(bsb_chunk));
				
				//packet frames
				uint16_t pld_len=rx_len-(4+240/8); //"M17P" plus 240-bit LSD
				uint8_t frame=0;
				uint8_t pld[26];
				
				while(pld_len>25)
				{
					memcpy(pld, &rx_buff[4+240/8+frame*25], 25);
					pld[25]=frame<<2;
					gen_frame_i8(frame_symbols, pld, FRAME_PKT, NULL, 0, 0);
					filterSymbols(bsb_samples, frame_symbols, rrc_taps_5, 0);
					memcpy(&bsb_chunk[3], bsb_samples, sizeof(bsb_samples));
					write(fd, (uint8_t*)bsb_chunk, sizeof(bsb_chunk));
					pld_len-=25;
					frame++;
					usleep(40*1000U);
				}
				memset(pld, 0, 26);
				memcpy(pld, &rx_buff[4+240/8+frame*25], pld_len);
				pld[25]=(1<<7)|(pld_len<<2); //EoT flag set, amount of remaining data in the 'frame number' field
				gen_frame_i8(frame_symbols, pld, FRAME_PKT, NULL, 0, 0);
				filterSymbols(bsb_samples, frame_symbols, rrc_taps_5, 0);
				memcpy(&bsb_chunk[3], bsb_samples, sizeof(bsb_samples));
				write(fd, (uint8_t*)bsb_chunk, sizeof(bsb_chunk));
				usleep(40*1000U);

				//now the final EOT marker
				frame_buff_cnt=0;
				gen_eot_i8(frame_symbols, &frame_buff_cnt);

				//filter and send out to the device
				filterSymbols(bsb_samples, frame_symbols, rrc_taps_5, 0);
				memcpy(&bsb_chunk[3], bsb_samples, sizeof(bsb_samples));
				write(fd, (uint8_t*)bsb_chunk, sizeof(bsb_chunk));

				time(&rawtime);
				timeinfo=localtime(&rawtime);

				dbg_print(TERM_SKYBLUE, "[%02d:%02d:%02d]",
					timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
				dbg_print(TERM_GREEN, " PKT TX end\n");
				usleep(3*40e3); //wait 120ms (3 M17 frames)
				
				//disable TX
				gpio_set(config.pa_en, 0);

				//restart RX
				while (dev_stop_tx() != 0) usleep(40e3);
				while (dev_start_rx() != 0) usleep(40e3);
				time(&rawtime);
				timeinfo=localtime(&rawtime);
				dbg_print(TERM_SKYBLUE, "[%02d:%02d:%02d]",
					timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
				dbg_print(TERM_GREEN, " RX start\n");

				tx_state = TX_IDLE;
			}

			//clear the rx_buff
			memset((uint8_t*)rx_buff, 0, rx_len);
		}

		//tx timeout
		if(tx_state==TX_ACTIVE && (get_ms()-tx_timer)>240) //240ms timeout
		{
			time(&rawtime);
			timeinfo=localtime(&rawtime);

			dbg_print(TERM_SKYBLUE, "[%02d:%02d:%02d]",
				timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
			dbg_print(TERM_GREEN, " TX timeout\n");
			//usleep(10*40e3); //wait 400ms (10 M17 frames)
			
			//disable TX
			gpio_set(config.pa_en, 0);

			//restart RX
			while (dev_stop_tx() != 0) usleep(40e3);
			while (dev_start_rx() != 0) usleep(40e3);
			time(&rawtime);
			timeinfo=localtime(&rawtime);
			dbg_print(TERM_SKYBLUE, "[%02d:%02d:%02d]",
				timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
			dbg_print(TERM_GREEN, " RX start\n");

			tx_state=TX_IDLE;
		}

		//connection with the reflector borken
		if(time(NULL)-last_refl_ping>30)
		{
			//for now, just cry about it and quit
			dbg_print(TERM_RED, "Lost connection with the reflector\nExiting");

			//cleanup gpios
			gpio_cleanup();

			//close log file if necessary
			if(logfile!=NULL)
			{
				fclose(logfile);
			}

			exit(EXIT_FAILURE);
		}
	}
	
	//should never get here	
	return 0;
}


