/*
 * 	Copyright (c) 2001-2014  The Bochs Project
 * 	Copyright (c) 2015  Marco Bortolin
 *
 *	This file is part of IBMulator
 *
 *  IBMulator is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 3 of the License, or
 *	(at your option) any later version.
 *
 *	IBMulator is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with IBMulator.  If not, see <http://www.gnu.org/licenses/>.
 */

// Peter Grehan (grehan@iprg.nokia.com) coded the original version of this
// serial emulation. He implemented a single 8250, and allow terminal
// input/output to stdout on FreeBSD.
// The current version emulates up to 4 UART 16550A with FIFO. Terminal
// input/output now works on some more platforms.

/* WARNING
 * on the PS/1 2011 there's only 1 serial port and it's controlled by POS register 2.
 * I'll keep the Bochs' general structure (I don't want to introduce new bugs)
 * but it now support ONLY 1 UART
 */

#include "ibmulator.h"
#include "program.h"
#include "machine.h"
#include "hardware/devices.h"
#include "hardware/devices/systemboard.h"
#include "gui/gui.h"
#include "pic.h"
#include "serial.h"

#if SER_POSIX
	#include <sys/types.h>
	#include <sys/socket.h>
	#include <netinet/in.h>
	#include <netdb.h>
	#include <unistd.h>
	#define closesocket(s) ::close(s)
#endif
#if SER_WIN32
	//#include <winioctl.h>
	//#include <io.h>
	#if !defined(FILE_FLAG_FIRST_PIPE_INSTANCE)
		#define FILE_FLAG_FIRST_PIPE_INSTANCE 0
	#endif
#endif
#include <sys/stat.h>
#include <fcntl.h>


#if USE_RAW_SERIAL
	#include "serial_raw.h"
#endif

#include <cstring>
#include <functional>
using namespace std::placeholders;

Serial g_serial;

uint16_t Serial::ms_ports[SERIAL_MAXDEV] = { 0x3F8, 0x2F8 };
uint8_t Serial::ms_irqs[SERIAL_MAXDEV] = { 4, 3 };

Serial::Serial(void)
{
	for(int i=0; i<SERIAL_MAXDEV; i++) {
		memset(&m_s[i], 0, sizeof(serial_t));
		m_s[i].io_mode = SER_MODE_NULL;
		m_s[i].tty_id = -1;
		m_s[i].tx_timer_index = NULL_TIMER_HANDLE;
		m_s[i].rx_timer_index = NULL_TIMER_HANDLE;
		m_s[i].fifo_timer_index = NULL_TIMER_HANDLE;
	}
}

Serial::~Serial(void)
{
	for(int i=0; i<SERIAL_MAXDEV; i++) {
		if(!m_s[i].enabled) {
			continue;
		}
		switch (m_s[i].io_mode) {
			case SER_MODE_FILE:
				if(m_s[i].output != NULL) {
					fclose(m_s[i].output);
				}
				break;
			case SER_MODE_TERM:
				#if SERIAL_ENABLE && !SER_WIN32
				if(m_s[i].tty_id >= 0) {
					tcsetattr(m_s[i].tty_id, TCSAFLUSH, &m_s[i].term_orig);
				}
				#endif
				break;
			case SER_MODE_RAW:
				#if USE_RAW_SERIAL
				delete[] m_s[i].raw;
				#endif
				break;
			case SER_MODE_SOCKET_CLIENT:
			case SER_MODE_SOCKET_SERVER:
				if(m_s[i].socket_id >= 0) {
					closesocket(m_s[i].socket_id);
				}
				break;
			case SER_MODE_PIPE_CLIENT:
			case SER_MODE_PIPE_SERVER:
				#if SER_WIN32
				if(m_s[i].pipe) {
					CloseHandle(m_s[i].pipe);
				}
				#endif
				break;
		}
	}
}


static ini_enum_map_t serial_modes = {
	{ "null", SER_MODE_NULL },
	{ "file", SER_MODE_FILE },
	{ "term", SER_MODE_TERM },
	{ "raw", SER_MODE_RAW },
	{ "mouse", SER_MODE_MOUSE },
	{ "socket-client", SER_MODE_SOCKET_CLIENT },
	{ "socket-server", SER_MODE_SOCKET_SERVER },
	{ "pipe-client", SER_MODE_PIPE_CLIENT },
	{ "pipe-server", SER_MODE_PIPE_SERVER }
};


void Serial::init()
{
	//controlled by POS reg 2 bit 2
	m_enabled = g_program.config().get_bool(COM_SECTION, COM_ENABLED);

	detect_mouse = 0;
	mouse_port = -1;
	mouse_type = MOUSE_TYPE_NONE;

	char pname[10];

	/* WARNING
	 * on the PS/1 2011 there's only 1 serial port and it's controlled by POS register 2.
	 * I'll keep the Bochs' structure (array of serial_t) just because I don't
	 * want to introduce new bugs.
	 * USE ONLY m_s[0]
	 */

	const unsigned i = 0;
	sprintf(pname, "COM%d", i+1);

	m_s[i].enabled = true; // a serial port is always defined

	/* serial interrupt */
	m_s[i].IRQ = ms_irqs[i];
	g_machine.register_irq(m_s[i].IRQ, pname);

	if(m_s[i].tx_timer_index == NULL_TIMER_HANDLE) {
		m_s[i].tx_timer_index =
				g_machine.register_timer(
						std::bind(&Serial::tx_timer, this, i), 0,
						false,false, // one-shot, inactive
						"COM.tx");
	}

	if(m_s[i].rx_timer_index == NULL_TIMER_HANDLE) {
		m_s[i].rx_timer_index =
				g_machine.register_timer(
						std::bind(&Serial::rx_timer, this, i), 0,
						false,false,  // one-shot, inactive
						"COM.rx");
	}
	if(m_s[i].fifo_timer_index == NULL_TIMER_HANDLE) {
		m_s[i].fifo_timer_index =
				g_machine.register_timer(
						std::bind(&Serial::fifo_timer, this, i), 0,
						false,false,  // one-shot, inactive
						"COM.fifo");
	}

	for(uint addr = ms_ports[i]; addr < (uint)(ms_ports[i] + 8); addr++) {
		PDEBUGF(LOG_V2, LOG_COM, "%s initialize register for read/write: 0x%04X\n", pname, addr);
		if(addr < (uint)(ms_ports[i] + 7)) {
			g_devices.register_read_handler(this, addr, 3);
			g_devices.register_write_handler(this, addr, 3);
		} else {
			g_devices.register_read_handler(this, addr, 1);
			g_devices.register_write_handler(this, addr, 1);
		}
	}

	m_s[i].io_mode = SER_MODE_NULL;
	uint8_t mode = g_program.config().get_enum(COM_SECTION, COM_MODE, serial_modes);
	std::string dev = g_program.config().get_string(COM_SECTION, COM_DEV);

	switch(mode) {
		case SER_MODE_FILE:
			init_mode_file(i, dev);
			break;
		case SER_MODE_TERM:
			init_mode_term(i, dev);
			break;
		case SER_MODE_RAW:
			init_mode_raw(i, dev);
			break;
		case SER_MODE_MOUSE:
			init_mode_mouse(i);
			break;
		case SER_MODE_SOCKET_CLIENT:
		case SER_MODE_SOCKET_SERVER:
			init_mode_socket(i, dev, mode);
			break;
		case SER_MODE_PIPE_CLIENT:
		case SER_MODE_PIPE_SERVER:
			init_mode_pipe(i, dev, mode);
			break;
		case SER_MODE_NULL:
			break;
		default:
			PERRF_ABORT(LOG_COM, "unknown serial i/o mode %d\n", mode);
			break;
	}


	PINFOF(LOG_V0, LOG_COM, "%s at 0x%04x, irq %d (mode: %s)\n", pname,
			ms_ports[i], m_s[i].IRQ,
			g_program.config().get_string(COM_SECTION, COM_MODE).c_str());


	if((mouse_type == MOUSE_TYPE_SERIAL) ||
		(mouse_type == MOUSE_TYPE_SERIAL_WHEEL) ||
		(mouse_type == MOUSE_TYPE_SERIAL_MSYS)) {

		g_machine.register_mouse_fun(
			std::bind(&Serial::mouse_motion, this, _1, _2, _3, _4)
		);
	}
}

