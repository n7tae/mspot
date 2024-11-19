// Copyright (C) 2011-2018,2020,2021 by Jonathan Naylor G4KLX

/****************************************************************
 *                                                              *
 *             More - An M17-only Repeater/HotSpot              *
 *                                                              *
 *         Copyright (c) 2024 by Thomas A. Early N7TAE          *
 *                                                              *
 * See the LICENSE file for details about the software license. *
 *                                                              *
 ****************************************************************/

#pragma once

#include <string>
#include <memory>

#include "BasePort.h"
#include "RingBuffer.h"
#include "Defines.h"
#include "Timer.h"

enum RESP_TYPE_MMDVM
{
	RTM_OK,
	RTM_TIMEOUT,
	RTM_ERROR
};

enum SERIAL_STATE
{
	SS_START,
	SS_LENGTH1,
	SS_LENGTH2,
	SS_TYPE,
	SS_DATA
};

class CModem
{
public:
	CModem(bool duplex, bool rxInvert, bool txInvert, bool pttInvert, unsigned int txDelay, bool trace, bool debug);
	~CModem();

	void setPort(std::unique_ptr<CBasePort> port);
	void setRFParams(unsigned int rxFrequency, int rxOffset, unsigned int txFrequency, int txOffset, int txDCOffset, int rxDCOffset, float rfLevel, unsigned int pocsagFrequency);
	void setModeParams(bool dstarEnabled, bool dmrEnabled, bool ysfEnabled, bool p25Enabled, bool nxdnEnabled, bool m17Enabled, bool pocsagEnabled, bool fmEnabled, bool ax25Enabled);
	void setLevels(float rxLevel, float cwIdTXLevel, float dstarTXLevel, float dmrTXLevel, float ysfTXLevel, float p25TXLevel, float nxdnTXLevel, float m17TXLevel, float pocsagLevel, float fmTXLevel, float ax25TXLevel);
	void setM17Params(unsigned int txHang);
	void setTransparentDataParams(unsigned int sendFrameType);
	bool open();

	bool hasM17() const;

	unsigned int getVersion() const;

	unsigned int readM17Data(unsigned char* data);

	bool hasM17Space() const;

	bool hasTX() const;
	bool hasCD() const;

	bool hasLockout() const;
	bool hasError() const;

	bool writeConfig();
	bool writeM17Data(const unsigned char* data, unsigned int length);

	bool writeM17Info(const char* source, const char* dest, const char* type);
	bool writeIPInfo(const std::string& address);

	bool writeSerial(const unsigned char* data, unsigned int length);
	unsigned int readSerial(unsigned char* data, unsigned int length);

	unsigned char getMode() const;
	bool setMode(unsigned char mode);

	bool sendCWId(const std::string& callsign);

	HW_TYPE getHWType() const;

	void clock(unsigned int ms);

	void close();

private:
	unsigned int               m_protocolVersion;
	unsigned int               m_dmrColorCode;
	bool                       m_ysfLoDev;
	unsigned int               m_ysfTXHang;
	unsigned int               m_p25TXHang;
	unsigned int               m_nxdnTXHang;
	unsigned int               m_m17TXHang;
	bool                       m_duplex;
	bool                       m_rxInvert;
	bool                       m_txInvert;
	bool                       m_pttInvert;
	unsigned int               m_txDelay;
	unsigned int               m_dmrDelay;
	float                      m_rxLevel;
	float                      m_cwIdTXLevel;
	float                      m_dstarTXLevel;
	float                      m_dmrTXLevel;
	float                      m_ysfTXLevel;
	float                      m_p25TXLevel;
	float                      m_nxdnTXLevel;
	float                      m_m17TXLevel;
	float                      m_pocsagTXLevel;
	float                      m_fmTXLevel;
	float                      m_ax25TXLevel;
	float                      m_rfLevel;
	bool                       m_useCOSAsLockout;
	bool                       m_trace;
	bool                       m_debug;
	unsigned int               m_rxFrequency;
	unsigned int               m_txFrequency;
	unsigned int               m_pocsagFrequency;
	bool                       m_dstarEnabled;
	bool                       m_dmrEnabled;
	bool                       m_ysfEnabled;
	bool                       m_p25Enabled;
	bool                       m_nxdnEnabled;
	bool                       m_m17Enabled;
	bool                       m_pocsagEnabled;
	bool                       m_fmEnabled;
	bool                       m_ax25Enabled;
	int                        m_rxDCOffset;
	int                        m_txDCOffset;
	std::unique_ptr<CBasePort> m_port;
	unsigned char*             m_buffer;
	unsigned int               m_length;
	unsigned int               m_offset;
	SERIAL_STATE               m_state;
	unsigned char              m_type;
	CRingBuffer<unsigned char> m_rxM17Data;
	CRingBuffer<unsigned char> m_txM17Data;
	CRingBuffer<unsigned char> m_rxSerialData;
	CRingBuffer<unsigned char> m_txSerialData;
	unsigned int               m_sendTransparentDataFrameType;
	CTimer                     m_statusTimer;
	CTimer                     m_inactivityTimer;
	CTimer                     m_playoutTimer;
	unsigned int               m_dstarSpace;
	unsigned int               m_dmrSpace1;
	unsigned int               m_dmrSpace2;
	unsigned int               m_ysfSpace;
	unsigned int               m_p25Space;
	unsigned int               m_nxdnSpace;
	unsigned int               m_m17Space;
	unsigned int               m_pocsagSpace;
	unsigned int               m_fmSpace;
	unsigned int               m_ax25Space;
	bool                       m_tx;
	bool                       m_cd;
	bool                       m_lockout;
	bool                       m_error;
	unsigned char              m_mode;
	HW_TYPE                    m_hwType;
	int                        m_ax25RXTwist;
	unsigned int               m_ax25TXDelay;
	unsigned int               m_ax25SlotTime;
	unsigned int               m_ax25PPersist;

	unsigned char              m_capabilities1;
	unsigned char              m_capabilities2;

	bool readVersion();
	bool readStatus();
	bool setConfig1();
	bool setConfig2();
	bool setFrequency();

	void printDebug();

	RESP_TYPE_MMDVM getResponse();
};
