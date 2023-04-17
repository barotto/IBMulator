/*
 * Copyright (C) 2002-2021  The DOSBox Team
 * Copyright (C) 2023  Marco Bortolin
 *
 * This file is part of IBMulator.
 * 
 * IBMulator is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * IBMulator is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with IBMulator.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef IBMULATOR_SERIALMODEM_H
#define IBMULATOR_SERIALMODEM_H

#include "netservice.h"
#include "serialmodemfx.h"
#include <vector>
#include <memory>


#define MODEM_SREGS 100

// let some space for generated outgoing telnet traffic
#define MODEM_BUFFER_QUEUE_SIZE 1024
#define MODEM_BUFFER_CTS_THRESHOLD 512
#define MODEM_NET_RX_BUFFER_SIZE 32

#define MODEM_DEFAULT_PORT 23

#define MODEM_TICKRATE 1000 // Ticks per second
#define MODEM_TICKTIME_MS (1000 / MODEM_TICKRATE) // Tick interval in milliseconds
#define MODEM_RINGINTERVAL_TICKS (3000 / MODEM_TICKTIME_MS)
#define MODEM_WARMUP_DELAY_MS 250
#define MODEM_WARMUP_DELAY_TICKS (MODEM_WARMUP_DELAY_MS / MODEM_TICKTIME_MS)

#define MODEM_PRODUCT_CODE "249" // IBM PS/1 internal modem
#define MODEM_CHECKSUM "123" // hw/sw level 2.0

// The AT command line can consist of a 99-character command sequence
// including the AT prefix followed by "D<phone/hostname>", where the
// hostname can reach a length of up to 253 characters.
// AT<97-chars>D<253-chars> is a string of up to 353 characters plus a
// null.
#define MODEM_CMDBUF_SIZE 354

enum ResTypes {
	ResOK = 0,
	ResCONNECT = 1,
	ResRING = 2,
	ResNOCARRIER = 3,
	ResERROR = 4,
	ResNODIALTONE = 6,
	ResBUSY = 7,
	ResNOANSWER = 8
};


class Serial;

struct ModemStatus
{
	bool CTS = false; // Clear To Send
	bool DSR = false; // Data Set Ready
	bool RI = false;  // Ring Indicator
	bool DCD = false; // Data Carrier Detect
};

struct ModemControl
{
	bool DTR = false; // Data Terminal Ready
	bool RTS = false; // Request-to-send
};


class SerialModem
{
public:
	using StatusFn = std::function<void(const ModemStatus&)>;
	
private:
	enum class State {
		Idle, Originate, Handshaking, Connected
	} m_state = State::Idle;
	
	enum DTRMode {
		DTR_IGNORE, DTR_COMMAND, DTR_HANG, DRT_RESET
	};

	enum Handshake {
		HANDSHAKE_NO, HANDSHAKE_SHORT, HANDSHAKE_FULL
	} m_handshake = HANDSHAKE_NO;

	TimerID m_timer = NULL_TIMER_ID;
	NetService *m_network = nullptr;
	double m_txdelay_ms = 50.0;
	struct BaudRate { 
		unsigned bps;
		unsigned Bps;
		unsigned code;
		uint64_t handshake;
	} m_baudrate = {};
	double m_bytes_per_tick = .0;
	double m_bytes_ready = .0;
	uint64_t m_conn_timeout_ms = 5000;

	RingBuffer m_rqueue; // to serial port
	RingBuffer m_tqueue; // from serial port

	StatusFn m_MSR_callback;
	ModemStatus m_MSR;
	ModemControl m_MCR;

	std::map<std::string,std::string> m_phonebook;

	std::ofstream m_dump_file;


	char m_cmdbuf[MODEM_CMDBUF_SIZE] = {0};
	char m_prevcmd[MODEM_CMDBUF_SIZE] = {0};
	unsigned m_cmdpos = 0;
	bool m_commandmode = false; // true: interpret input as commands
	bool m_echo = false;      // local echo on or off
	bool m_echo_after_reset = true;
	int m_connect_code = -1;
	bool m_ringing = false;
	bool m_terse_result = false;
	unsigned m_rescode_set = 4; // Full messages, dial tone dialing, dial tone timeout, busy detect
	bool m_telnet_mode = false; // Telnet mode interprets IAC

	uint32_t m_doresponse = 0;
	uint32_t m_cmdpause = 0;
	int32_t m_ringtimer = 0;
	int32_t m_ringcount = 0;
	uint64_t m_accept_time = 0;
	uint32_t m_plusinc = 0;
	
	uint32_t m_flowcontrol = 0;
	uint32_t m_dtrmode = 0;
	int32_t m_dtrofftimer = 0;
	int32_t m_warmup_delay_ticks = 0;
	int32_t m_warmup_remain_ticks = 0;
	uint8_t m_tmpbuf[MODEM_BUFFER_QUEUE_SIZE] = {0};
	uint8_t m_reg[MODEM_SREGS] = {0};
	struct {
		bool binary[2] = {false};
		bool echo[2] = {false};
		bool supressGA[2] = {false};
		bool timingMark[2] = {false};
		bool inIAC = false;
		bool recCommand = false;
		uint8_t command = 0;
	} m_telClient;

	bool m_fx_enabled = false;
	SerialModemFX m_fx;
	struct {
		uint64_t time = 0;
		std::string host;
		uint16_t port = 0;
	} m_dial;

public:
	SerialModem();

	void init(NetService *_net, double _tx_delay);
	void close();
	void reset(unsigned _type);
	void power_off();

	void set_MSR_callback(StatusFn _fn) { m_MSR_callback = _fn; }
	void set_MCR(const ModemControl &_mcr);
	ModemStatus get_MSR() const { return m_MSR; }

	bool serial_read_byte(uint8_t *_byte);
	bool serial_write_byte(uint8_t _byte);

private:
	void set_MSR(const ModemStatus &_msr);
	void set_CTS(bool);
	void set_RI(bool);

	void timer(uint64_t);

	void send_line_to_serial(const char *line, bool _terse);
	void send_number_to_serial(uint32_t val, bool _terse);
	void send_res_to_serial(const ResTypes response);

	void echo(uint8_t ch);
	void do_command();
	
	void dial(const char *_str, const char *_addr);

	void enter_idle_state();
	void enter_handshaking_state();
	void enter_connected_state();

	void accept_incoming_call();
	uint32_t scan_number(char *&scan) const;
	char get_char(char * & scan) const;

	void telnet_emulation(uint8_t *data, uint32_t size);
	
	BaudRate find_baudrate(unsigned _baudrate);
};

#endif