void Serial::config_changed()
{
	//TODO
}

void Serial::save_state(StateBuf &_state)
{
	PINFOF(LOG_V1, LOG_COM, "saving state (not implemented)\n");

	StateHeader h;
	h.name = get_name();
	h.data_size = sizeof(m_s);
	_state.write(&m_s,h);
}

void Serial::restore_state(StateBuf &_state)
{
	PINFOF(LOG_V1, LOG_COM, "restoring state (not implemented)\n");

	//TODO restoring the serial state is a lot more than just copy the m_s struct.
	//for the time being, don't save the machine state while using the serial port
	//state_header_t h;
	//h.size = sizeof(m_s);
	//_state.read(&m_s,h);
	_state.skip();
}

void Serial::init_mode_file(uint comn, std::string dev)
{
	if(!dev.empty()) {
		// tx_timer() opens the output file on demand
		m_s[comn].io_mode = SER_MODE_FILE;
	}
}

void Serial::init_mode_term(uint comn, std::string dev)
{
	if(!SERIAL_ENABLE || SER_WIN32) {
		PERRF_ABORT(LOG_COM, "serial terminal support not available\n");
	}
#if !SER_WIN32
	if(dev.empty())
		return;

	m_s[comn].tty_id = ::open(dev.c_str(), O_RDWR|O_NONBLOCK, 600);
	if(m_s[comn].tty_id < 0) {
		PERRF_ABORT(LOG_COM, "open of COM%d (%s) failed\n", comn+1, dev.c_str());
	} else {
		m_s[comn].io_mode = SER_MODE_TERM;
		PDEBUGF(LOG_V2, LOG_COM, "COM%d tty_id: %d\n", comn+1, m_s[comn].tty_id);
		tcgetattr(m_s[comn].tty_id, &m_s[comn].term_orig);
		memcpy(&m_s[comn].term_orig, &m_s[comn].term_new, sizeof(struct termios));
		m_s[comn].term_new.c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP|INLCR|IGNCR|ICRNL|IXON);
		m_s[comn].term_new.c_lflag &= ~(ECHO|ECHONL|ICANON|ISIG|IEXTEN);
		m_s[comn].term_new.c_cflag &= ~(CSIZE|PARENB);
		m_s[comn].term_new.c_cflag |= CS8;
		m_s[comn].term_new.c_oflag |= OPOST | ONLCR;  // Enable NL to CR-NL translation
		if(!TRUE_CTLC) {
			// ctl-C will exit the emulator
			m_s[comn].term_new.c_iflag &= ~IGNBRK;
			m_s[comn].term_new.c_iflag |= BRKINT;
			m_s[comn].term_new.c_lflag |= ISIG;
		} else {
			// ctl-C will be delivered to the serial comn
			m_s[comn].term_new.c_iflag |= IGNBRK;
			m_s[comn].term_new.c_iflag &= ~BRKINT;
		}
		m_s[comn].term_new.c_iflag = 0;
		m_s[comn].term_new.c_oflag = 0;
		m_s[comn].term_new.c_cflag = CS8|CREAD|CLOCAL;
		m_s[comn].term_new.c_lflag = 0;
		m_s[comn].term_new.c_cc[VMIN] = 1;
		m_s[comn].term_new.c_cc[VTIME] = 0;
		//m_s[comn].term_new.c_iflag |= IXOFF;
		tcsetattr(m_s[comn].tty_id, TCSAFLUSH, &m_s[comn].term_new);
	}
#endif
}

void Serial::init_mode_raw(uint, std::string)
{
	#if USE_RAW_SERIAL
		m_s[comn].raw = new serial_raw(dev.c_str());
		m_s[comn].io_mode = SER_MODE_RAW;
	#else
		PERRF_ABORT(LOG_COM, "raw serial support not present\n");
	#endif
}

void Serial::init_mode_mouse(uint comn)
{
	m_s[comn].io_mode = SER_MODE_MOUSE;
	mouse_port = comn;
	mouse_type = g_program.config().get_enum(GUI_SECTION, GUI_MOUSE_TYPE, g_mouse_types);
}

void Serial::set_port(uint8_t _port)
{
	_port %= 2;
	_port = !bool(_port); // if _port==1 then is COM1, if _port==0 then COM2, so invert

	if(m_s[0].port != _port) {
		char pname[5];
		sprintf(pname, "COM%d", _port+1);
		m_s[0].port = _port;
		g_machine.unregister_irq(m_s[0].IRQ);
		m_s[0].IRQ = ms_irqs[_port];
		g_machine.register_irq(m_s[0].IRQ, pname);
		PINFOF(LOG_V0, LOG_COM, "Serial port at 0x%04X (%s), irq %d\n",
				ms_ports[m_s[0].port], pname, m_s[0].IRQ);
	}
}

void Serial::set_enabled(bool _enabled)
{
	if(_enabled != m_enabled) {
		PINFOF(LOG_V1, LOG_COM, "Serial port %s\n", _enabled?"ENABLED":"DISABLED");
		m_enabled = _enabled;
		if(!_enabled) {
			reset(DEVICE_SOFT_RESET);
		}
	}
}

void Serial::init_mode_socket(uint comn, std::string dev, uint mode)
{
	m_s[comn].io_mode = mode;
	struct sockaddr_in sin;
	struct hostent *hp;
	char host[PATHNAME_LEN];
	int port;
	SOCKET socket;
	bool server = (mode == SER_MODE_SOCKET_SERVER);

	#if SER_WIN32
		static bool winsock_init = false;
		if(!winsock_init) {
			WORD wVersionRequested;
			WSADATA wsaData;
			int err;
			wVersionRequested = MAKEWORD(2, 0);
			err = WSAStartup(wVersionRequested, &wsaData);
			if(err != 0)
				PERRF_ABORT(LOG_COM, "WSAStartup failed\n");
			winsock_init = true;
		}
	#endif

	strcpy(host, dev.c_str());
	char *substr = strtok(host, ":");
	substr = strtok(NULL, ":");
	if(!substr) {
		PERRF_ABORT(LOG_COM, "COM%d: inet address is wrong (%s)\n", comn+1, dev.c_str());
	}
	port = atoi(substr);

	hp = gethostbyname(host);
	if(!hp) {
		PERRF_ABORT(LOG_COM, "COM%d: gethostbyname failed (%s)\n", comn+1, host);
	}

	memset ((char*) &sin, 0, sizeof (sin));
	#if HAVE_SOCKADDR_IN_SIN_LEN
		sin.sin_len = sizeof sin;
	#endif
	memcpy ((char*) &(sin.sin_addr), hp->h_addr, hp->h_length);
	sin.sin_family = hp->h_addrtype;
	sin.sin_port = htons (port);

	socket = ::socket (AF_INET, SOCK_STREAM, 0);
	if(socket < 0)
		PERRF_ABORT(LOG_COM, "COM%d: socket() failed\n",comn+1);

	// server mode
	if(server) {
		if(::bind (socket, (sockaddr *) &sin, sizeof (sin)) < 0 || ::listen (socket, SOMAXCONN) < 0) {
			closesocket(socket);
			socket = (SOCKET) -1;
			PERRF_ABORT(LOG_COM, "COM%d: bind() or listen() failed (host:%s, port:%d)\n",comn+1, host, port);
		} else {
			PINFOF(LOG_V0, LOG_COM, "COM%d: waiting for client to connect (host:%s, port:%d)\n",comn+1, host, port);
			SOCKET client;
			if((client = ::accept (socket, NULL, 0)) < 0) {
				PERRF_ABORT(LOG_COM, "COM%d: accept() failed (host:%s, port:%d)\n",comn+1, host, port);
			}
			closesocket(socket);
			socket = client;
		}
	} else if(::connect (socket, (sockaddr *) &sin, sizeof (sin)) < 0) { // client mode
		closesocket(socket);
		socket = (SOCKET) -1;
		PERRF(LOG_COM, "COM%d: connect() failed (host:%s, port:%d)\n",comn+1, host, port);
	}

	m_s[comn].socket_id = socket;
	if(socket > 0) {
		PINFOF(LOG_V0, LOG_COM, "com%d - inet %s - socket_id: %d, ip:%s, port:%d\n",
				comn+1, server ? "server" : "client", socket, host, port);
	}
}

