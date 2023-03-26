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

#include "ibmulator.h"
#include "program.h"
#include "machine.h"
#include "serialmodem.h"
#include "utils.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <utility>
#include <fstream>
#include <sstream>

#define TEL_CLIENT 0
#define TEL_SERVER 1

enum ModemRegisters {
	MREG_AUTOANSWER_COUNT = 0,
	MREG_RING_COUNT = 1,
	MREG_ESCAPE_CHAR = 2,
	MREG_CR_CHAR = 3,
	MREG_LF_CHAR = 4,
	MREG_BACKSPACE_CHAR = 5,
	MREG_GUARD_TIME = 12,
	MREG_DTR_DELAY = 25
};

SerialModem::SerialModem()
{
	m_rqueue.set_size(MODEM_BUFFER_QUEUE_SIZE);
	m_tqueue.set_size(MODEM_BUFFER_QUEUE_SIZE);
}


SerialModem::BaudRate SerialModem::find_baudrate(unsigned _baudrate)
{
	if(_baudrate <=   300) return {    300,    30,   1 };
	if(_baudrate <=  1200) return {   1200,   120,   5 };
	if(_baudrate <=  2400) return {   2400,   240,  10 };
	if(_baudrate <=  4800) return {   4800,   480,  11 }; // Hayes code
	if(_baudrate <=  9600) return {   9600,   960,  12 }; // Hayes code
	if(_baudrate <= 14400) return {  14400,  1440,  13 }; // Hayes code
	if(_baudrate <= 19200) return {  19200,  1920,  14 }; // Hayes code
	if(_baudrate <= 28800) return {  28800,  2880, 107 }; // USR code
	if(_baudrate <= 33600) return {  33600,  3360, 155 }; // USR code
	if(_baudrate <= 56000) return {  56000,  5600, 162 }; // USR code
	if(_baudrate <= 57600) return {  57600,  5760, 316 }; // made up code
	                  else return { 115200, 11520, 500 }; // made up code
}

