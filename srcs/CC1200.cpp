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
#include <chrono>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <stdarg.h>

#include <netinet/ip_icmp.h>
#include <netinet/udp.h>
#include <netinet/tcp.h>
#include <netinet/ip.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <fcntl.h> 
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <time.h>
#include <signal.h>

#include "Log.h"
#include "CC1200.h"
#include "Configure.h"
#include "Packet.h"
#include "SafePacketQueue.h"
#include "Version.h"
#include "LSF.h"

extern IPFrameFIFO Modem2Gate;
extern IPFrameFIFO Get2Modem;
extern CVersion g_Version;
extern CGateway g_Gate;

//rpi-interface commands
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
	CMD_SET_TX_START,
	CMD_SET_RX,

	//GET
	CMD_GET_IDENT = 0x80,
	CMD_GET_CAPS,
	CMD_GET_RX_FREQ,
	CMD_GET_TX_FREQ,
	CMD_GET_TX_POWER,
	CMD_GET_FREQ_CORR
};

#define MAX_UDP_LEN 65535

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

// returns true on error
bool CCC1200::Start()
{
	LogInfo("Starting gateway");
	loadConfig();
	fd = -1;
	loadConfig();
	srand(time(nullptr));

	//-----------------------------------gpio reset-----------------------------------
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
			std::this_thread::sleep_for (std::chrono::milliseconds(50));
			if (gpioSetValue(cfg.nrst, 1))
				return true;
			else
				// wait for device bootup
				std::this_thread::sleep_for(std::chrono::seconds(1));
		}
	} else
		return true;


	//-----------------------------------uart init-----------------------------------
	fd = open(cfg.uartDev.c_str(), O_RDWR | O_NOCTTY | O_SYNC);
	if (fd < 0)
	{
		LogError("Could not open %s", cfg.uartDev.c_str());
		return true;
	}
	LogInfo("UART %s opened on descriptor %d", cfg.uartDev.c_str(), fd);
	if (setIinterface(cfg.baudRate, 0))
		return true;

	//PING-PONG test
	if (sendPING())
		return true;

	int count = 0;
	LogInfo("Testing the radio hat reply to PING...");
	do
	{
		auto rval = ioctl(fd, FIONREAD, &count);
		if (rval < 0)
		{
			LogError("ioctl error: %s", strerror(errno));
			return true;
		} else if (count < 6) {
			LogDebug("There are %d of 6 bytes available", count);
			std::this_thread::sleep_for(std::chrono::seconds(1));
		}
	} while(count < 6);

	uint8_t ping_test[6] = { 0 };
	auto n = read(fd, ping_test, 6);
	if (n < 0)
	{
		LogError("PONG read() error: %s", strerror(errno));
		return true;
	}
	else if (n < 6)
	{
		LogError("Only read %d of 6-byte PONG response", n);
		return true;
	}

	uint32_t dev_err =* ((uint32_t*)&ping_test[2]);
	if ((ping_test[0] == 0) and (ping_test[1] == 6) and (dev_err == 0))
	{
		LogInfo("Got PONG!");
	} else {
		LogInfo(" PONG error code: 0x%04x", dev_err);
		return true;
	}

	//config the device
	if (dev_set_freq(CMD_SET_RX_FREQ, cfg.rxFreq) 
		or dev_set_freq(CMD_SET_TX_FREQ, cfg.txFreq) 
		or dev_set_freq_corr(cfg.freqCorr) 
		or dev_set_tx_power(cfg.power) 
		or dev_set_afc(cfg.afc))
	{
		return true;
	} else {
		if (cfg.afc)
			LogInfo("AFC enabled");
		else
			LogInfo("AFC disabled");
	}

	// start the Rx
	if (dev_start_rx())
		return true;

	// start up the processing loop
	runFuture = std::async(std::launch::async, &CCC1200::run, this);

	LogInfo("Device start - RX");

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

uint32_t CCC1200::getMilliseconds(void)
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
			LogError("Can't find a baud rate of %u", baud);
			return B0;
    }
}