void Serial::init_mode_pipe(uint /*comn*/, std::string dev, uint mode)
{
	if(dev.empty()) {
		return;
	}

	bool server = (mode == SER_MODE_PIPE_SERVER);
	#if SER_WIN32
		HANDLE pipe;
		m_s[comn].io_mode = mode;
		// server mode
		if(server) {
			pipe = CreateNamedPipe( dev.c_str(),
					PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
					PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
					1, 4096, 4096, 0, NULL);

			if(pipe == INVALID_HANDLE_VALUE) {
				PERRF_ABORT(LOG_COM, "COM%d: CreateNamedPipe(%s) failed\n", comn+1, dev.c_str());
			}
			PINFOF(LOG_V0, LOG_COM, "COM%d: waiting for client to connect to %s\n", comn+1, dev.c_str());
			if(!ConnectNamedPipe(pipe, NULL) && GetLastError() != ERROR_PIPE_CONNECTED) {
				CloseHandle(pipe);
				pipe = INVALID_HANDLE_VALUE;
				PERRF_ABORT(LOG_COM, "COM%d: ConnectNamedPipe(%s) failed\n", comn+1, dev.c_str());
			}
		} else { // client mode
			pipe = CreateFile( dev.c_str(), GENERIC_READ | GENERIC_WRITE,
					0, NULL, OPEN_EXISTING, 0, NULL);

			if(pipe == INVALID_HANDLE_VALUE) {
				PERRF(LOG_COM, "COM%d: failed to open pipe %s", comn+1, dev.c_str());
			}
		}

		if(pipe != INVALID_HANDLE_VALUE) {
			m_s[comn].pipe = pipe;
		}
	#else
		PERRF_ABORT(LOG_COM, "support for serial mode 'pipe-%s' not available\n", server?"server":"client");
	#endif
}

void Serial::lower_interrupt(uint8_t port)
{
	/* If there are no more ints pending, clear the irq */
	if((m_s[port].rx_interrupt == 0) &&
		(m_s[port].tx_interrupt == 0) &&
		(m_s[port].ls_interrupt == 0) &&
		(m_s[port].ms_interrupt == 0) &&
		(m_s[port].fifo_interrupt == 0))
	{
		g_pic.lower_irq(m_s[port].IRQ);
	}
}

void Serial::reset(uint)
{
	mouse_internal_buffer.num_elements = 0;
	for(uint i=0; i<MOUSE_BUFF_SIZE; i++)
		mouse_internal_buffer.buffer[i] = 0;
	mouse_internal_buffer.head = 0;
	mouse_delayed_dx = 0;
	mouse_delayed_dy = 0;
	mouse_delayed_dz = 0;
	mouse_buttons = 0;
	mouse_update = 0;

	/*
	* Put the UART registers into their RESET state
	*/
	//for(uint i=0; i<N_SERIAL_PORTS; i++) {
	const uint i = 0;
		/*
		if(!m_s[i].enabled) {
			continue;
		}
		*/
		/* internal state */
		m_s[i].ls_ipending = 0;
		m_s[i].ms_ipending = 0;
		m_s[i].rx_ipending = 0;
		m_s[i].fifo_ipending = 0;
		m_s[i].ls_interrupt = 0;
		m_s[i].ms_interrupt = 0;
		m_s[i].rx_interrupt = 0;
		m_s[i].tx_interrupt = 0;
		m_s[i].fifo_interrupt = 0;

		/* int enable: b0000 0000 */
		m_s[i].int_enable.rxdata_enable = 0;
		m_s[i].int_enable.txhold_enable = 0;
		m_s[i].int_enable.rxlstat_enable = 0;
		m_s[i].int_enable.modstat_enable = 0;

		/* int ID: b0000 0001 */
		m_s[i].int_ident.ipending = 1;
		m_s[i].int_ident.int_ID = 0;

		/* FIFO control: b0000 0000 */
		m_s[i].fifo_cntl.enable = 0;
		m_s[i].fifo_cntl.rxtrigger = 0;
		m_s[i].rx_fifo_end = 0;
		m_s[i].tx_fifo_end = 0;

		/* Line Control reg: b0000 0000 */
		m_s[i].line_cntl.wordlen_sel = 0;
		m_s[i].line_cntl.stopbits = 0;
		m_s[i].line_cntl.parity_enable = 0;
		m_s[i].line_cntl.evenparity_sel = 0;
		m_s[i].line_cntl.stick_parity = 0;
		m_s[i].line_cntl.break_cntl = 0;
		m_s[i].line_cntl.dlab = 0;

		/* Modem Control reg: b0000 0000 */
		m_s[i].modem_cntl.dtr = 0;
		m_s[i].modem_cntl.rts = 0;
		m_s[i].modem_cntl.out1 = 0;
		m_s[i].modem_cntl.out2 = 0;
		m_s[i].modem_cntl.local_loopback = 0;

		/* Line Status register: b0110 0000 */
		m_s[i].line_status.rxdata_ready = 0;
		m_s[i].line_status.overrun_error = 0;
		m_s[i].line_status.parity_error = 0;
		m_s[i].line_status.framing_error = 0;
		m_s[i].line_status.break_int = 0;
		m_s[i].line_status.thr_empty = 1;
		m_s[i].line_status.tsr_empty = 1;
		m_s[i].line_status.fifo_error = 0;

		/* Modem Status register: bXXXX 0000 */
		m_s[i].modem_status.delta_cts = 0;
		m_s[i].modem_status.delta_dsr = 0;
		m_s[i].modem_status.ri_trailedge = 0;
		m_s[i].modem_status.delta_dcd = 0;
		m_s[i].modem_status.cts = 0;
		m_s[i].modem_status.dsr = 0;
		m_s[i].modem_status.ri = 0;
		m_s[i].modem_status.dcd = 0;

		m_s[i].scratch = 0;      /* scratch register */
		m_s[i].divisor_lsb = 1;  /* divisor-lsb register */
		m_s[i].divisor_msb = 0;  /* divisor-msb register */

		m_s[i].baudrate = 19200;
		m_s[i].databyte_usec = 87;

		memset(m_s[i].rx_fifo, 0, 16);   /* receive FIFO (internal) */
		memset(m_s[i].tx_fifo, 0, 16);   /* transmit FIFO (internal) */

		// simulate device connected
		if(m_s[i].io_mode != SER_MODE_RAW) {
			m_s[i].modem_status.cts = 1;
			m_s[i].modem_status.dsr = 1;
		}
	//}
}

void Serial::raise_interrupt(uint8_t port, int type)
{
	bool gen_int = 0;

	switch (type) {
		case SER_INT_IER: /* IER has changed */
			gen_int = 1;
			break;
		case SER_INT_RXDATA:
			if(m_s[port].int_enable.rxdata_enable) {
				m_s[port].rx_interrupt = 1;
				gen_int = 1;
			} else {
				m_s[port].rx_ipending = 1;
			}
			break;
		case SER_INT_TXHOLD:
			if(m_s[port].int_enable.txhold_enable) {
				m_s[port].tx_interrupt = 1;
				gen_int = 1;
			}
			break;
		case SER_INT_RXLSTAT:
			if(m_s[port].int_enable.rxlstat_enable) {
				m_s[port].ls_interrupt = 1;
				gen_int = 1;
			} else {
				m_s[port].ls_ipending = 1;
			}
			break;
		case SER_INT_MODSTAT:
			if((m_s[port].ms_ipending == 1) && (m_s[port].int_enable.modstat_enable == 1)) {
				m_s[port].ms_interrupt = 1;
				m_s[port].ms_ipending = 0;
				gen_int = 1;
			}
			break;
		case SER_INT_FIFO:
			if(m_s[port].int_enable.rxdata_enable) {
				m_s[port].fifo_interrupt = 1;
				gen_int = 1;
			} else {
				m_s[port].fifo_ipending = 1;
			}
			break;
	}

	if(gen_int && m_s[port].modem_cntl.out2) {
		g_pic.raise_irq(m_s[port].IRQ);
	}
}