void SerialModem::init(NetService *_network, double _tx_delay_ms)
{
	m_network = _network;
	m_txdelay_ms = _tx_delay_ms;

	// Enable telnet-mode if configured
	m_telnet_mode = g_program.config().get_bool(MODEM_SECTION, MODEM_TELNET_MODE, true);

	// Get the connect speed
	int rate = g_program.config().get_int(MODEM_SECTION, MODEM_BAUD_RATE, 2400);
	m_baudrate = find_baudrate(rate);

	double timeout_s = g_program.config().get_real(MODEM_SECTION, MODEM_CONN_TIMEOUT, 10.0);
	m_conn_timeout_ms = uint64_t(timeout_s * 1000.0);
	m_conn_timeout_ms = clamp(m_conn_timeout_ms, uint64_t(1000), uint64_t(60'000));

	m_warmup_delay_ticks = MODEM_WARMUP_DELAY_TICKS * g_program.config().get_bool(MODEM_SECTION, MODEM_WARM_DELAY, false);

	m_connect_code = g_program.config().get_int(MODEM_SECTION, MODEM_CONNECT_CODE, -1);

	m_echo_after_reset = g_program.config().get_bool(MODEM_SECTION, MODEM_ECHO_ON, true);

	if(m_timer == NULL_TIMER_ID) {
		m_timer = g_machine.register_timer(
			std::bind(&SerialModem::timer, this, std::placeholders::_1),
			"Serial Modem"
		);
	}

	double byte_time_ms = 1000.0 / m_baudrate.Bps;
	m_bytes_per_tick = MODEM_TICKTIME_MS / byte_time_ms;
	m_bytes_ready = .0;

	g_machine.activate_timer(m_timer, MS_TO_NS(MODEM_TICKTIME_MS), true);

	std::string dump = g_program.config().get_string(MODEM_SECTION, MODEM_DUMP);
	if(!dump.empty()) {
		dump = (g_program.config().get_cfg_home() + FS_SEP + dump);
		m_dump_file = FileSys::make_ofstream(dump.c_str(), std::ofstream::binary);
		if(m_dump_file.is_open()) {
			PINFOF(LOG_V0, LOG_COM, "MODEM: dumping received network data into '%s'\n", dump.c_str());
		}
	}

	PINFOF(LOG_V0, LOG_COM, "MODEM: baud_rate:%u, tx_delay:%fms, telnet_mode:%u, warmup_delay:%d\n",
			m_baudrate.bps, m_txdelay_ms, m_telnet_mode, (m_warmup_delay_ticks > 0)
	);

	m_network->set_rx_queue(MODEM_NET_RX_BUFFER_SIZE, false);
	m_network->set_tx_queue(DEFAULT_TX_FIFO_SIZE);
	m_network->set_tx_threshold(m_txdelay_ms, m_baudrate.bps);

	auto listen_addr = g_program.config().get_string(MODEM_SECTION, MODEM_LISTEN_ADDR, "");
	if(!listen_addr.empty()) {
		try {
			auto [host,port] = NetService::parse_address(listen_addr, 2323);
			m_network->open(host.c_str(), port, NetService::Mode::Server, 0);
			m_network->set_server_not_ready();
		} catch(std::runtime_error &e) {
			PERRF(LOG_COM, "MODEM: cannot open server: %s\n", e.what());
		}
	} else {
		PINFOF(LOG_V0, LOG_COM, "MODEM: `listen_addr` not set: not accepting incoming connections.\n");
	}

	auto filename = g_program.config().try_get_file(MODEM_SECTION, MODEM_PHONEBOOK, FILE_TYPE_USER);
	if(!filename.empty() && !FileSys::file_exists(filename.c_str())) {
		std::string asset = g_program.config().get_file_path("phones.txt", FILE_TYPE_ASSET);
		if(!FileSys::file_exists(asset.c_str())) {
			PERRF(LOG_COM, "MODEM: file '%s' is missing from assets directory!\n", asset.c_str());
		} else {
			FileSys::copy_file(asset.c_str(), filename.c_str());
		}
	}
	if(!filename.empty()) {
		std::ifstream loadfile(filename);
		if(loadfile) {
			PINFOF(LOG_V0, LOG_COM, "MODEM: phonebook loading from '%s'\n", filename.c_str());

			std::string linein;
			unsigned n = 0;
			while (std::getline(loadfile, linein)) {
				linein = str_trim(linein);
				if(linein.empty() || linein[0] == '/') {
					continue;
				}
				std::istringstream iss(linein);
				std::string phone, address;

				if (!(iss >> phone >> address)) {
					PWARNF(LOG_V0, LOG_COM, "MODEM: phonebook: skipped bad line '%s'\n", linein.c_str());
					continue;
				}

				// Check phone number for characters ignored by Hayes modems.
				const char phoneValidChars[] = "01234567890*=,;#+>";
				size_t found = phone.find_first_not_of(phoneValidChars);
				if(found != phone.npos) {
					PWARNF(LOG_V0, LOG_COM, "MODEM: phonebook [%u]: number '%s' contains invalid character '%c'\n", n, phone.c_str(), phone[found]);
					continue;
				}
				m_phonebook[phone] = address;

				PINFOF(LOG_V1, LOG_COM, "MODEM: phonebook [%u]: mapped '%s' to '%s'\n", n, phone.c_str(), address.c_str());
				n++;
			}
		} else {
			PERRF(LOG_COM, "MODEM: cannot open phonebook file '%s'\n", filename.c_str());
		}
	} else {
		PWARNF(LOG_V0, LOG_COM, "MODEM: no phonebook defined\n");
	}

	m_fx_enabled = g_program.config().get_bool(SOUNDFX_SECTION, SOUNDFX_ENABLED, false);
	if(m_fx_enabled) {
		try {
			m_fx.install(m_baudrate.bps);
		} catch(std::runtime_error &) {
			PERRF(LOG_COM, "MODEM: sound effects disabled\n");
			m_fx_enabled = false;
		}
	}
}

void SerialModem::close()
{
	PDEBUGF(LOG_V0, LOG_COM, "MODEM: closing ...\n");
	if(m_network) {
		m_network->close();
		m_network = nullptr;
	}
	if(m_timer != NULL_TIMER_ID) {
		g_machine.unregister_timer(m_timer);
		m_timer = NULL_TIMER_ID;
	}

	if(m_fx_enabled) {
		m_fx.remove();
	}
}

void SerialModem::set_MSR(const ModemStatus &_msr)
{
	PDEBUGF(LOG_V0, LOG_COM, "MODEM: CTS:%u, DSR:%u, RI:%u, DCD:%u\n", _msr.CTS, _msr.DSR, _msr.RI, _msr.DCD);

	m_MSR = _msr;
	m_MSR_callback(m_MSR);
}

void SerialModem::set_CTS(bool _value)
{
	// PDEBUGF(LOG_V0, LOG_COM, "MODEM: CTS:%u\n", _value);

	set_MSR({
		_value,
		m_MSR.DSR,
		m_MSR.RI,
		m_MSR.DCD
	});
}

void SerialModem::set_RI(bool _value)
{
	set_MSR({
		m_MSR.CTS,
		m_MSR.DSR,
		_value,
		m_MSR.DCD
	});
}

void SerialModem::send_line_to_serial(const char *line)
{
	PINFOF(LOG_V1, LOG_COM, "MODEM: response: \"%s\"\n", line);

	size_t line_len = strlen(line);
	if((line_len + 2 + 2*m_terse_result) > m_rqueue.get_write_avail()) {
		PWARNF(LOG_V1, LOG_COM, "MODEM: serial tx fifo buffer overflow.\n");
	}

	if(!m_terse_result) {
		m_rqueue.write(m_reg[MREG_CR_CHAR]);
		m_rqueue.write(m_reg[MREG_LF_CHAR]);
	}
	m_rqueue.write((uint8_t *)line, line_len);
	m_rqueue.write(m_reg[MREG_CR_CHAR]);
	m_rqueue.write(m_reg[MREG_LF_CHAR]);
}

void SerialModem::send_number_to_serial(uint32_t val)
{
	auto str = str_format("%u", val);

	if(!LOG_DEBUG_MESSAGES) {
		PINFOF(LOG_V1, LOG_COM, "MODEM: response: '%s'\n", str.c_str());
	}

	if(m_terse_result) {
		str += char(m_reg[MREG_CR_CHAR]);
	} else {
		std::string crlf;
		crlf  = char(m_reg[MREG_CR_CHAR]);
		crlf += char(m_reg[MREG_LF_CHAR]);
		str = crlf + str + crlf;
	}

	if(LOG_DEBUG_MESSAGES) {
		PINFOF(LOG_V1, LOG_COM, "MODEM: response: '%s'\n", str_format_special(str.c_str()).c_str());
	}

	if(m_rqueue.get_write_avail() < str.length()) {
		PWARNF(LOG_V1, LOG_COM, "MODEM: serial tx fifo buffer overflow.\n");
	}

	m_rqueue.write((uint8_t *)str.data(), str.length());
}

void SerialModem::send_res_to_serial(const ResTypes response)
{
	std::string str;
	uint32_t code = response;
	switch (response) {
		case ResOK: {
			str = "OK";
			break;
		}
		case ResCONNECT:    {
			if((m_rescode_set == 4 && m_baudrate.bps <= 300) || m_rescode_set == 0) {
				str = "CONNECT";
			} else {
				if(m_connect_code >= 0) {
					code = m_connect_code;
				} else {
					code = m_baudrate.code;
				}
				str = str_format("CONNECT %u", m_baudrate.bps);
			}
			break;
		}
		case ResRING: {
			str = "RING";
			break;
		}
		case ResNOCARRIER: {
			str = "NO CARRIER";
			break;
		}
		case ResERROR: {
			str = "ERROR";
			break;
		}
		case ResNODIALTONE: {
			if(m_rescode_set != 2 && m_rescode_set != 4) {
				return;
			}
			str = "NO DIALTONE";
			break;
		}
		case ResBUSY: {
			if(m_rescode_set != 3 && m_rescode_set != 4) {
				return;
			}
			str = "BUSY";
			break;
		}
		case ResNOANSWER: {
			if(m_rescode_set != 3 && m_rescode_set != 4) {
				return;
			}
			str = "NO ANSWER";
			break;
		}
		default: {
			PDEBUGF(LOG_V0, LOG_COM, "MODEM: unhandled result code: %u.\n", response);
			return;
		}
	}

	if(m_doresponse != 1) {
		if(m_doresponse == 2 && (
			response == ResRING ||
			response == ResCONNECT || 
			response == ResNOCARRIER))
		{
			return;
		}
		if(m_terse_result) {
			send_number_to_serial(code);
		} else {
			send_line_to_serial(str.c_str());
		}
	}
}

void SerialModem::dial(const char *_str, const char *_addr)
{
	// refure any server connections
	m_network->set_server_not_ready();
	// close any pending client connections
	m_network->close_client();

	const char *addr;
	if(_addr) {
		addr = _addr;
	} else {
		addr = _str;
	}

	try {
		auto [host,port] = NetService::parse_address(addr, MODEM_DEFAULT_PORT);

		m_dial.host = host;
		m_dial.port = port;

		if(m_fx_enabled) {
			m_dial.time = m_fx.dial(_str, m_conn_timeout_ms); 
		} else {
			m_dial.time = g_machine.get_virt_time_ns() + 3_s;
		}

		m_state = State::Originate;

	} catch(std::runtime_error &e) {
		PERRF(LOG_COM, "MODEM: dial failed: %s\n", e.what());
		send_res_to_serial(ResNOCARRIER);
		enter_idle_state();
	}
}

void SerialModem::enter_handshaking_state()
{
	PDEBUGF(LOG_V0, LOG_COM, "MODEM: entering handshaking state ...\n");

	m_commandmode = false;
	m_ringing = false;
	m_dtrofftimer = -1;
	set_MSR({
		m_MSR.CTS,
		m_MSR.DSR,
		false, // RI ring
		false  // DCD carrier detect
	});

	m_state = State::Handshaking;

	if(m_fx_enabled) {
		m_accept_time = g_machine.get_virt_time_ns() + m_fx.handshake();
	} else {
		m_accept_time = g_machine.get_virt_time_ns();
	}
}

void SerialModem::accept_incoming_call()
{
	if(m_network->is_connected()) {
		enter_handshaking_state();
	} else {
		PDEBUGF(LOG_V0, LOG_COM, "MODEM: client socket not connected!\n");
		enter_idle_state();
	}
}

uint32_t SerialModem::scan_number(char *&_scan) const
{
	uint32_t ret = 0;
	while (char c = *_scan) {
		if (c >= '0' && c <= '9') {
			ret *= 10;
			ret += c - '0';
			_scan++;
		} else {
			break;
		}
	}
	return ret;
}

char SerialModem::get_char(char * &_scan) const
{
	char ch = *_scan;
	_scan++;
	return ch;
}

void SerialModem::reset(unsigned _type)
{
	m_cmdpos = 0;
	m_cmdbuf[0] = 0;
	m_prevcmd[0] = 0;
	m_flowcontrol = 0;
	m_plusinc = 0;
	m_dtrmode = DTR_HANG;

	memset(&m_reg, 0, sizeof(m_reg));
	m_reg[MREG_AUTOANSWER_COUNT] = 0;  // no autoanswer
	m_reg[MREG_RING_COUNT]       = 1;
	m_reg[MREG_ESCAPE_CHAR]      = '+';
	m_reg[MREG_CR_CHAR]          = '\r';
	m_reg[MREG_LF_CHAR]          = '\n';
	m_reg[MREG_BACKSPACE_CHAR]   = '\b';
	m_reg[MREG_GUARD_TIME]       = 50;
	m_reg[MREG_DTR_DELAY]        = 5;

	m_cmdpause = 0;
	m_echo = m_echo_after_reset;
	m_doresponse = 0; // all on
	m_terse_result = false; // verbose
	m_rescode_set = 4; // all results

	if(_type != DEVICE_SOFT_RESET) {
		m_rqueue.clear();
	}

	enter_idle_state();
}

void SerialModem::power_off()
{
	m_network->close_client();
	m_network->set_server_not_ready();
}

void SerialModem::enter_idle_state()
{
	PDEBUGF(LOG_V0, LOG_COM, "MODEM: entering idle state ...\n");

	// should not block
	m_network->close_client();
	m_network->clear_error();

	if(m_fx_enabled) {
		m_fx.silence();
	}

	m_ringing = false;
	m_dtrofftimer = -1;
	m_warmup_remain_ticks = 0;

	m_commandmode = true;

	set_MSR({
		true,  // CTS
		true,  // DSR
		false, // RI
		false  // DCD
	});

	m_tqueue.clear();

	// allow server connections
	m_network->set_server_ready();

	m_state = State::Idle;
}

void SerialModem::enter_connected_state()
{
	PDEBUGF(LOG_V0, LOG_COM, "MODEM: entering connected state ...\n");

	send_res_to_serial(ResCONNECT);
	m_bytes_ready = .0;
	m_commandmode = false;
	m_telClient = {};
	m_ringing = false;
	m_dtrofftimer = -1;
	set_MSR({
		m_MSR.CTS,
		m_MSR.DSR,
		false, // RI ring
		true   // DCD carrier detect
	});

	if(m_fx_enabled) {
		m_fx.silence();
	}

	m_warmup_remain_ticks = m_warmup_delay_ticks;

	m_state = State::Connected;
}

template <size_t N>
bool is_next_token(const char (&a)[N], const char *b) noexcept
{
	// Is 'b' at least as long as 'a'?
	constexpr size_t N_without_null = N - 1;
	if (strnlen(b, N) < N_without_null) {
		return false;
	}
	return (strncmp(a, b, N_without_null) == 0);
}

void SerialModem::do_command()
{
	m_cmdbuf[m_cmdpos] = 0;
	m_cmdpos = 0; // Reset for next command
	cstr_to_upper(m_cmdbuf);

	PINFOF(LOG_V1, LOG_COM, "MODEM: command: %s\n", m_cmdbuf);

	if(m_cmdbuf[0] == 'A' && m_cmdbuf[1] == '/') {
		// repeat last command
		std::memcpy(m_cmdbuf, m_prevcmd, MODEM_CMDBUF_SIZE);
		PINFOF(LOG_V1, LOG_COM, "MODEM: repeat: %s\n", m_cmdbuf);
	}

	// AT command set interpretation
	if ((m_cmdbuf[0] != 'A') || (m_cmdbuf[1] != 'T')) {
		send_res_to_serial(ResERROR);
		return;
	}

	std::memcpy(m_prevcmd, m_cmdbuf, MODEM_CMDBUF_SIZE);

	char *scanbuf = &m_cmdbuf[2];
	while(true) {

		char chr = get_char(scanbuf);
		switch (chr) {

		case ' ': // skip space
			break;

		// Multi-character AT-commands are prefixed with +
		// -----------------------------------------------
		// Note: successfully finding your multi-char command
		// requires moving the scanbuf position one beyond the
		// the last character in the multi-char sequence to ensure
		// single-character detection resumes on the next character.
		// Either break if successful or fail with send_res_to_serial(ResERROR)
		// and return (halting the command sequence all together).
		case '+': {
			// +NET1 enables telnet-mode and +NET0 disables it
			if (is_next_token("NET", scanbuf)) {
				// only walk the pointer ahead if the command matches
				scanbuf += 3;
				const uint32_t enabled = scan_number(scanbuf);
				// If the mode isn't valid then stop parsing
				if (enabled != 1 && enabled != 0) {
					send_res_to_serial(ResERROR);
					return;
				}
				m_telnet_mode = enabled;
				PINFOF(LOG_V1, LOG_COM, "MODEM: +NET, telnet-mode %s\n", m_telnet_mode ? "enabled" : "disabled");
				break;
			}
			// +SOCK1 enables enet.  +SOCK0 is TCP.
			// DOSBox specific
			if (is_next_token("SOCK", scanbuf)) {
				scanbuf += 4;
				PINFOF(LOG_V1, LOG_COM, "MODEM: unhandled command: +SOCK%d\n", scan_number(scanbuf));
				break;
			}
			// +WRM1 enables warmup delay
			// Drop all incoming and outgoing traffic for a short period after
			// answering a call. This is to simulate real modem behavior where
			// the first packet is usually bad (extra data in the buffer from
			// connecting, noise, random nonsense).
			// Some games are known to break without this.
			if (is_next_token("WRM", scanbuf)) {
				scanbuf += 3;
				const uint32_t enabled = scan_number(scanbuf);
				if (enabled != 1 && enabled != 0) {
					send_res_to_serial(ResERROR);
					return;
				}
				m_warmup_delay_ticks = MODEM_WARMUP_DELAY_TICKS * enabled;
				PINFOF(LOG_V1, LOG_COM, "MODEM: +WRM, %dms warmup delay %s\n", MODEM_WARMUP_DELAY_MS, enabled ? "enabled" : "disabled");
				break;
			}
			// If the command wasn't recognized then stop parsing
			send_res_to_serial(ResERROR);
			return;
		}

		case 'A': { // Answer call
			PINFOF(LOG_V1, LOG_COM, "MODEM: 'A', answer call\n");
			if(m_ringing) {
				accept_incoming_call();
				return;
			} else {
				// TODO? The modem does not wait for a ring on the line. If no
				// carrier is received after the wait specified in Register S7, the
				// modem returns to the command mode.
				send_res_to_serial(ResERROR);
				return;
			}
		}

		case 'B': { // BELL/CCITT Handshake Default
			PINFOF(LOG_V1, LOG_COM, "MODEM: 'B', BELL handshake %d (ignored)\n", scan_number(scanbuf));
			break;
		}

		case 'D': { // Dial
			char *foundstr = &scanbuf[0];
			if (*foundstr == 'T' || *foundstr == 'P') {
				foundstr++;
			}

			// Small protection against empty line or hostnames beyond the 253-char limit
			if ((!foundstr[0]) || (strlen(foundstr) > 253)) {
				PINFOF(LOG_V1, LOG_COM, "MODEM: 'D', dial (missing number)\n");
				send_res_to_serial(ResERROR);
				return;
			}
			// scan for and remove whitespaces
			foundstr = cstr_trim(foundstr);

			PINFOF(LOG_V1, LOG_COM, "MODEM: 'D', dial %s\n", foundstr);

			if(m_state != State::Idle) {
				PINFOF(LOG_V1, LOG_COM, "MODEM: The D command is not valid when the modem is on-line.\n");
				send_res_to_serial(ResERROR);
				return;
			}

			auto mappedaddr = m_phonebook.find(foundstr);
			if(mappedaddr != m_phonebook.end()) {
				dial(foundstr, mappedaddr->second.c_str());
				return;
			}

			// Large enough scope, so the buffers are still valid when reaching dial().
			char buffer[128];
			char obuffer[128];
			if (strlen(foundstr) >= 12) {
				// Check if supplied parameter only consists of digits
				bool isNum = true;
				size_t fl = strlen(foundstr);
				for (size_t i = 0; i < fl; i++) {
					if (foundstr[i] < '0' || foundstr[i] > '9') {
						isNum = false;
					}
				}
				if (isNum) {
					// Parameter is a number with at least 12 digits => this cannot
					// be a valid IP/name
					// Transform by adding dots
					size_t j = 0;
					size_t foundlen = strlen(foundstr);
					for (size_t i = 0; i < foundlen; i++) {
						buffer[j++] = foundstr[i];
						// Add a dot after the third, sixth and ninth number
						if (i == 2 || i == 5 || i == 8) {
							buffer[j++] = '.';
						}
						// If the string is longer than 12 digits,
						// interpret the rest as port
						if (i == 11 && strlen(foundstr) > 12) {
							buffer[j++] = ':';
						}
					}
					buffer[j] = 0;
					foundstr = buffer;

					// Remove Zeros from beginning of octets
					size_t k = 0;
					size_t foundlen2 = strlen(foundstr);
					for (size_t i = 0; i < foundlen2; i++) {
						if (i == 0 && foundstr[0] == '0') {
							continue;
						}
						if (i == 1 && foundstr[0] == '0' && foundstr[1] == '0') {
							continue;
						}
						if (foundstr[i] == '0' && foundstr[i-1] == '.') {
							continue;
						}
						if (foundstr[i] == '0' && foundstr[i-1] == '0' && foundstr[i-2] == '.') {
							continue;
						}
						obuffer[k++] = foundstr[i];
					}
					obuffer[k] = 0;
					foundstr = obuffer;
				}
			}
			dial(foundstr, nullptr);
			return;
		}

		case 'E': { // Echo on/off
			uint32_t num = scan_number(scanbuf);
			PINFOF(LOG_V1, LOG_COM, "MODEM: 'E', echo %u\n", num);
			switch(num) {
				case 0: m_echo = false; break;
				case 1: m_echo = true; break;
				default: break;
			}
			break;
		}

		case 'H': { // On/Off Hook
			uint32_t num = scan_number(scanbuf);
			PINFOF(LOG_V1, LOG_COM, "MODEM: 'H', hook %u\n", num);
			switch(num) {
				case 0:
					if(m_state == State::Originate || m_state == State::Connected) {
						send_res_to_serial(ResNOCARRIER);
						enter_idle_state();
						return;
					}
					break;
				default:
					break;
			}
			break;
		}

		case 'I': { // Information strings
			uint32_t num = scan_number(scanbuf);
			PINFOF(LOG_V1, LOG_COM, "MODEM: 'I', info %u\n", num);
			switch(num) {
				case 0: send_number_to_serial(MODEM_PRODUCT_CODE); break; // Display Product Code
				case 1: send_number_to_serial(MODEM_CHECKSUM); break; // Display ROM Checksum
				case 2: send_res_to_serial(ResOK); break; // Perform ROM Checksum
				case 3: send_line_to_serial("IBMulator Emulated Modem Firmware V1.00"); break;
				case 4: send_line_to_serial("Modem compiled for IBMulator " VERSION); break;
				default: break;
			}
			break;
		}

		case 'L': { // Volume
			uint32_t vol = scan_number(scanbuf);
			PINFOF(LOG_V1, LOG_COM, "MODEM: 'L', volume %u\n", vol);
			if(m_fx_enabled) {
				m_fx.set_volume(vol);
			}
			break;
		}

		case 'M': { // Monitor
			uint32_t mode = scan_number(scanbuf);
			PINFOF(LOG_V1, LOG_COM, "MODEM: 'M', speaker %u\n", mode);
			if(m_fx_enabled) {
				if(mode == 0) {
					m_fx.enable(false);
				} else {
					m_fx.enable(true);
				}
			}
			break;
		}

		case 'O': { // Return to data mode
			uint32_t num = scan_number(scanbuf);
			PINFOF(LOG_V1, LOG_COM, "MODEM: 'O', command mode %u\n", num);
			switch(num) {
				case 0:
					if(m_state == State::Connected) {
						m_commandmode = false;
					} else {
						send_res_to_serial(ResERROR);
						return;
					}
					break;
				default:
					break;
			}
			break;
		}

		case 'P': // Pulse Dial
			PINFOF(LOG_V1, LOG_COM, "MODEM: 'P', pulse dial (ignored)\n");
			break;

		case 'Q': {
			// Response options
			// 0 = all on, 1 = all off,
			// 2 = no ring and no connect/carrier in answer mode
			const uint32_t val = scan_number(scanbuf);
			PINFOF(LOG_V1, LOG_COM, "MODEM: 'Q', response %u\n", val);
			if(val < 3) {
				m_doresponse = val;
			} else {
				send_res_to_serial(ResERROR);
				return;
			}
			break;
		}

		case 'S': { // Registers
			const uint32_t index = scan_number(scanbuf);
			if (index >= MODEM_SREGS) {
				PINFOF(LOG_V1, LOG_COM, "MODEM: 'S', register %u (invalid)\n", index);
				send_res_to_serial(ResERROR);
				return;
			} else {
				while (scanbuf[0] == ' ') {
					scanbuf++; // skip spaces
				}
				if (scanbuf[0] == '=') {
					// set register
					scanbuf++;
					while (scanbuf[0] == ' ') {
						scanbuf++; // skip spaces
					}
					const uint32_t val = scan_number(scanbuf);
					m_reg[index] = val;
					PINFOF(LOG_V1, LOG_COM, "MODEM: 'S', set register %u = 0x%02x (%u)\n", index, val, val);
				} else if (scanbuf[0] == '?') {
					// get register
					PINFOF(LOG_V1, LOG_COM, "MODEM: 'S', get register %u = 0x%02x (%u)\n", index, m_reg[index], m_reg[index]);
					send_number_to_serial(m_reg[index]);
					scanbuf++;
				} else {
					PINFOF(LOG_V1, LOG_COM, "MODEM: 'S', register %u, unk. op.\n", index);
				}
			}
			break;
		}

		case 'T': // Tone Dial
			PINFOF(LOG_V1, LOG_COM, "MODEM: 'T', tone dial (ignored)\n");
			break;

		case 'V': { // Verbose/Terse Result Codes
			uint32_t num = scan_number(scanbuf);
			PINFOF(LOG_V1, LOG_COM, "MODEM: 'V', verbose %u\n", num);
			switch(num) {
				case 0: m_terse_result = true; break;
				case 1: m_terse_result = false; break;
				default: break;
			}
			break;
		}

		case 'X': { // Basic/Extended Result Code Set
			uint32_t num = scan_number(scanbuf);
			PINFOF(LOG_V1, LOG_COM, "MODEM: 'X', result code set %u\n", num);
			if(num <= 4) {
				m_rescode_set = num;
			} else {
				send_res_to_serial(ResERROR);
				return;
			}
			break;
		}

		case 'Y': { // Long Space Disconnect
			uint32_t num = scan_number(scanbuf);
			PINFOF(LOG_V1, LOG_COM, "MODEM: 'Y', long space disconnect %u (ignored)\n", num);
			break;
		}

		case 'Z': { // Reset and load profiles
			// scan the number away, if any
			PINFOF(LOG_V1, LOG_COM, "MODEM: 'Z', reset %u\n", scan_number(scanbuf));
			if(m_state == State::Connected) {
				send_res_to_serial(ResNOCARRIER);
			}
			reset(DEVICE_SOFT_RESET);
			return;
		}

		case '&': { // & escaped commands
			char cmdchar = get_char(scanbuf);
			switch(cmdchar) {
				case 'K': {
					const uint32_t val = scan_number(scanbuf);
					PINFOF(LOG_V1, LOG_COM, "MODEM: '&K', flow control %u\n", val);
					if (val == 0 || val == 1 || val == 3) {
						m_flowcontrol = val;
					} else {
						PWARNF(LOG_V0, LOG_COM, "MODEM: XON/XOFF flow control not supported\n");
						send_res_to_serial(ResERROR);
						return;
					}
					break;
				}
				case 'D': {
					const uint32_t val = scan_number(scanbuf);
					PINFOF(LOG_V1, LOG_COM, "MODEM: '&D', DTR mode %u\n", val);
					if (val < 4) {
						m_dtrmode = val;
					} else {
						send_res_to_serial(ResERROR);
						return;
					}
					break;
				}
				case '\0':
					// end of string
					send_res_to_serial(ResERROR);
					return;
				default:
					PINFOF(LOG_V1, LOG_COM, "MODEM: unhandled command: &%c%u\n", cmdchar, scan_number(scanbuf));
					break;
			}
			break;
		}

		case '\\': { // \ escaped commands
			char cmdchar = get_char(scanbuf);
			switch (cmdchar) {
				case 'N': {
					// error correction stuff - not emulated
					const uint32_t val = scan_number(scanbuf);
					PINFOF(LOG_V1, LOG_COM, "MODEM: '\\N', error correction %u (ignored)\n", val);
					if(val > 5) {
						send_res_to_serial(ResERROR);
						return;
					}
					break;
				}
				case '\0':
					// end of string
					send_res_to_serial(ResERROR);
					return;
				default:
					PINFOF(LOG_V1, LOG_COM, "MODEM: unhandled command: \\%c%u\n", cmdchar, scan_number(scanbuf));
					break;
			}
			break;
		}

		case '\0':
			// end of command
			send_res_to_serial(ResOK);
			return;

		default:
			PINFOF(LOG_V1, LOG_COM, "MODEM: unhandled command: %c%u\n", chr, scan_number(scanbuf));
			break;
		}
	}
}

void SerialModem::telnet_emulation(uint8_t *data, uint32_t size)
{
	for(uint32_t i = 0; i < size; i++) {
		uint8_t c = data[i];
		if (m_telClient.inIAC) {
			if (m_telClient.recCommand) {
				if ((c != 0) && (c != 1) && (c != 3)) {
					PDEBUGF(LOG_V0, LOG_COM, "MODEM: telnet: unhandled option %u\n", c);
					if (m_telClient.command > 250) {
						// Reject anything we don't recognize
						m_tqueue.write(0xff);
						m_tqueue.write(252);
						m_tqueue.write(c); // Won't do 'c'
					}
				}
				switch (m_telClient.command) {
					case 251: // Will
						if (c == 0) m_telClient.binary[TEL_SERVER] = true;
						if (c == 1) m_telClient.echo[TEL_SERVER] = true;
						if (c == 3) m_telClient.supressGA[TEL_SERVER] = true;
						break;
					case 252: // Won't
						if (c == 0) m_telClient.binary[TEL_SERVER] = false;
						if (c == 1) m_telClient.echo[TEL_SERVER] = false;
						if (c == 3) m_telClient.supressGA[TEL_SERVER] = false;
						break;
					case 253: // Do
						if (c == 0) {
							m_telClient.binary[TEL_CLIENT] = true;
							m_tqueue.write(0xff);
							m_tqueue.write(251);
							m_tqueue.write(0); // Will do binary transfer
						}
						if (c == 1) {
							m_telClient.echo[TEL_CLIENT] = false;
							m_tqueue.write(0xff);
							m_tqueue.write(252);
							m_tqueue.write(1); // Won't echo (too lazy)
						}
						if (c == 3) {
							m_telClient.supressGA[TEL_CLIENT] = true;
							m_tqueue.write(0xff);
							m_tqueue.write(251);
							m_tqueue.write(3); // Will Suppress GA
						}
						break;
					case 254: // Don't
						if (c == 0) {
							m_telClient.binary[TEL_CLIENT] = false;
							m_tqueue.write(0xff);
							m_tqueue.write(252);
							m_tqueue.write(0); // Won't do binary transfer
						}
						if (c == 1) {
							m_telClient.echo[TEL_CLIENT] = false;
							m_tqueue.write(0xff);
							m_tqueue.write(252);
							m_tqueue.write(1); // Won't echo (fine by me)
						}
						if (c == 3) {
							m_telClient.supressGA[TEL_CLIENT] = true;
							m_tqueue.write(0xff);
							m_tqueue.write(251);
							m_tqueue.write(3); // Will Suppress GA (too lazy)
						}
						break;
					default:
						PDEBUGF(LOG_V0, LOG_COM, "MODEM: telnet client sent IAC %u\n", m_telClient.command);
						break;
				}
				m_telClient.inIAC = false;
				m_telClient.recCommand = false;
				continue;
			} else {
				if (c == 249) {
					// Go Ahead received
					m_telClient.inIAC = false;
					continue;
				}
				m_telClient.command = c;
				m_telClient.recCommand = true;

				if ((m_telClient.binary[TEL_SERVER]) && (c == 0xff)) {
					// Binary data with value of 255
					m_telClient.inIAC = false;
					m_telClient.recCommand = false;
					c = 0xff;
					m_rqueue.write(c);
					if(m_dump_file.is_open()) {
						m_dump_file.write((const char*)(&c), 1);
					}
					continue;
				}
			}
		} else {
			if (c == 0xff) {
				m_telClient.inIAC = true;
				continue;
			}
			m_rqueue.write(c);
			if(m_dump_file.is_open()) {
				m_dump_file.write((const char*)(&c), 1);
			}
		}
	}
}

void SerialModem::echo(uint8_t ch)
{
	if(m_echo) {
		char buf[2] = {char(ch), 0};
		PDEBUGF(LOG_V1, LOG_COM, "MODEM: echo '%s'\n", str_format_special(buf).c_str());
		m_rqueue.write(ch);
	}
}

bool SerialModem::serial_read_byte(uint8_t *_byte)
{
	return (m_rqueue.read(_byte) == 1);
}

bool SerialModem::serial_write_byte(uint8_t _byte)
{
	if(m_tqueue.write(_byte) != 1) {
		PWARNF(LOG_V2, LOG_COM, "MODEM: serial tx overflow!\n");
		return false;
	}
	if(m_tqueue.get_write_avail() < MODEM_BUFFER_CTS_THRESHOLD && m_flowcontrol != 0) {
		set_CTS(false);
	}
	return true;
}

void SerialModem::timer(uint64_t _time)
{
	if(m_state == State::Originate) {
		if(m_dial.time && _time >= m_dial.time) {
			if(m_network->get_error() != NetService::Error::NoError) {
				PERRF(LOG_COM, "MODEM: connection failed.\n");
				if(m_network->get_error() == NetService::Error::HostRefused) {
					send_res_to_serial(ResBUSY);
				} else {
					send_res_to_serial(ResNOCARRIER);
				}
				enter_idle_state();
			} else {
				PINFOF(LOG_V0, LOG_COM, "MODEM: connecting to host %s:%u\n", m_dial.host.c_str(), m_dial.port);
				try {
					m_network->open(
						m_dial.host.c_str(), m_dial.port,
						NetService::Mode::ClientAsync,
						m_conn_timeout_ms
					);
				} catch(std::runtime_error &e) {
					PERRF(LOG_COM, "MODEM: dial failed: %s\n", e.what());
					send_res_to_serial(ResNOCARRIER);
					enter_idle_state();
				}
			}
			m_dial.time = 0;
		} else {
			if(m_network->is_connected()) {
				// enter_connected_state();
				enter_handshaking_state();
			} else if(!m_dial.time && m_network->get_error() != NetService::Error::NoError) {
				m_dial.time = g_machine.get_virt_time_ns();
				if(m_fx_enabled) {
					switch(m_network->get_error()) {
						case NetService::Error::NoRoute:
						case NetService::Error::HostDown:
							m_dial.time += m_fx.reorder();
							break;
						default:
							m_dial.time += m_fx.busy();
							break;
					}
				}
			}
		}
	}

	// Check for eventual break command
	if (!m_commandmode) {
		m_cmdpause++;
		uint32_t guard_threashold = uint32_t(m_reg[MREG_GUARD_TIME] * 20 / MODEM_TICKTIME_MS);
		if (m_cmdpause > guard_threashold) {
			if (m_plusinc == 0) {
				m_plusinc = 1;
			} else if (m_plusinc == 4) {
				PDEBUGF(LOG_V0, LOG_COM, "MODEM: entering command mode (escape sequence).\n");
				m_commandmode = true;
				send_res_to_serial(ResOK);
				m_plusinc = 0;
			}
		}
	}

	size_t bytesready = m_bytes_ready;
	if(bytesready) {
		m_bytes_ready -= bytesready;
	}
	m_bytes_ready += m_bytes_per_tick;

	if(bytesready) {

		// Handle incoming data from the serial port
		size_t txbytes = bytesready;
		uint32_t txbuffersize = 0;
		uint8_t txval;
		while(txbytes-- && m_tqueue.read(&txval)) {
			if (m_commandmode) {
				if (m_cmdpos < 2) {
					// Ignore everything until we see "AT" sequence.
					if (m_cmdpos == 0 && toupper(txval) != 'A') {
						continue;
					}
					if (m_cmdpos == 1 && toupper(txval) != 'T') {
						echo(m_reg[MREG_BACKSPACE_CHAR]);
						m_cmdpos = 0;
						continue;
					}
				} else {
					// Now entering command.
					if (txval == m_reg[MREG_BACKSPACE_CHAR]) {
						if (m_cmdpos > 2) {
							echo(txval);
							m_cmdpos--;
						}
						continue;
					}
					if (txval == m_reg[MREG_LF_CHAR]) {
						continue; // Real modem doesn't seem to skip this?
					}
					if (txval == m_reg[MREG_CR_CHAR]) {
						echo(txval);
						do_command();
						continue;
					}
				}
				if (m_cmdpos < 99) {
					echo(txval);
					m_cmdbuf[m_cmdpos] = txval;
					m_cmdpos++;
				}
			} else {
				if(m_state != State::Connected) {
					PDEBUGF(LOG_V0, LOG_COM, "MODEM: receiving non-command data from serial while disconnected\n");
				}
				if (m_plusinc >= 1 && m_plusinc <= 3 && txval == m_reg[MREG_ESCAPE_CHAR]) {
					// +
					m_plusinc++;
				} else {
					m_plusinc = 0;
				}
				m_cmdpause = 0;
				m_tmpbuf[txbuffersize] = txval;
				txbuffersize++;
			}
		}

		if(m_state == State::Connected && txbuffersize && m_warmup_remain_ticks == 0) {
			// down here it saves a lot of network traffic
			if(m_network->tx_fifo().write(m_tmpbuf, txbuffersize) != txbuffersize) {
				send_res_to_serial(ResNOCARRIER);
				PDEBUGF(LOG_V0, LOG_COM, "MODEM: No carrier on send\n");
				enter_idle_state();
			}
		}

		// Handle incoming data to the serial port
		if(m_state == State::Connected && !m_commandmode) {
			size_t maxsize = m_rqueue.get_write_avail() >= 16 ? 16 : m_rqueue.get_write_avail();
			maxsize = std::min(maxsize, bytesready);
			size_t usesize = m_network->rx_fifo().pop(m_tmpbuf, maxsize);
			if(usesize) {
				PDEBUGF(LOG_V3, LOG_COM, "MODEM: net read: %u bytes\n", usesize);
				if(m_warmup_remain_ticks == 0) {
					// Filter telnet commands
					if(m_telnet_mode) {
						telnet_emulation(m_tmpbuf, usesize);
					} else {
						m_rqueue.write(m_tmpbuf, usesize);
						if(m_dump_file.is_open()) {
							m_dump_file.write((const char*)m_tmpbuf, usesize);
						}
					}
				}
			} else if(!m_network->is_connected() && m_network->rx_fifo().was_empty()) {
				send_res_to_serial(ResNOCARRIER);
				PDEBUGF(LOG_V0, LOG_COM, "MODEM: No carrier on receive\n");
				enter_idle_state();
			}
		}

		if(!m_MSR.CTS && m_tqueue.get_write_avail() >= MODEM_BUFFER_CTS_THRESHOLD && m_flowcontrol != 0) {
			set_CTS(true);
		}
	}

	// Tick down warmup timer
	if(m_state == State::Connected && m_warmup_remain_ticks) {
		// Drop all incoming and outgoing traffic for a short period after
		// answering a call. This is to simulate real modem behavior where
		// the first packet is usually bad (extra data in the buffer from
		// connecting, noise, random nonsense).
		// Some games are known to break without this.
		m_warmup_remain_ticks--;
	}

	// Check for incoming calls
	if(m_state == State::Idle && m_network->has_server_accepted() && !m_ringing) {
		if(!m_MCR.DTR && m_dtrmode != DTR_IGNORE) {
			// accept no calls with DTR off
			PDEBUGF(LOG_V0, LOG_COM, "MODEM: DTR off, drop incoming call\n");
			m_network->close_client(true);
			enter_idle_state();
		} else {
			m_ringing = true;
			send_res_to_serial(ResRING);
			set_RI(!m_MSR.RI);
			if(m_fx_enabled) {
				m_fx.incoming();
			}
			m_ringtimer = MODEM_RINGINTERVAL_TICKS;
			m_reg[MREG_RING_COUNT] = 0; // Reset ring counter reg
		}
	}
	if(m_ringing) {
		if(m_ringtimer <= 0) {
			m_reg[MREG_RING_COUNT]++;
			if(!m_network->is_connected() || m_reg[MREG_RING_COUNT] >= MODEM_RINGING_MAX) {
				if(!m_network->is_connected()) {
					PDEBUGF(LOG_V0, LOG_COM, "MODEM: incoming connection dropped before answer\n");
				} else {
					PDEBUGF(LOG_V0, LOG_COM, "MODEM: answer timeout\n");
				}
				enter_idle_state();
				return;
			} else if((m_reg[MREG_AUTOANSWER_COUNT] > 0) && (m_reg[MREG_RING_COUNT] >= m_reg[MREG_AUTOANSWER_COUNT])) {
				PDEBUGF(LOG_V0, LOG_COM, "MODEM: answering incoming call ...\n");
				accept_incoming_call();
				return;
			}
			send_res_to_serial(ResRING);
			set_RI(!m_MSR.RI);
			m_ringtimer = MODEM_RINGINTERVAL_TICKS;
		}
		m_ringtimer--;
	}

	if(m_state == State::Handshaking && _time >= m_accept_time) {
		enter_connected_state();
	}

	// Handle DTR drop
	if(!m_MCR.DTR) {
		if(m_dtrofftimer == 0) {
			switch(m_dtrmode) {
				case DTR_IGNORE:
					// Do nothing.
					break;
				case DTR_COMMAND:
					if(m_state == State::Connected) {
						// Go back to command mode.
						// If in the on-line state, DTR ON-to-OFF signals the modem to exit
						// the on-line state, issue an OK result code at the response speed,
						// and go to command state, while maintaining the connection.
						PDEBUGF(LOG_V0, LOG_COM, "MODEM: entering command mode due to dropped DTR.\n");
						m_commandmode = true;
						send_res_to_serial(ResOK);
					}
					break;
				case DTR_HANG:
					if(m_state != State::Idle) {
						// Hang up.
						// If in the on-line state, or in the handshaking, dialing, or
						// answer process, DTR ON-to-OFF signals the modem to execute the
						// hangup process, issue an OK result code at the response speed,
						// and go to the idle condition.
						PDEBUGF(LOG_V0, LOG_COM, "MODEM: hanging up due to dropped DTR.\n");
						send_res_to_serial(ResOK);
						enter_idle_state();
					}
					break;
				case DRT_RESET:
					// Reset.
					// DTR ON-to-OFF signals the modem to immediately perform a hard
					// reset regardless of state. All processes are aborted. S25 does not
					// affect the modem's reactions to DTR going OFF-to-ON. There is no
					// result code.
					PDEBUGF(LOG_V0, LOG_COM, "MODEM: resetting due to dropped DTR.\n");
					reset(DEVICE_SOFT_RESET);
					break;
				default:
					PDEBUGF(LOG_V0, LOG_COM, "MODEM: invalid dtrmode (%u).\n", m_dtrmode);
					break;
			}
		}

		// Set the timer to -1 once it's expired to turn it off.
		if(m_dtrofftimer >= 0) {
			m_dtrofftimer--;
		}
	}
}

void SerialModem::set_MCR(const ModemControl &_mcr)
{
	if(m_MCR.DTR != _mcr.DTR) {
		if(!_mcr.DTR) {
			// Start the timer upon losing DTR (S25 stores time in 1/100s of a second).
			m_dtrofftimer = m_reg[MREG_DTR_DELAY] * 10 / MODEM_TICKTIME_MS;
		} else {
			m_dtrofftimer = -1;
		}
	}
	m_MCR = _mcr;
}