bool CCC1200::setIinterface(uint32_t speed, int parity)
{
	struct termios tty;
	if (0 > tcgetattr(fd, &tty))
	{
		LogError("Fatal error in setIinterface() from tcgetattr: %s", strerror(errno));
		return true;
 	}
	auto spd = getBaud(speed);
	if (B0 == spd)
		return true;
	cfsetospeed(&tty, spd);
	cfsetispeed(&tty, spd);

	tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;     // 8-bit chars
	// disable IGNBRK for mismatched speed tests; otherwise receive break
	// as \000 chars
	tty.c_iflag &= ~IGNBRK;			// disable break processing
	tty.c_lflag = 0;				// no signaling chars, no echo,
									// no canonical processing
	tty.c_oflag = 0;                // no remapping, no delays
	tty.c_cc[VMIN]  = 0;            // read doesn't block
	tty.c_cc[VTIME] = 0;            // 0.0 seconds read timeout

	tty.c_iflag &= ~(IXON | IXOFF | IXANY); // shut off xon/xoff ctrl

	tty.c_cflag |= (CLOCAL | CREAD);// ignore modem controls,
                                        // enable reading
	tty.c_cflag &= ~(PARENB | PARODD);      // shut off parity
	tty.c_cflag |= parity;
	tty.c_cflag &= ~CSTOPB;
	tty.c_cflag &= ~CRTSCTS;

	if (0 > tcsetattr(fd, TCSANOW, &tty))
	{		
		LogError("Fatal error in setIinterface() from tcsetattr: %s", strerror(errno));
		return true;
	}
	LogDebug("UART set for %u baud with parity bit of %d", speed, parity);
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

// returns true on failure
bool CCC1200::sendPING(void)
{
	const uint8_t cmd[2] = { CMD_PING, 2u };
	auto rval = write(fd, cmd, cmd[1]);
	if (rval < 0)
		LogError("write ping error: %s", strerror(errno));
	else if (rval < 2)
		LogError("Only wrote %d bytes of 2 byte PING command", rval);
	return (2 == rval) ? false : true;
}

ssize_t CCC1200::getResponse(void)
{
	uint8_t resp[3] = { 0 };
	int count;
	do {
		if (ioctl(fd, FIONREAD, &count) < 0) {
			LogError("Error from ioctl(): %s", strerror(errno));
			return -1;
		}
		if (count < 3)
		{
			LogDebug("Waiting for response...");
			std::this_thread::sleep_for(std::chrono::seconds(1));
		}
	} while (count != 3);
	auto n = read(fd, resp, 3u);
	if (n < 0) {
		LogError("getResponse read() error: %s", strerror(errno));
	} else if (n < 3) {
		LogError("getResponse only read %d bytes of 3", n);
	}
	return (3 == n) ? ssize_t(resp[2]) : -1;
}

bool CCC1200::dev_set_freq(unsigned r_or_t, uint32_t freq)
{
	uint8_t cmd[6];
	cmd[0] = r_or_t ? CMD_SET_RX_FREQ : CMD_SET_TX_FREQ;
	cmd[1] = 6;
	*((uint32_t*)&cmd[2]) = freq;
	const char *cstr = r_or_t ? "Rx" : "Tx";
	ssize_t n = write(fd, cmd, cmd[1]);
	if (n < 0) {
		LogError("Error writing set %s frequency: %s", cstr, strerror(errno));
		return true;
	} else if (n < 6) {
		LogError("Only wrote %d of 6 bytes setting %s Frequency", n, cstr);
		return true;
	}
	
	//wait for device's response
	n = getResponse();
	if (n)
	{
		LogError("Error %d setting %s frequency: %lu Hz", n, cstr, freq);
		return true;
	}
	LogInfo("%s Frequency: %u Hz", cstr, freq);
	return false;
}

bool CCC1200::dev_set_freq_corr(int16_t corr)
{
	uint8_t cmd[4];
	cmd[0] = CMD_SET_FREQ_CORR;	//freq correction
	cmd[1] = 4;
	*((int16_t*)&cmd[2]) = corr;
	ssize_t n = write(fd, cmd, cmd[1]);
	if (n < 0) {
		LogError("Error writing freq. correction: %s", strerror(errno));
		return true;
	} else if (n < 4) {
		LogError("Only wrote %d of 4 bytes freq. correction", n);
		return true;
	}

	//wait for device's response
	n = getResponse();
	if (n)
	{
		LogError("Error %d setting frequency correction of %d", n, corr);
		return true;
	}
	LogInfo("Frequency Offset: %d Hz", corr);
	return false;
}

bool CCC1200::dev_set_afc(bool en)
{
	uint8_t cmd[3];
	cmd[0] = CMD_SET_AFC;
	cmd[1] = 3;
	cmd[2] = en ? 1 : 0;

	ssize_t n = write(fd, cmd, cmd[1]);
	if (n < 0) {
		LogError("Error writing AFC: %s", strerror(errno));
		return true;
	} else if (n < 3) {
		LogError("Only wrote %d of 3 bytes freq. correction", n);
		return true;
	}

	//wait for device's response
	n = getResponse();
	if (n)
	{
		LogError("Error setting AFC");
		return true;
	}
	LogInfo("AFC: %s", en ? "set" : "not set");
	return false;
}

bool CCC1200::dev_set_tx_power(float power) //power in dBm
{
	uint8_t cmd[3];
	cmd[0] = CMD_SET_TX_POWER;	//transmit power
	cmd[1] = 3;
	cmd[2] = roundf(power*4.0f);
	ssize_t n = write(fd, cmd, cmd[1]);
	if (n < 0) {
		LogError("Error writing Tx Power: %s", strerror(errno));
		return true;
	} else if (n < 3) {
		LogError("Only wrote %d of 3 bytes Tx Power", n);
		return true;
	}

	//wait for device's response
	n = getResponse();
	
	if (n)
	{
		LogError("Error %d setting TX power: %.2f dBm", n, power);
		return true;
	}
	LogInfo("Tx Power: %.2f dBm", power);
	return false;
}

bool CCC1200::dev_start_tx(void)
{
	const uint8_t cmd[2] ={ CMD_SET_TX_START, 2u };
	auto n = write(fd, cmd, cmd[1]);
	if (n < 0)
		LogError("ERROR: Could not send Tx Start command");
	else if (n < 2)
		LogError("ERROR: Sent %d byte Tx Start command", n);
	return (2 == n) ? false : true;
}

bool CCC1200::dev_start_rx(void)
{
	uint8_t cmd[3] ={ CMD_SET_RX, 3u, 1u };
	auto n = write(fd, cmd, cmd[1]);
	if (n < 0)
		LogError("ERROR: Could not send Rx Start command");
	else if (n < 3)
		LogError("ERROR: Sent %d byte Rx Start command", n);
	return (3 == n) ? false : true;
}

bool CCC1200::dev_stop_rx(void)
{
	uint8_t cmd[3] = {CMD_SET_RX, 3u, 0u };
	auto n = write(fd, cmd, cmd[1]);
	if (n < 0)
		LogError("ERROR: Could not send Rx Stop command");
	else if (n < 3)
		LogError("ERROR: Sent %d byte Rx Stop command", n);
	return (3 == n) ? false : true;
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

//samples per symbol (sps) = 5
void CCC1200::filter_symbols(int8_t out[SYM_PER_FRA*5], const int8_t in[SYM_PER_FRA], const float* flt, uint8_t phase_inv)
{
	#define FLT_LEN 41
	static int8_t last[FLT_LEN]; //memory for last symbols

	if (out!=NULL)
	{
		for (uint8_t i=0; i<SYM_PER_FRA; i++)
		{
			for (uint8_t j=0; j<5; j++)
			{
				for (uint8_t k=0; k<FLT_LEN-1; k++)
					last[k]=last[k+1];

				if (j==0)
				{
					if (phase_inv) //optional phase inversion
						last[FLT_LEN-1]=-in[i];
					else
						last[FLT_LEN-1]= in[i];
				}
				else
					last[FLT_LEN-1]=0;

				float acc=0.0f;
				for (uint8_t k=0; k<FLT_LEN; k++)
					acc+=last[k]*flt[k];

				out[i*5+j]=acc*TX_SYMBOL_SCALING_COEFF*sqrtf(5.0f); //crank up the gain
			}
		}
	}
	else
	{
		for (uint8_t i=0; i<FLT_LEN; i++)
			last[i]=0;
	}
}

void CCC1200::writeBSB(int8_t *bsb_samples)
{
	auto n = write(fd,  bsb_samples, 960);
	if (n < 0)
	{
		LogError("Error writing bsb samples: %s", strerror(errno));
	} else if (n < 960) {
		LogError("Only wrote %d of 960 bsb samples", n);
	}
}

void CCC1200::run()
{
	// extend the LSF syncword pattern with 8 symbols from the preamble
	int8_t lsf_sync_ext[16];
	lsf_sync_ext[0]=3; lsf_sync_ext[1]=-3; lsf_sync_ext[2]=3; lsf_sync_ext[3]=-3;
	lsf_sync_ext[4]=3; lsf_sync_ext[5]=-3; lsf_sync_ext[6]=3; lsf_sync_ext[7]=-3;
	memcpy(&lsf_sync_ext[8], lsf_sync_symbols, 8);
	ERxState rx_state = ERxState::idle;
	ETxState tx_state = ETxState::idle;

	// data for the processing loop
	int uart_byte_count;
	float f_flt_buff[8*5+2*(8*5+4800/25*5)+2];	// 8 preamble symbols, 8 for the syncword, and 960 for the payload.
												// floor(sps/2)=2 extra samples for timing error correction
	SLSF lsf;						// recovered LSF
	uint16_t sample_cnt = 0;		// sample counter (for RX sync timeout)
	uint16_t fn, last_fn = 0xffffu;	// current and last received FN (stream mode)

	uint8_t pkt_fn, last_pkt_fn;	// current and last frame number
	uint8_t pkt_payload[825];		// packet payload
	uint16_t pkt_payload_size = 0;	// accumulated payload size

	bool isFirstFrame = true;		// first decoded frame after SYNC?
	uint8_t lich_parts = 0;			// LICH chunks received (bit flags)
	uint8_t lsf_b[30];				// raw decoded LSF
	uint32_t tx_timer = 0;			// timer for tx timeouts
	int8_t flt_buff[8*5+1];			// length of this has to match RRC filter's length
	uint8_t rx_bsb_sample = 0;		// one byte to receive from the modem
	float f_sample;					// symbol
	keep_running = true;

	while(keep_running)
	{
		// are there any new baseband samples to process?
		ioctl(fd, FIONREAD, &uart_byte_count);	// incoming rf data
		if (uart_byte_count > 0) // incoming RF!
		{
			read(fd, &rx_bsb_sample, 1);

			//push buffer
			for (uint8_t i=0; i<sizeof(flt_buff)-1; i++)
				flt_buff[i] = flt_buff[i+1];
			flt_buff[sizeof(flt_buff) - 1] = rx_bsb_sample;

			f_sample=0.0f;
			for (uint8_t i=0; i<sizeof(flt_buff); i++)
				f_sample += rrc_taps_5[i] * (float)flt_buff[i];
			f_sample *= RX_SYMBOL_SCALING_COEFF; //symbol map (works for CC1200 only)

			for (uint16_t i=0; i<sizeof(f_flt_buff)/sizeof(float)-1; i++)
				f_flt_buff[i] = f_flt_buff[i+1];
			f_flt_buff[sizeof(f_flt_buff) / sizeof(float) - 1] = f_sample;

			//L2 norm check against syncword
			float symbols[16];
			for (uint8_t i=0; i<16; i++)
				symbols[i] = f_flt_buff[i*5];

			float dist_lsf = eucl_norm(&symbols[0], lsf_sync_ext, 16); //check against extended LSF syncword (8 symbols, alternating -3/+3)
			float dist_pkt = eucl_norm(&symbols[0], pkt_sync_symbols, 8);
			float dist_str_a = eucl_norm(&symbols[8], str_sync_symbols, 8);
			for (uint8_t i=0; i<16; i++)
				symbols[i] = f_flt_buff[960+i*5];
			float dist_str_b = eucl_norm(&symbols[8], str_sync_symbols, 8);
			float dist_str = sqrtf(dist_str_a * dist_str_a + dist_str_b * dist_str_b);

			//LSF received at idle state
			if ((dist_lsf <= 4.5f) and (rx_state == ERxState::idle)) // incoming LSF?
			{
				//find L2's minimum
				uint8_t sample_offset = 0;
				for (uint8_t i=1; i<=2; i++)
				{
					for (uint8_t j=0; j<16; j++)
						symbols[j] = f_flt_buff[j*5+i];

					float d = eucl_norm(symbols, lsf_sync_ext, 16);

					if (d < dist_lsf)
					{
						dist_lsf = d;
						sample_offset = i;
					}
				}

				float pld[SYM_PER_PLD];

				for (uint16_t i=0; i<SYM_PER_PLD; i++)
				{
					pld[i] = f_flt_buff[16*5+i*5+sample_offset]; //add symbol timing correction
				}

				uint32_t e = decode_LSF((lsf_t *)lsf.GetData(), pld);

				uint8_t can;
				uint16_t type, crc;
				const CCallsign dst(lsf.GetCDstAddress());
                const CCallsign src(lsf.GetCSrcAddress());
				type = lsf.GetFrameType();
				can = (type >> 7) & 0xfu;

				if (lsf.CheckCRC()) //if CRC valid
				{
					rx_state = ERxState::sync;	//change RX state
					sample_cnt = 0;		//reset rx timeout timer

					last_fn = 0xffffu;

					LogInfo("CRC OK | DST: %-9s | SRC: %-9s | TYPE: %04X (CAN=%d) | MER: %-3.1f%%", dst.c_str(), src.c_str(), type, can, (float)e/0xFFFFU/SYM_PER_PLD/2.0f*100.0f);

					if (type & 1) //if stream
					{
						lsfPack = std::make_unique<CPacket>(true); // everything but magic is zeroed
						while (0u == lsfPack->GetStreamId())
							lsfPack->SetStreamId(rand() & 0xffffu);

						memcpy(lsfPack->GetLSDAddress(), &lsf, 28);	// LSD
						lsfPack->CalcCRC();							// CRC

						// make a copy and send it
						auto pack = std::make_unique<CPacket>(true);
						memcpy(pack->GetData()+4, lsfPack->GetCData()+4, 50);
						Modem2Gate.Push(pack);						// send a single frame to the reflector

						LogDash("\"%s\" \"%s\" \"RF\" \"%u\" \"%3.1f%%\"", src.c_str(), dst.c_str(), can, (float)e/0xFFFFU/SYM_PER_PLD/2.0f*100.0f);
					}
				}
				else
				{
					LogWarning("CRC error in LSF");
				}
			}

			else if (dist_str <= 5.0f)	// incoming voice data
			{
				rx_state = ERxState::sync;
				sample_cnt = 0;		//reset rx timeout timer

				//find L2's minimum
				uint8_t sample_offset=0;
				for (uint8_t i=1; i<=2; i++)
				{
					for (uint8_t j=0; j<16; j++)
						symbols[j] = f_flt_buff[j*5+i];
					
					float tmp_a = eucl_norm(&symbols[8], str_sync_symbols, 8);
					for (uint8_t j=0; j<16; j++)
						symbols[j] = f_flt_buff[960+j*5+i];
					
					float tmp_b = eucl_norm(&symbols[8], str_sync_symbols, 8);

					float d = sqrtf(tmp_a * tmp_a + tmp_b * tmp_b);

					if (d < dist_str)
					{
						dist_str = d;
						sample_offset = i;
					}
				}

				float pld[SYM_PER_PLD];
				
				for (uint16_t i=0; i<SYM_PER_PLD; i++)
				{
					pld[i] = f_flt_buff[16*5+i*5+sample_offset];
				}

				uint8_t lich[6];
				uint8_t lich_cnt;
				uint8_t frame_data[16];
				uint32_t e = decode_str_frame(frame_data, lich, &fn, &lich_cnt, pld);
				
				//set the last FN number to FN-1 if this is a late-join and the frame data is valid
				if (isFirstFrame and ((fn % 6) == lich_cnt))
				{
					last_fn = fn - 1;
				}
				
				if (((last_fn+1) & 0xffffu) == fn) //new frame. TODO: maybe a timeout would be better
				{
					if (lich_parts != 0x3fu) //6 chunks = 0b111111
					{
						//reconstruct LSF chunk by chunk
						memcpy(lsf_b+(5u * lich_cnt), lich, 5); //40 bits
						lich_parts |= (1 << lich_cnt);
						if ((lich_parts == 0x3fu) and (not lsfPack)) //collected all of them?
						{
							if (0 == CRC_M17(lsf_b, 30)) //CRC check
							{
								lsfPack = std::make_unique<CPacket>(true);
								while (lsfPack->GetStreamId())
									lsfPack->SetStreamId(rand() % 0xffffu);
								memcpy(lsfPack->GetLSDAddress(), lsf_b, 28);
								unsigned type = lsfPack->GetFrameType();
								unsigned can = (type >> 7) & 0xfu;
								const CCallsign dst(lsf_b);
								const CCallsign src(lsf_b+6);

								LogInfo("LICH LSF REC: DST: %-9s | SRC: %-9s | TYPE: 0x%04x (CAN=%u)", dst.c_str(), src.c_str(), type, can);

								LogInfo("\"%s\" \"%s\" \"RF\" \"%d\" \"--\"", src.c_str(), dst.c_str(), can);
							}
							else
							{
								LogWarning("LICH CRC error");
								lich_parts = 0; //reset flags
							}
						}
					}

					LogMessage("RF FRM: FN:%04X | LICH_CNT:%u | MER: %-3.1f%%", fn, lich_cnt, (float)e/0xFFFFU/SYM_PER_PLD/2.0f*100.0f);

					if (lsfPack)
					{
						lsfPack->SetFrameNumber(fn);
						memcpy(lsfPack->GetPayload(), frame_data, 16);

						// create a copy and send
						auto pack = std::make_unique<CPacket>(true);
						memcpy(pack->GetData()+4, lsfPack->GetCData()+4, 48);
						pack->CalcCRC();
						Modem2Gate.Push(pack);
					}

					last_fn = fn;
				}

				isFirstFrame = false;
			}

			else if ((dist_pkt <= 5.0f) and (rx_state == ERxState::sync)) // incoming Packet data
			{
				//find L2's minimum
				uint8_t sample_offset = 0;
				for (uint8_t i=1; i<=2; i++)
				{
					for (uint8_t j=0; j<8; j++)
						symbols[j]=f_flt_buff[j*5+i];
						
					float d = eucl_norm(symbols, pkt_sync_symbols, 8);
					
					if (d < dist_pkt)
					{
						dist_pkt = d;
						sample_offset = i;
					}
				}

				float pld[SYM_PER_PLD];
				uint8_t eof = 0;
				for (uint16_t i=0; i<SYM_PER_PLD; i++)
				{
					pld[i] = f_flt_buff[8*5+i*5+sample_offset];
				}

				/*uint32_t e = */decode_pkt_frame(pkt_payload+(25u*last_pkt_fn), &eof, &pkt_fn, pld);
				pkt_payload_size += eof ? pkt_fn : 25u;
				last_pkt_fn = pkt_fn;

				sample_cnt = 0;		//reset rx timeout timer
				if (eof) {
					if (0 == CRC_M17(pkt_payload, pkt_payload_size)) {
						auto pack = std::make_unique<CPacket>(false, pkt_payload_size);
						memcpy(pack->GetLSDAddress(), &lsf, 30);
						memcpy(pack->GetPayload(), pkt_payload, pkt_payload_size);
						const CCallsign dst(pack->GetCDstAddress());
						const CCallsign src(pack->GetCSrcAddress());
						LogInfo("RF Packet: SRC: %s DST %s", src.c_str(), dst.c_str());
						if ((0x5u == pkt_payload[0]) and (0x0u == pkt_payload[pkt_payload_size-2]))
							LogInfo("SMS: %s", pkt_payload);
						else
							LogInfo("NON-SMS DATA Size including type byte(s) and CRC: %u", pkt_payload_size);
						Modem2Gate.Push(pack);
					} else {
						LogWarning("RF Packet from %s Failed payload CRC", CCallsign(lsf.GetCSrcAddress()).c_str());
					}
					last_pkt_fn = 0u;
					pkt_payload_size = 0u;
				}
			}
			
			//RX sync timeout
			if (rx_state == ERxState::sync)
			{
				sample_cnt++;
				if (sample_cnt >= 960*2)
				{
					rx_state = ERxState::idle;
					sample_cnt = 0u;
					isFirstFrame = true;
					last_fn = 0xffffu; //TODO: there's a small chance that this will cause problems (it's a valid frame number)
					last_pkt_fn = 0u;
					lich_parts = 0u;
					lsfPack.reset();
				}
			}
		}

		auto gatePack = Get2Modem.Pop();
		if (gatePack) // incoming packet
		{
			if (gatePack->IsStreamData()) // stream mode data
			{
				tx_timer = getMilliseconds();

				int8_t frame_symbols[SYM_PER_FRA];	//raw frame symbols
				int8_t bsb_samples[SYM_PER_FRA*5];	//filtered baseband samples = symbols*sps
				fn = gatePack->GetFrameNumber();
				uint8_t lichCount = (fn & 0x7ffffu) % 6u;

				if (tx_state == ETxState::idle) //first received frame
				{
					tx_state = ETxState::active;

					//TODO: this needs to happen every time a new transmission appears
					//dev_stop_rx();
					//dbg_print(0, "RX stop\n");
					std::this_thread::sleep_for(std::chrono::milliseconds(10));

					//extract data
					memcpy(lsf.GetData(), gatePack->GetLSDAddress(), 28);
					const CCallsign dst(lsf.GetCDstAddress());
					const CCallsign src(lsf.GetCSrcAddress());

					//set TYPE field
					auto type = lsf.GetFrameType();
					type = (type & 0xff9fu) | 0x20u; // clear the subtype bits and set to ECD
					lsf.SetFrameType(type);
					LogDebug("Open Gate Stream: DST=%s SRC=%s TYPE=0x%04u", dst.c_str(), src.c_str(), type);

					//generate META field
					cfg.callSign.CodeOut(lsf.GetMetaData());
					g_Gate.GetLink().CodeOut(lsf.GetMetaData()+6);
					lsf.CalcCRC();

					//log to file
					LogDash("\"%s\" \"%s\" \"Internet\" \"--\" \"--\"", src.c_str(), dst.c_str());

					//stop RX, set PA_EN=1 and initialize TX
					if (dev_stop_rx())
						break;
					std::this_thread::sleep_for(std::chrono::milliseconds(2));
					//gpio_set(config.pa_en, 1);
					if (dev_start_tx())
						break;
					std::this_thread::sleep_for(std::chrono::milliseconds(10));

					//flush the RRC baseband filter
					filter_symbols(NULL, NULL, NULL, 0);
				
					//generate frame symbols, filter them and send out to the device
					//we need to prepare 3 frames to begin the transmission - preamble, LSF and stream frame 0
					//let's start with the preamble
					uint32_t frame_buff_cnt=0;
					gen_preamble_i8(frame_symbols, &frame_buff_cnt, PREAM_LSF);

					//filter and send out to the device
					filter_symbols(bsb_samples, frame_symbols, rrc_taps_5, 0);
					writeBSB(bsb_samples);

					//now the LSF
					gen_frame_i8(frame_symbols, NULL, FRAME_LSF, (lsf_t*)lsf.GetCData(), 0, 0);

					//filter and send out to the device
					filter_symbols(bsb_samples, frame_symbols, rrc_taps_5, 0);
					writeBSB(bsb_samples);

					//finally, the first frame
					gen_frame_i8(frame_symbols, gatePack->GetCPayload(), FRAME_STR, (lsf_t *)lsf.GetCData(), lichCount, fn);

					//filter and send out to the device
					filter_symbols(bsb_samples, frame_symbols, rrc_taps_5, 0);
					writeBSB(bsb_samples);
					LogDebug("Gateway Payload: FN=0x%04x", fn);
				}
				else
				{
					//only one frame is needed
					gen_frame_i8(frame_symbols, gatePack->GetCPayload(), FRAME_STR, &lsf, lichCount, fn);

					//filter and send out to the device
					filter_symbols(bsb_samples, frame_symbols, rrc_taps_5, 0);
					writeBSB(bsb_samples);
					LogDebug("Gateway Payload: FN=0x%04x", fn);
				}

				if (gatePack->IsLastPacket()) //last stream frame
				{
					//send the final EOT marker
					uint32_t frame_buff_cnt=0;
					gen_eot_i8(frame_symbols, &frame_buff_cnt);

					//filter and send out to the device
					filter_symbols(bsb_samples, frame_symbols, rrc_taps_5, 0);
					writeBSB(bsb_samples);

					LogDebug("Gateway Stream TX end: FN=0x%04u", fn);
					std::this_thread::sleep_for(std::chrono::milliseconds(400));

					//restart RX
					if (dev_start_rx())
						break;
					LogInfo("RX start");

					tx_state = ETxState::idle;
				}
			}

			//M17 packet data - "Packet Mode IP Packet"
			else if (gatePack->IsPacketData()) // packet mode data
			{
				auto can = (gatePack->GetFrameType() >> 7) & 0xffu;
				auto type = *gatePack->GetCPayload();
				const CCallsign dst(gatePack->GetCDstAddress());
				const CCallsign src(gatePack->GetCSrcAddress());

				LogInfo("M17 Inet packet received");
				LogInfo("├ DST: %s", dst.c_str());
				LogInfo("├ SRC: %s", src.c_str());
				LogInfo("├ CAN: %u", can);
				if ((type == 5)) {	//assuming 1-byte type specifier
					LogInfo("├ TYPE: SMS");
					if (gatePack->GetCData()[gatePack->GetSize()-2]) {
						auto length = gatePack->GetSize() - 37;
						std::vector<char> msg(length + 1, 0);
						memcpy(msg.data(), gatePack->GetCPayload()+1, length);
						LogInfo("└ MSG: %s", msg.data());
					} else {
						LogInfo("└ MSG: %s", (char *)(gatePack->GetCPayload() + 1));
					}
				} else {
					LogInfo("└ TYPE: %u", type);
				}

				//TODO: handle TX here
				int8_t frame_symbols[SYM_PER_FRA];	//raw frame symbols
				int8_t bsb_samples[SYM_PER_FRA*5];	//filtered baseband samples = symbols*sps

				//log to file
				LogDash("\"%s\" \"%s\" \"Internet\" \"--\" \"--\"", src.c_str(), dst.c_str());
				
				LogInfo("Transmitting Packet...");

				//stop RX, set PA_EN=1 and initialize TX
				if (dev_stop_rx())
					break;
				std::this_thread::sleep_for(std::chrono::milliseconds(2));
				if (dev_start_tx())
					break;
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
				
				//flush the RRC baseband filter
				filter_symbols(NULL, NULL, NULL, 0);
				
				//generate frame symbols, filter them and send out to the device
				//we need to prepare 3 frames to begin the transmission - preamble, LSF and stream frame 0
				//let's start with the preamble
				uint32_t frame_buff_cnt=0;
				gen_preamble_i8(frame_symbols, &frame_buff_cnt, PREAM_LSF);
				
				//filter and send out to the device
				filter_symbols(bsb_samples, frame_symbols, rrc_taps_5, 0);
				writeBSB(bsb_samples);
				
				//now the LSF
				gen_frame_i8(frame_symbols, NULL, FRAME_LSF, (lsf_t *)gatePack->GetLSDAddress(), 0, 0);
				
				//filter and send out to the device
				filter_symbols(bsb_samples, frame_symbols, rrc_taps_5, 0);
				writeBSB(bsb_samples);
				
				//packet frames
				uint16_t pld_len=gatePack->GetSize() - 34; // Payload size
				uint8_t frame = 0;
				uint8_t pld[26];
				
				while(pld_len > 25)
				{
					memcpy(pld, gatePack->GetCPayload()+(frame*25), 25);
					pld[25]=frame<<2;
					gen_frame_i8(frame_symbols, pld, FRAME_PKT, NULL, 0, 0);
					filter_symbols(bsb_samples, frame_symbols, rrc_taps_5, 0);
					writeBSB(bsb_samples);
					pld_len -= 25;
					frame++;
					std::this_thread::sleep_for(std::chrono::milliseconds(40));
				}
				memset(pld, 0, 26);
				memcpy(pld, gatePack->GetCPayload()+(frame+25), pld_len);
				pld[25] = (1 << 7) | (pld_len << 2); //EoT flag set, amount of remaining data in the 'frame number' field
				gen_frame_i8(frame_symbols, pld, FRAME_PKT, NULL, 0, 0);
				filter_symbols(bsb_samples, frame_symbols, rrc_taps_5, 0);
				writeBSB(bsb_samples);
					std::this_thread::sleep_for(std::chrono::milliseconds(40));

				//now the final EOT marker
				frame_buff_cnt=0;
				gen_eot_i8(frame_symbols, &frame_buff_cnt);

				//filter and send out to the device
				filter_symbols(bsb_samples, frame_symbols, rrc_taps_5, 0);
				writeBSB(bsb_samples);

				LogInfo("Done!");
				std::this_thread::sleep_for(std::chrono::milliseconds(400));	// wait 400ms (10 M17 frames)
				//restart RX
				if (dev_start_rx())
					break;
				LogInfo("RX start");
			}
		}

		//tx timeout
		if ((tx_state==ETxState::active) && (getMilliseconds() - tx_timer) > 240) //240ms timeout
		{
			LogWarning("TX timeout!");
			//restart RX
			if (dev_start_rx())
				break;

			LogInfo("RX idle");

			tx_state = ETxState::idle;
		}
	}
	LogInfo("CC1200 is not running");
}