uint16_t Serial::read_disabled(uint16_t address, unsigned io_len)
{
	/* when read in disabled status the ports are being tested.
	 * i'm not interested in emulating the real behaviour,
	 * i just want the POST to succeed
	 */
	/* well, it appears the POST tests only LCR in disabled mode, other registers
	 * are tested with serial enabled. i can delete read_disable and write_disabled.
	 */
	uint16_t ret16;
	uint8_t offset, val=0;

	if(io_len == 2) {
		ret16 = read_disabled(address, 1);
		ret16 |= (read_disabled(address + 1, 1)) << 8;
		return ret16;
	}

	offset = address & 0x07;

	switch(offset) {
		case SER_RBR: /* 8 receive buffer, or divisor latch LSB if DLAB set */
		case SER_IER: /* 9 interrupt enable register, or div. latch MSB */
			val = m_post_reg;
			break;
		case SER_IIR: /* a interrupt ID register */
			break;
		case SER_LCR: /* b Line control register */
			//see F000:2062, the OUT_PORT_TEST must fail with CF=1
			val = 0;
			break;
		case SER_MCR: /* c MODEM control register */
		case SER_LSR: /* d Line status register */
		case SER_MSR: /* e MODEM status register */
		case SER_SCR: /* f scratch register */
			val = m_post_reg;
			break;
	}

	return val;
}

void Serial::write_disabled(uint16_t address, uint16_t value, unsigned io_len)
{
	uint8_t offset;

	if(io_len == 2) {
		write_disabled(address, (value & 0xff), 1);
		write_disabled(address + 1, ((value >> 8) & 0xff), 1);
		return;
	}

	offset = address & 0x07;

	switch(offset) {
		case SER_THR: /* 8 transmit buffer, or divisor latch LSB if DLAB set */
			m_post_reg = value;
			break;
		case SER_IER: /* 9 interrupt enable register, or div. latch MSB */
			m_post_reg = value & 0x0F;
			break;
		case SER_FCR: /* a FIFO control register */
			break;
		case SER_LCR: /* b Line control register */
			m_s[0].line_cntl.dlab = (value & 0x80);
			break;
		case SER_MCR: /* c MODEM control register */
		case SER_LSR: /* d Line status register */
		case SER_MSR: /* e MODEM status register */
		case SER_SCR: /* f scratch register */
			m_post_reg = value;
			break;
	}
}

uint16_t Serial::read(uint16_t address, unsigned io_len)
{
	g_sysboard.set_feedback();

	if(!m_enabled) {
		return read_disabled(address, io_len);
	}

	uint8_t offset, val;
	uint16_t ret16;

	if(io_len == 2) {
		ret16 = read(address, 1);
		ret16 |= (read(address + 1, 1)) << 8;
		return ret16;
	}

	offset = address & 0x07;

	/* port is always 0
	switch (address & 0x03f8) {
		case 0x03f8: port = 0; break;
		case 0x02f8: port = 1; break;
		case 0x03e8: port = 2; break;
		case 0x02e8: port = 3; break;
	}
	*/

	const uint8_t port = 0;

	switch (offset) {
		case SER_RBR: /* receive buffer, or divisor latch LSB if DLAB set */
			if(m_s[port].line_cntl.dlab) {
				val = m_s[port].divisor_lsb;
			} else {
				if(m_s[port].fifo_cntl.enable) {
					val = m_s[port].rx_fifo[0];
					if(m_s[port].rx_fifo_end > 0) {
						memcpy(&m_s[port].rx_fifo[0], &m_s[port].rx_fifo[1], 15);
						m_s[port].rx_fifo_end--;
					}
					if(m_s[port].rx_fifo_end == 0) {
						m_s[port].line_status.rxdata_ready = 0;
						m_s[port].rx_interrupt = 0;
						m_s[port].rx_ipending = 0;
						m_s[port].fifo_interrupt = 0;
						m_s[port].fifo_ipending = 0;
						lower_interrupt(port);
					}
				} else {
					val = m_s[port].rxbuffer;
					m_s[port].line_status.rxdata_ready = 0;
					m_s[port].rx_interrupt = 0;
					m_s[port].rx_ipending = 0;
					lower_interrupt(port);
				}
			}
			break;

		case SER_IER: /* interrupt enable register, or div. latch MSB */
			if(m_s[port].line_cntl.dlab) {
				val = m_s[port].divisor_msb;
			} else {
				val = m_s[port].int_enable.rxdata_enable |
						(m_s[port].int_enable.txhold_enable  << 1) |
						(m_s[port].int_enable.rxlstat_enable << 2) |
						(m_s[port].int_enable.modstat_enable << 3);
			}
		break;

		case SER_IIR: /* interrupt ID register */
			/*
			* Set the interrupt ID based on interrupt source
			*/
			if(m_s[port].ls_interrupt) {
				m_s[port].int_ident.int_ID = 0x3;
				m_s[port].int_ident.ipending = 0;
			} else if(m_s[port].fifo_interrupt) {
				m_s[port].int_ident.int_ID = 0x6;
				m_s[port].int_ident.ipending = 0;
			} else if(m_s[port].rx_interrupt) {
				m_s[port].int_ident.int_ID = 0x2;
				m_s[port].int_ident.ipending = 0;
			} else if(m_s[port].tx_interrupt) {
				m_s[port].int_ident.int_ID = 0x1;
				m_s[port].int_ident.ipending = 0;
			} else if(m_s[port].ms_interrupt) {
				m_s[port].int_ident.int_ID = 0x0;
				m_s[port].int_ident.ipending = 0;
			} else {
				m_s[port].int_ident.int_ID = 0x0;
				m_s[port].int_ident.ipending = 1;
			}
			m_s[port].tx_interrupt = 0;
			lower_interrupt(port);

			val = m_s[port].int_ident.ipending  |
					(m_s[port].int_ident.int_ID << 1) |
					(m_s[port].fifo_cntl.enable ? 0xc0 : 0x00);
			break;

		case SER_LCR: /* Line control register */
			val = m_s[port].line_cntl.wordlen_sel |
			(m_s[port].line_cntl.stopbits       << 2) |
			(m_s[port].line_cntl.parity_enable  << 3) |
			(m_s[port].line_cntl.evenparity_sel << 4) |
			(m_s[port].line_cntl.stick_parity   << 5) |
			(m_s[port].line_cntl.break_cntl     << 6) |
			(m_s[port].line_cntl.dlab           << 7);
			break;

		case SER_MCR: /* MODEM control register */
			val = m_s[port].modem_cntl.dtr |
			(m_s[port].modem_cntl.rts << 1) |
			(m_s[port].modem_cntl.out1 << 2) |
			(m_s[port].modem_cntl.out2 << 3) |
			(m_s[port].modem_cntl.local_loopback << 4);
			break;

		case SER_LSR: /* Line status register */
			val = m_s[port].line_status.rxdata_ready |
				(m_s[port].line_status.overrun_error  << 1) |
				(m_s[port].line_status.parity_error   << 2) |
				(m_s[port].line_status.framing_error  << 3) |
				(m_s[port].line_status.break_int      << 4) |
				(m_s[port].line_status.thr_empty      << 5) |
				(m_s[port].line_status.tsr_empty      << 6) |
				(m_s[port].line_status.fifo_error     << 7);
			m_s[port].line_status.overrun_error = 0;
			m_s[port].line_status.framing_error = 0;
			m_s[port].line_status.break_int = 0;
			m_s[port].ls_interrupt = 0;
			m_s[port].ls_ipending = 0;
			lower_interrupt(port);
			break;

		case SER_MSR: /* MODEM status register */
			#if USE_RAW_SERIAL
			if(m_s[port].io_mode == SER_MODE_RAW) {
				bool prev_cts = m_s[port].modem_status.cts;
				bool prev_dsr = m_s[port].modem_status.dsr;
				bool prev_ri  = m_s[port].modem_status.ri;
				bool prev_dcd = m_s[port].modem_status.dcd;

				val = m_s[port].raw->get_modem_status();
				m_s[port].modem_status.cts = (val & 0x10) >> 4;
				m_s[port].modem_status.dsr = (val & 0x20) >> 5;
				m_s[port].modem_status.ri  = (val & 0x40) >> 6;
				m_s[port].modem_status.dcd = (val & 0x80) >> 7;
				if(m_s[port].modem_status.cts != prev_cts) {
					m_s[port].modem_status.delta_cts = 1;
				}
				if(m_s[port].modem_status.dsr != prev_dsr) {
					m_s[port].modem_status.delta_dsr = 1;
				}
				if((m_s[port].modem_status.ri == 0) && (prev_ri == 1))
					m_s[port].modem_status.ri_trailedge = 1;
				if(m_s[port].modem_status.dcd != prev_dcd) {
					m_s[port].modem_status.delta_dcd = 1;
				}
			}
			#endif
			val = m_s[port].modem_status.delta_cts |
					(m_s[port].modem_status.delta_dsr    << 1) |
					(m_s[port].modem_status.ri_trailedge << 2) |
					(m_s[port].modem_status.delta_dcd    << 3) |
					(m_s[port].modem_status.cts          << 4) |
					(m_s[port].modem_status.dsr          << 5) |
					(m_s[port].modem_status.ri           << 6) |
					(m_s[port].modem_status.dcd          << 7);
			m_s[port].modem_status.delta_cts = 0;
			m_s[port].modem_status.delta_dsr = 0;
			m_s[port].modem_status.ri_trailedge = 0;
			m_s[port].modem_status.delta_dcd = 0;
			m_s[port].ms_interrupt = 0;
			m_s[port].ms_ipending = 0;
			lower_interrupt(port);
			break;

		case SER_SCR: /* scratch register */
			val = m_s[port].scratch;
			break;

		default:
			val = 0; // keep compiler happy
			PERRF_ABORT(LOG_COM, "unsupported io read from address=0x%04x!\n", address);
			break;
	}

	PDEBUGF(LOG_V2, LOG_COM, "COM%d register read from address: 0x%04x = 0x%02x\n", port+1, address, val);

	return val;
}


void Serial::write(uint16_t address, uint16_t value, unsigned io_len)
{
	g_sysboard.set_feedback();

	if(!m_enabled) {
		write_disabled(address, value, io_len);
		return;
	}

	bool gen_int = 0;
	uint8_t offset, new_wordlen;
	#if USE_RAW_SERIAL
		bool mcr_changed = 0;
		uint8_t p_mode;
	#endif
	int new_baudrate;
	bool restart_timer = 0;


	if(io_len == 2) {
		write(address, (value & 0xff), 1);
		write(address + 1, ((value >> 8) & 0xff), 1);
		return;
	}

	offset = address & 0x07;
	/* port is always 0
	switch (address & 0x03f8) {
		case 0x03f8: port = 0; break;
		case 0x02f8: port = 1; break;
		case 0x03e8: port = 2; break;
		case 0x02e8: port = 3; break;
	}
	*/
	const uint8_t port = 0;

	PDEBUGF(LOG_V2, LOG_COM, "COM%d register write to  address: 0x%04x = 0x%02x\n", port+1, address, value);

	bool new_b0 = value & 0x01;
	bool new_b1 = (value & 0x02) >> 1;
	bool new_b2 = (value & 0x04) >> 2;
	bool new_b3 = (value & 0x08) >> 3;
	bool new_b4 = (value & 0x10) >> 4;
	bool new_b5 = (value & 0x20) >> 5;
	bool new_b6 = (value & 0x40) >> 6;
	bool new_b7 = (value & 0x80) >> 7;

	switch (offset) {
		case SER_THR: /* transmit buffer, or divisor latch LSB if DLAB set */
			if(m_s[port].line_cntl.dlab) {
				m_s[port].divisor_lsb = value;
			} else {
				uint8_t bitmask = 0xff >> (3 - m_s[port].line_cntl.wordlen_sel);
				value &= bitmask;
				if(m_s[port].line_status.thr_empty) {
					if(m_s[port].fifo_cntl.enable) {
						m_s[port].tx_fifo[m_s[port].tx_fifo_end++] = value;
					} else {
						m_s[port].thrbuffer = value;
					}
					m_s[port].line_status.thr_empty = 0;
					if(m_s[port].line_status.tsr_empty) {
						if(m_s[port].fifo_cntl.enable) {
							m_s[port].tsrbuffer = m_s[port].tx_fifo[0];
							memcpy(&m_s[port].tx_fifo[0], &m_s[port].tx_fifo[1], 15);
							m_s[port].line_status.thr_empty = (--m_s[port].tx_fifo_end == 0);
						} else {
							m_s[port].tsrbuffer = m_s[port].thrbuffer;
							m_s[port].line_status.thr_empty = 1;
						}
						m_s[port].line_status.tsr_empty = 0;
						raise_interrupt(port, SER_INT_TXHOLD);
						g_machine.activate_timer(m_s[port].tx_timer_index,
								m_s[port].databyte_usec,
								0); /* not continuous */
					} else {
						m_s[port].tx_interrupt = 0;
						lower_interrupt(port);
					}
				} else {
					if(m_s[port].fifo_cntl.enable) {
						if(m_s[port].tx_fifo_end < 16) {
							m_s[port].tx_fifo[m_s[port].tx_fifo_end++] = value;
						} else {
							PERRF(LOG_COM, "COM%d: transmit FIFO overflow\n", port+1);
						}
					} else {
						PERRF(LOG_COM, "COM%d: write to tx hold register when not empty\n", port+1);
					}
				}
			}
			break;

		case SER_IER: /* interrupt enable register, or div. latch MSB */
			if(m_s[port].line_cntl.dlab) {
				m_s[port].divisor_msb = value;
			} else {
				if(new_b3 != m_s[port].int_enable.modstat_enable) {
					m_s[port].int_enable.modstat_enable  = new_b3;
					if(m_s[port].int_enable.modstat_enable == 1) {
						if(m_s[port].ms_ipending == 1) {
							m_s[port].ms_interrupt = 1;
							m_s[port].ms_ipending = 0;
							gen_int = 1;
						}
					} else {
						if(m_s[port].ms_interrupt == 1) {
							m_s[port].ms_interrupt = 0;
							m_s[port].ms_ipending = 1;
							lower_interrupt(port);
						}
					}
				}
				if(new_b1 != m_s[port].int_enable.txhold_enable) {
					m_s[port].int_enable.txhold_enable  = new_b1;
					if(m_s[port].int_enable.txhold_enable == 1) {
						m_s[port].tx_interrupt = m_s[port].line_status.thr_empty;
						if(m_s[port].tx_interrupt) gen_int = 1;
					} else {
						m_s[port].tx_interrupt = 0;
						lower_interrupt(port);
					}
				}
				if(new_b0 != m_s[port].int_enable.rxdata_enable) {
					m_s[port].int_enable.rxdata_enable  = new_b0;
					if(m_s[port].int_enable.rxdata_enable == 1) {
						if(m_s[port].fifo_ipending == 1) {
							m_s[port].fifo_interrupt = 1;
							m_s[port].fifo_ipending = 0;
							gen_int = 1;
						}
						if(m_s[port].rx_ipending == 1) {
							m_s[port].rx_interrupt = 1;
							m_s[port].rx_ipending = 0;
							gen_int = 1;
						}
					} else {
						if(m_s[port].rx_interrupt == 1) {
							m_s[port].rx_interrupt = 0;
							m_s[port].rx_ipending = 1;
							lower_interrupt(port);
						}
						if(m_s[port].fifo_interrupt == 1) {
							m_s[port].fifo_interrupt = 0;
							m_s[port].fifo_ipending = 1;
							lower_interrupt(port);
						}
					}
				}
				if(new_b2 != m_s[port].int_enable.rxlstat_enable) {
					m_s[port].int_enable.rxlstat_enable  = new_b2;
					if(m_s[port].int_enable.rxlstat_enable == 1) {
						if(m_s[port].ls_ipending == 1) {
							m_s[port].ls_interrupt = 1;
							m_s[port].ls_ipending = 0;
							gen_int = 1;
						}
					} else {
						if(m_s[port].ls_interrupt == 1) {
							m_s[port].ls_interrupt = 0;
							m_s[port].ls_ipending = 1;
							lower_interrupt(port);
						}
					}
				}
				if(gen_int) raise_interrupt(port, SER_INT_IER);
			}
			break;

		case SER_FCR: /* FIFO control register */
			if(new_b0 && !m_s[port].fifo_cntl.enable) {
				PDEBUGF(LOG_V0, LOG_COM, "COM%d: FIFO enabled\n", port+1);
				m_s[port].rx_fifo_end = 0;
				m_s[port].tx_fifo_end = 0;
			}
			m_s[port].fifo_cntl.enable = new_b0;
			if(new_b1) {
				m_s[port].rx_fifo_end = 0;
			}
			if(new_b2) {
				m_s[port].tx_fifo_end = 0;
			}
			m_s[port].fifo_cntl.rxtrigger = (value & 0xc0) >> 6;
			break;

		case SER_LCR: /* Line control register */
			new_wordlen = value & 0x03;
			#if USE_RAW_SERIAL
			if(m_s[port].io_mode == SER_MODE_RAW) {
				if(m_s[port].line_cntl.wordlen_sel != new_wordlen) {
					m_s[port].raw->set_data_bits(new_wordlen + 5);
				}
				if(new_b2 != m_s[port].line_cntl.stopbits) {
					m_s[port].raw->set_stop_bits(new_b2 ? 2 : 1);
				}
				if((new_b3 != m_s[port].line_cntl.parity_enable) ||
						(new_b4 != m_s[port].line_cntl.evenparity_sel) ||
						(new_b5 != m_s[port].line_cntl.stick_parity)) {
					if(new_b3 == 0) {
						p_mode = P_NONE;
					} else {
						p_mode = ((value & 0x30) >> 4) + 1;
					}
					m_s[port].raw->set_parity_mode(p_mode);
				}
				if((new_b6 != m_s[port].line_cntl.break_cntl) &&
						(!m_s[port].modem_cntl.local_loopback)) {
					m_s[port].raw->set_break(new_b6);
				}
			}
			#endif // USE_RAW_SERIAL
			/* These are ignored, but set them up so they can be read back */
			m_s[port].line_cntl.stopbits = new_b2;
			m_s[port].line_cntl.parity_enable = new_b3;
			m_s[port].line_cntl.evenparity_sel = new_b4;
			m_s[port].line_cntl.stick_parity = new_b5;
			m_s[port].line_cntl.break_cntl = new_b6;
			if(m_s[port].modem_cntl.local_loopback && m_s[port].line_cntl.break_cntl) {
				m_s[port].line_status.break_int = 1;
				m_s[port].line_status.framing_error = 1;
				rx_fifo_enq(port, 0x00);
			}
			if(!new_b7 && m_s[port].line_cntl.dlab) {
				if((m_s[port].divisor_lsb | m_s[port].divisor_msb) != 0) {
					new_baudrate = (int)(PC_CLOCK_XTL /
							(16 * ((m_s[port].divisor_msb << 8) | m_s[port].divisor_lsb)));
					if(new_baudrate != m_s[port].baudrate) {
						m_s[port].baudrate = new_baudrate;
						restart_timer = 1;
						PDEBUGF(LOG_V2, LOG_COM, "COM%d: baud rate set to %d\n", port+1, m_s[port].baudrate);
						#if USE_RAW_SERIAL
						if(m_s[port].io_mode == SER_MODE_RAW) {
							m_s[port].raw->set_baudrate(m_s[port].baudrate);
						}
						#endif
					}
				} else {
					PERRF(LOG_COM, "COM%d: ignoring invalid baud rate divisor\n", port+1);
				}
			}
			m_s[port].line_cntl.dlab = new_b7;
			if(new_wordlen != m_s[port].line_cntl.wordlen_sel) {
				m_s[port].line_cntl.wordlen_sel = new_wordlen;
				restart_timer = 1;
			}
			if(restart_timer) {
				// Start the receive polling process if not already started
				// and there is a valid baudrate.
				m_s[port].databyte_usec = (uint32_t)(1000000.0 / m_s[port].baudrate *
						(m_s[port].line_cntl.wordlen_sel + 7));
				g_machine.activate_timer(m_s[port].rx_timer_index,
						m_s[port].databyte_usec,
						0); /* not continuous */
			}
			break;

		case SER_MCR: /* MODEM control register */
			if((m_s[port].io_mode == SER_MODE_MOUSE) && ((m_s[port].line_cntl.wordlen_sel == 2) ||
					(m_s[port].line_cntl.wordlen_sel == 3)))
			{
				if(!m_s[port].modem_cntl.dtr && new_b0) {
					detect_mouse = 1;
				}
				if((detect_mouse == 1) && new_b1) {
					detect_mouse = 2;
				}
			}
			#if USE_RAW_SERIAL
			if(m_s[port].io_mode == SER_MODE_RAW) {
				mcr_changed = (m_s[port].modem_cntl.dtr != new_b0) |
						(m_s[port].modem_cntl.rts != new_b1);
			}
			#endif
			m_s[port].modem_cntl.dtr  = new_b0;
			m_s[port].modem_cntl.rts  = new_b1;
			m_s[port].modem_cntl.out1 = new_b2;
			m_s[port].modem_cntl.out2 = new_b3;

			if(new_b4 != m_s[port].modem_cntl.local_loopback) {
				m_s[port].modem_cntl.local_loopback = new_b4;
				if(m_s[port].modem_cntl.local_loopback) {
					/* transition to loopback mode */
					#if USE_RAW_SERIAL
					if(m_s[port].io_mode == SER_MODE_RAW) {
						if(m_s[port].modem_cntl.dtr || m_s[port].modem_cntl.rts) {
							m_s[port].raw->set_modem_control(0);
						}
					}
					#endif
					if(m_s[port].line_cntl.break_cntl) {
						#if USE_RAW_SERIAL
						if(m_s[port].io_mode == SER_MODE_RAW) {
							m_s[port].raw->set_break(0);
						}
						#endif
						m_s[port].line_status.break_int = 1;
						m_s[port].line_status.framing_error = 1;
						rx_fifo_enq(port, 0x00);
					}
				} else {
					/* transition to normal mode */
					#if USE_RAW_SERIAL
					if(m_s[port].io_mode == SER_MODE_RAW) {
						mcr_changed = 1;
						if(m_s[port].line_cntl.break_cntl) {
							m_s[port].raw->set_break(0);
						}
					}
					#endif
				}
			}

			if(m_s[port].modem_cntl.local_loopback) {
				bool prev_cts = m_s[port].modem_status.cts;
				bool prev_dsr = m_s[port].modem_status.dsr;
				bool prev_ri  = m_s[port].modem_status.ri;
				bool prev_dcd = m_s[port].modem_status.dcd;
				m_s[port].modem_status.cts = m_s[port].modem_cntl.rts;
				m_s[port].modem_status.dsr = m_s[port].modem_cntl.dtr;
				m_s[port].modem_status.ri  = m_s[port].modem_cntl.out1;
				m_s[port].modem_status.dcd = m_s[port].modem_cntl.out2;
				if(m_s[port].modem_status.cts != prev_cts) {
					m_s[port].modem_status.delta_cts = 1;
					m_s[port].ms_ipending = 1;
				}
				if(m_s[port].modem_status.dsr != prev_dsr) {
					m_s[port].modem_status.delta_dsr = 1;
					m_s[port].ms_ipending = 1;
				}
				if(m_s[port].modem_status.ri != prev_ri)
					m_s[port].ms_ipending = 1;
				if((m_s[port].modem_status.ri == 0) && (prev_ri == 1))
					m_s[port].modem_status.ri_trailedge = 1;
				if(m_s[port].modem_status.dcd != prev_dcd) {
					m_s[port].modem_status.delta_dcd = 1;
					m_s[port].ms_ipending = 1;
				}
				raise_interrupt(port, SER_INT_MODSTAT);
			} else {
				if(m_s[port].io_mode == SER_MODE_MOUSE) {
					if(detect_mouse == 2) {
						PDEBUGF(LOG_V2, LOG_COM, "com%d: mouse detection mode\n", port+1);
						if((mouse_type == MOUSE_TYPE_SERIAL) ||
								(mouse_type == MOUSE_TYPE_SERIAL_MSYS)) {
							mouse_internal_buffer.head = 0;
							mouse_internal_buffer.num_elements = 1;
							mouse_internal_buffer.buffer[0] = 'M';
						} else if(mouse_type == MOUSE_TYPE_SERIAL_WHEEL) {
							mouse_internal_buffer.head = 0;
							mouse_internal_buffer.num_elements = 6;
							mouse_internal_buffer.buffer[0] = 'M';
							mouse_internal_buffer.buffer[1] = 'Z';
							mouse_internal_buffer.buffer[2] = '@';
							mouse_internal_buffer.buffer[3] = '\0';
							mouse_internal_buffer.buffer[4] = '\0';
							mouse_internal_buffer.buffer[5] = '\0';
						}
						g_machine.activate_timer(m_s[port].rx_timer_index,
								m_s[port].databyte_usec,
								0); /* not continuous */
						detect_mouse = 0;
					}
				}

				if(m_s[port].io_mode == SER_MODE_RAW) {
					#if USE_RAW_SERIAL
					if(mcr_changed) {
						m_s[port].raw->set_modem_control(value & 0x03);
					}
					#endif
				} else {
					/* simulate device connected */
					m_s[port].modem_status.cts = 1;
					m_s[port].modem_status.dsr = 1;
					m_s[port].modem_status.ri  = 0;
					m_s[port].modem_status.dcd = 0;
				}
			}
			break;

		case SER_LSR: /* Line status register */
			PERRF(LOG_COM, "COM%d: write to line status register ignored\n", port+1);
			break;

		case SER_MSR: /* MODEM status register */
			PERRF(LOG_COM, "COM%d: write to MODEM status register ignored\n", port+1);
			break;

		case SER_SCR: /* scratch register */
			m_s[port].scratch = value;
			break;

		default:
			PERRF_ABORT(LOG_COM, "unsupported io write to address=0x%04x, value = 0x%02x!\n", address, value);
			break;
	}
}

void Serial::rx_fifo_enq(uint8_t port, uint8_t data)
{
	bool gen_int = 0;

	if(m_s[port].fifo_cntl.enable) {
		if(m_s[port].rx_fifo_end == 16) {
			PERRF(LOG_COM, "COM%d: receive FIFO overflow\n", port+1);
			m_s[port].line_status.overrun_error = 1;
			raise_interrupt(port, SER_INT_RXLSTAT);
		} else {
			m_s[port].rx_fifo[m_s[port].rx_fifo_end++] = data;
			switch (m_s[port].fifo_cntl.rxtrigger) {
				case 1:
					if(m_s[port].rx_fifo_end == 4) gen_int = 1;
					break;
				case 2:
					if(m_s[port].rx_fifo_end == 8) gen_int = 1;
					break;
				case 3:
					if(m_s[port].rx_fifo_end == 14) gen_int = 1;
					break;
				default:
					gen_int = 1;
					break;
			}
			if(gen_int) {
				g_machine.deactivate_timer(m_s[port].fifo_timer_index);
				m_s[port].line_status.rxdata_ready = 1;
				raise_interrupt(port, SER_INT_RXDATA);
			} else {
				g_machine.activate_timer(m_s[port].fifo_timer_index,
						m_s[port].databyte_usec * 3,
						0); /* not continuous */
			}
		}
	} else {
		if(m_s[port].line_status.rxdata_ready == 1) {
			PERRF(LOG_COM, "COM%d: overrun error\n", port+1);
			m_s[port].line_status.overrun_error = 1;
			raise_interrupt(port, SER_INT_RXLSTAT);
		}
		m_s[port].rxbuffer = data;
		m_s[port].line_status.rxdata_ready = 1;
		raise_interrupt(port, SER_INT_RXDATA);
	}
}

void Serial::tx_timer(uint8_t port)
{
	bool gen_int = 0;
	char pname[20];

	if(m_s[port].modem_cntl.local_loopback) {
		rx_fifo_enq(port, m_s[port].tsrbuffer);
	} else {
		switch (m_s[port].io_mode) {
			case SER_MODE_FILE:
				if(m_s[port].output == NULL) {
					sprintf(pname, "COM%d", port+1);
					std::string dev = g_program.config().get_string(pname, COM_DEV);

					if(!dev.empty()) {
						m_s[port].output = fopen(dev.c_str(), "wb");
					}
					if(m_s[port].output == NULL) {
						PERRF(LOG_COM, "Could not open '%s' to write COM%d output",
								dev.c_str(), port+1);
						m_s[port].io_mode = SER_MODE_NULL;
					}
				}
				fputc(m_s[port].tsrbuffer, m_s[port].output);
				fflush(m_s[port].output);
				break;
			case SER_MODE_TERM:
				#if SERIAL_ENABLE
				PDEBUGF(LOG_V2, LOG_COM, "COM%d: write: '%c'\n", port+1, m_s[port].tsrbuffer);
				if(m_s[port].tty_id >= 0) {
					ssize_t res = ::write(m_s[port].tty_id, (void*) & m_s[port].tsrbuffer, 1);
					ASSERT(res==1);
				}
				#endif
				break;
			case SER_MODE_RAW:
				#if USE_RAW_SERIAL
				if(!m_s[port].raw->ready_transmit())
					PERRF_ABORT(LOG_COM, "COM%d: not ready to transmit\n", port+1);
				m_s[port].raw->transmit(m_s[port].tsrbuffer);
				#endif
				break;
			case SER_MODE_MOUSE:
				PDEBUGF(LOG_V1, LOG_COM, "COM%d: write to mouse ignored: 0x%02x\n", port+1, m_s[port].tsrbuffer);
				break;
			case SER_MODE_SOCKET_CLIENT:
			case SER_MODE_SOCKET_SERVER:
				if(m_s[port].socket_id >= 0) {
					#if SER_WIN32
					PDEBUGF(LOG_V2, LOG_COM, "attempting to write win32 : %c\n", m_s[port].tsrbuffer);
					::send(m_s[port].socket_id, (const char*) & m_s[port].tsrbuffer, 1, 0);
					#else
					ssize_t res = ::write(m_s[port].socket_id, (void*)&m_s[port].tsrbuffer, 1);
					ASSERT(res==1);
					#endif
				}
				break;
			case SER_MODE_PIPE_CLIENT:
			case SER_MODE_PIPE_SERVER:
				#if SER_WIN32
				if(m_s[port].pipe) {
					DWORD written;
					WriteFile(m_s[port].pipe, (void*)& m_s[port].tsrbuffer, 1, &written, NULL);
				}
				#endif
				break;
		}
	}

	m_s[port].line_status.tsr_empty = 1;
	if(m_s[port].fifo_cntl.enable && (m_s[port].tx_fifo_end > 0)) {
		m_s[port].tsrbuffer = m_s[port].tx_fifo[0];
		m_s[port].line_status.tsr_empty = 0;
		memcpy(&m_s[port].tx_fifo[0], &m_s[port].tx_fifo[1], 15);
		gen_int = (--m_s[port].tx_fifo_end == 0);
	} else if(!m_s[port].line_status.thr_empty) {
		m_s[port].tsrbuffer = m_s[port].thrbuffer;
		m_s[port].line_status.tsr_empty = 0;
		gen_int = 1;
	}
	if(!m_s[port].line_status.tsr_empty) {
		if(gen_int) {
			m_s[port].line_status.thr_empty = 1;
			raise_interrupt(port, SER_INT_TXHOLD);
		}
		g_machine.activate_timer(m_s[port].tx_timer_index, m_s[port].databyte_usec,
				0); /* not continuous */
	}
}

void Serial::rx_timer(uint8_t port)
{
	#if HAVE_SYS_SELECT_H && SERIAL_ENABLE
	struct timeval tval;
	fd_set fds;
	#endif
	bool data_ready = 0;
	int db_usec = m_s[port].databyte_usec;
	unsigned char chbuf = 0;

	if(m_s[port].io_mode == SER_MODE_TERM) {
		#if HAVE_SYS_SELECT_H && SERIAL_ENABLE
		tval.tv_sec  = 0;
		tval.tv_usec = 0;

		// MacOS: I'm not sure what to do with this, since I don't know
		// what an fd_set is or what FD_SET() or select() do. They aren't
		// declared in the CodeWarrior standard library headers. I'm just
		// leaving it commented out for the moment.

		FD_ZERO(&fds);
		if(m_s[port].tty_id >= 0)
			FD_SET(m_s[port].tty_id, &fds);
		#endif
	}
	if((m_s[port].line_status.rxdata_ready == 0) || (m_s[port].fifo_cntl.enable)) {
		switch (m_s[port].io_mode) {
			case SER_MODE_SOCKET_CLIENT:
			case SER_MODE_SOCKET_SERVER:
				#if HAVE_SYS_SELECT_H && SERIAL_ENABLE
				if(m_s[port].line_status.rxdata_ready == 0) {
					tval.tv_sec  = 0;
					tval.tv_usec = 0;
					FD_ZERO(&fds);
					SOCKET socketid = m_s[port].socket_id;
					if(socketid >= 0) FD_SET(socketid, &fds);
					if((socketid >= 0) && (select(socketid+1, &fds, NULL, NULL, &tval) == 1)) {
						ssize_t bytes = (ssize_t)
						#if SER_WIN32
							::recv(socketid, (char*) &chbuf, 1, 0);
						#else
							::read(socketid, &chbuf, 1);
						#endif
						if(bytes > 0) {
							PINFOF(LOG_V2, LOG_COM, " -- COM%d : read byte [%d]\n", port+1, chbuf);
							data_ready = 1;
						}
					}
				}
				#endif
				break;
			case SER_MODE_RAW:
				#if USE_RAW_SERIAL
				int data;
				if((data_ready = m_s[port].raw->ready_receive())) {
					data = m_s[port].raw->receive();
					if(data < 0) {
						data_ready = 0;
						switch (data) {
							case RAW_EVENT_BREAK:
								m_s[port].line_status.break_int = 1;
								raise_interrupt(port, SER_INT_RXLSTAT);
								break;
							case RAW_EVENT_FRAME:
								m_s[port].line_status.framing_error = 1;
								raise_interrupt(port, SER_INT_RXLSTAT);
								break;
							case RAW_EVENT_OVERRUN:
								m_s[port].line_status.overrun_error = 1;
								raise_interrupt(port, SER_INT_RXLSTAT);
								break;
							case RAW_EVENT_PARITY:
								m_s[port].line_status.parity_error = 1;
								raise_interrupt(port, SER_INT_RXLSTAT);
								break;
							case RAW_EVENT_CTS_ON:
							case RAW_EVENT_CTS_OFF:
							case RAW_EVENT_DSR_ON:
							case RAW_EVENT_DSR_OFF:
							case RAW_EVENT_RING_ON:
							case RAW_EVENT_RING_OFF:
							case RAW_EVENT_RLSD_ON:
							case RAW_EVENT_RLSD_OFF:
								raise_interrupt(port, SER_INT_MODSTAT);
								break;
						}
					}
				}
				if(data_ready) {
					chbuf = data;
				}
				#endif
				break;
			case SER_MODE_TERM:
				#if HAVE_SYS_SELECT_H && SERIAL_ENABLE
				if((m_s[port].tty_id >= 0) && (select(m_s[port].tty_id + 1, &fds, NULL, NULL, &tval) == 1)) {
					ssize_t res = ::read(m_s[port].tty_id, &chbuf, 1);
					ASSERT(res==1);
					PDEBUGF(LOG_V2, LOG_COM, "COM%d: read: '%c'\n", port+1, chbuf);
					data_ready = 1;
				}
				#endif
				break;
			case SER_MODE_MOUSE:
				if(mouse_update && (mouse_internal_buffer.num_elements == 0)) {
					update_mouse_data();
				}
				if(mouse_internal_buffer.num_elements > 0) {
					chbuf = mouse_internal_buffer.buffer[mouse_internal_buffer.head];
					mouse_internal_buffer.head = (mouse_internal_buffer.head + 1) % MOUSE_BUFF_SIZE;
					mouse_internal_buffer.num_elements--;
					data_ready = 1;
				}
				break;
			case SER_MODE_PIPE_CLIENT:
			case SER_MODE_PIPE_SERVER:
				#if SER_WIN32
				DWORD avail = 0;
				if(m_s[port].pipe &&
						PeekNamedPipe(m_s[port].pipe, NULL, 0, NULL, &avail, NULL) &&
						avail > 0)
				{
					ReadFile(m_s[port].pipe, &chbuf, 1, &avail, NULL);
					data_ready = 1;
				}
				#endif
				break;
		}
		if(data_ready) {
			if(!m_s[port].modem_cntl.local_loopback) {
				rx_fifo_enq(port, chbuf);
			}
		} else {
			if(!m_s[port].fifo_cntl.enable) {
				db_usec = 100000; // Poll frequency is 100ms
			}
		}
	} else { //if((m_s[port].line_status.rxdata_ready == 0) || (m_s[port].fifo_cntl.enable)) {
		// Poll at 4x baud rate to see if the next-char can
		// be read
		db_usec *= 4;
	}

	g_machine.activate_timer(m_s[port].rx_timer_index,
			db_usec, 0); /* not continuous */
}

void Serial::fifo_timer(uint8_t port)
{
	m_s[port].line_status.rxdata_ready = 1;
	raise_interrupt(port, SER_INT_FIFO);
}

void Serial::mouse_motion(int delta_x, int delta_y, int delta_z, uint button_state)
{
	if(mouse_port == -1) {
		PERRF(LOG_COM, "mouse not connected to a serial port\n");
		return;
	}

	// if the DTR and RTS lines aren't up, the mouse doesn't have any power to send packets.
	if(!m_s[mouse_port].modem_cntl.dtr || !m_s[mouse_port].modem_cntl.rts)
		return;

	// scale down the motion
	if((delta_x < -1) || (delta_x > 1))
		delta_x /= 2;
	if((delta_y < -1) || (delta_y > 1))
		delta_y /= 2;

	if(delta_x > 127) delta_x = 127;
	if(delta_y > 127) delta_y = 127;
	if(delta_x < -128) delta_x = -128;
	if(delta_y < -128) delta_y = -128;

	mouse_delayed_dx += delta_x;
	mouse_delayed_dy -= delta_y;
	mouse_delayed_dz = delta_z;
	mouse_buttons = button_state;
	mouse_update = 1;
}

void Serial::update_mouse_data()
{
	int delta_x, delta_y;
	uint8_t b1, b2, b3, button_state, mouse_data[5];
	int bytes, tail;

	if(mouse_delayed_dx > 127) {
		delta_x = 127;
		mouse_delayed_dx -= 127;
	} else if(mouse_delayed_dx < -128) {
		delta_x = -128;
		mouse_delayed_dx += 128;
	} else {
		delta_x = mouse_delayed_dx;
		mouse_delayed_dx = 0;
	}
	if(mouse_delayed_dy > 127) {
		delta_y = 127;
		mouse_delayed_dy -= 127;
	} else if(mouse_delayed_dy < -128) {
		delta_y = -128;
		mouse_delayed_dy += 128;
	} else {
		delta_y = mouse_delayed_dy;
		mouse_delayed_dy = 0;
	}
	button_state = mouse_buttons;

	if(mouse_type != MOUSE_TYPE_SERIAL_MSYS) {
		b1 = (uint8_t) delta_x;
		b2 = (uint8_t) delta_y;
		b3 = (uint8_t) -((int8_t) mouse_delayed_dz);
		mouse_data[0] = 0x40 | ((b1 & 0xc0) >> 6) | ((b2 & 0xc0) >> 4);
		mouse_data[0] |= ((button_state & 0x01) << 5) | ((button_state & 0x02) << 3);
		mouse_data[1] = b1 & 0x3f;
		mouse_data[2] = b2 & 0x3f;
		mouse_data[3] = b3 & 0x0f;
		mouse_data[3] |= ((button_state & 0x04) << 2);
		bytes = 3;
		if(mouse_type == MOUSE_TYPE_SERIAL_WHEEL)
			bytes = 4;
	} else {
		b1 = (uint8_t) (delta_x / 2);
		b2 = (uint8_t) -((int8_t) (delta_y / 2));
		mouse_data[0] = 0x80 | ((~button_state & 0x01) << 2);
		mouse_data[0] |= ((~button_state & 0x06) >> 1);
		mouse_data[1] = b1;
		mouse_data[2] = b2;
		mouse_data[3] = 0;
		mouse_data[4] = 0;
		bytes = 5;
	}

	/* enqueue mouse data in multibyte internal mouse buffer */
	for(int i = 0; i < bytes; i++) {
		tail = (mouse_internal_buffer.head + mouse_internal_buffer.num_elements) % MOUSE_BUFF_SIZE;
		mouse_internal_buffer.buffer[tail] = mouse_data[i];
		mouse_internal_buffer.num_elements++;
	}
	mouse_update = 0;
}
