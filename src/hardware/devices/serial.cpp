/*
 * Copyright (C) 2001-2014  The Bochs Project
 * Copyright (C) 2015-2021  Marco Bortolin
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

// Peter Grehan (grehan@iprg.nokia.com) coded the original version of this
// serial emulation. He implemented a single 8250, and allow terminal
// input/output to stdout on FreeBSD.
// The current version emulates up to 4 UART 16550A with FIFO.

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
	#define INVALID_SOCKET -1
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

#if SER_ENABLE_RAW
	// TODO missing?
	#include "serial_raw.h"
#endif

#include <cstring>
#include <functional>

#define COM1_IRQ 4
#define COM2_IRQ 3
#define COM3_IRQ COM1_IRQ
#define COM4_IRQ COM2_IRQ

IODEVICE_PORTS(Serial) = {
	{ 0x3F8, 0x3FF, PORT_8BIT|PORT_RW }, // COM1
	{ 0x2F8, 0x2FF, PORT_8BIT|PORT_RW }, // COM2
	{ 0x3E8, 0x3EF, PORT_8BIT|PORT_RW }, // COM3
	{ 0x2E8, 0x2EF, PORT_8BIT|PORT_RW }  // COM4
};

static ini_enum_map_t serial_modes = {
	{ "dummy", SER_MODE_DUMMY },
	{ "file", SER_MODE_FILE },
	{ "term", SER_MODE_TERM },
	{ "raw", SER_MODE_RAW },
	{ "mouse", SER_MODE_MOUSE },
	{ "net-client", SER_MODE_NET_CLIENT },
	{ "net-server", SER_MODE_NET_SERVER },
	{ "pipe-client", SER_MODE_PIPE_CLIENT },
	{ "pipe-server", SER_MODE_PIPE_SERVER }
};

Serial::Serial(Devices *_dev)
: IODevice(_dev)
{
	std::memset(&m_s, 0, sizeof(m_s));
	for(int i=0; i<SER_PORTS; i++) {
		m_host[i].port_id = i;
		m_host[i].io_mode = SER_MODE_DUMMY;
		m_host[i].tty_id = -1;
		m_host[i].socket_id = -1;
		m_host[i].output = nullptr;
		m_host[i].tx_timer = NULL_TIMER_HANDLE;
		m_host[i].rx_timer = NULL_TIMER_HANDLE;
		m_host[i].fifo_timer = NULL_TIMER_HANDLE;

		m_s.uart[i].com = SER_COM_DISABLED; // POS determines the COM port number
	}
	m_s.portmap[SER_COM1] = SER_PORT_DISABLED;
	m_s.portmap[SER_COM2] = SER_PORT_DISABLED;
	m_s.portmap[SER_COM3] = SER_PORT_DISABLED;
	m_s.portmap[SER_COM4] = SER_PORT_DISABLED;
}

Serial::~Serial()
{
	close();
}

void Serial::install()
{
	// don't install I/O ports here, POS will do this

	using namespace std::placeholders;
	for(int i=0; i<SER_PORTS; i++) {
		m_host[i].tx_timer = g_machine.register_timer(
			std::bind(&Serial::tx_timer, this, 0, _1),
			std::string(m_host[i].name()) + " TX");

		m_host[i].rx_timer = g_machine.register_timer(
			std::bind(&Serial::rx_timer, this, 0, _1),
			std::string(m_host[i].name()) + " RX");

		m_host[i].fifo_timer = g_machine.register_timer(
			std::bind(&Serial::fifo_timer, this, 0, _1),
			std::string(m_host[i].name()) + " FIFO");
	}
}

void Serial::remove()
{
	close();

	for(int i=0; i<SER_PORTS; i++) {
		set_port(i, SER_COM_DISABLED);
		g_machine.unregister_timer(m_host[i].tx_timer);
		g_machine.unregister_timer(m_host[i].rx_timer);
		g_machine.unregister_timer(m_host[i].fifo_timer);
	}
}

void Serial::config_changed()
{
	// config_changed is called after the system board has initialised the POS
	// so general state and port number are already determined

	close();

	m_enabled = false; // POS determines the general state

	for(int i=0; i<SER_PORTS; i++) {
		uint8_t mode;
		std::string dev, mode_name, dev_name;
		switch(i) {
			case SER_PORT_A: mode_name = SERIAL_A_MODE; dev_name = SERIAL_A_DEV; break;
			case SER_PORT_B: mode_name = SERIAL_B_MODE; dev_name = SERIAL_B_DEV; break;
			case SER_PORT_C: mode_name = SERIAL_C_MODE; dev_name = SERIAL_C_DEV; break;
			case SER_PORT_D: mode_name = SERIAL_D_MODE; dev_name = SERIAL_D_DEV; break;
			default: assert(false); break;
		}
		mode = g_program.config().get_enum(SERIAL_SECTION, mode_name, serial_modes);
		dev = g_program.config().get_string(SERIAL_SECTION, dev_name);

		if(mode != SER_MODE_DUMMY) {
			PINFOF(LOG_V0, LOG_COM, "%s: initializing mode '%s' on device '%s'\n",
					m_host[i].name(),
					g_program.config().get_string(SERIAL_SECTION, mode_name).c_str(),
					dev.empty()?"none":dev.c_str());
		}

		try {
			switch(mode) {
				case SER_MODE_MOUSE:
					if(m_mouse.port != SER_PORT_DISABLED) {
						PERRF(LOG_COM, "%s: mouse already attached set to %s\n",
								m_host[i].name(), m_host[m_mouse.port].name());
						break;
					}
					m_host[i].init_mode_mouse();
					m_s.mouse.detect = 0;
					m_mouse.port = i;
					m_mouse.type = g_program.config().get_enum(GUI_SECTION, GUI_MOUSE_TYPE, g_mouse_types);
					if((m_mouse.type == MOUSE_TYPE_SERIAL) ||
					   (m_mouse.type == MOUSE_TYPE_SERIAL_WHEEL) ||
					   (m_mouse.type == MOUSE_TYPE_SERIAL_MSYS))
					{
						using namespace std::placeholders;
						g_machine.register_mouse_fun(
							std::bind(&Serial::mouse_motion, this, _1, _2, _3),
							std::bind(&Serial::mouse_button, this, _1, _2)
						);
						PINFOF(LOG_V0, LOG_COM, "%s: mouse installed\n", m_host[i].name());
					} else {
						PWARNF(LOG_V0, LOG_COM, "%s: mouse mode is enabled but the mouse type is '%s'\n",
								m_host[i].name(), g_program.config().get_string(GUI_SECTION, GUI_MOUSE_TYPE).c_str());
					}
					break;
				case SER_MODE_FILE:
					m_host[i].init_mode_file(dev);
					break;
				case SER_MODE_TERM:
					m_host[i].init_mode_term(dev);
					break;
				case SER_MODE_RAW:
					m_host[i].init_mode_raw(dev);
					break;
				case SER_MODE_NET_CLIENT:
				case SER_MODE_NET_SERVER:
					m_host[i].init_mode_socket(dev, mode);
					break;
				case SER_MODE_PIPE_CLIENT:
				case SER_MODE_PIPE_SERVER:
					m_host[i].init_mode_pipe(dev, mode);
					break;
				case SER_MODE_DUMMY:
					break;
				default:
					throw std::runtime_error("unknown mode");
			}
		} catch(std::runtime_error &e) {
			PERRF(LOG_COM, "%s: initialization error: %s\n", m_host[i].name(), e.what());
			m_host[i].io_mode = SER_MODE_DUMMY;
			throw;
		}
	}
}

void Serial::close()
{
	for(int i=0; i<SER_PORTS; i++) {
		switch(m_host[i].io_mode) {
			case SER_MODE_MOUSE:
				if((m_mouse.type == MOUSE_TYPE_SERIAL) ||
				   (m_mouse.type == MOUSE_TYPE_SERIAL_WHEEL) ||
				   (m_mouse.type == MOUSE_TYPE_SERIAL_MSYS))
				{
					g_machine.register_mouse_fun(nullptr, nullptr);
				}
				m_mouse.type = MOUSE_TYPE_NONE;
				m_mouse.port = SER_PORT_DISABLED;
				break;
			case SER_MODE_FILE:
				if(m_host[i].output != nullptr) {
					fclose(m_host[i].output);
					m_host[i].output = nullptr;
				}
				break;
			case SER_MODE_TERM:
				#if SERIAL_ENABLE && !SER_WIN32
				if(m_host[i].tty_id >= 0) {
					tcsetattr(m_host[i].tty_id, TCSAFLUSH, &m_host[i].term_orig);
					m_host[i].tty_id = -1;
				}
				#endif
				break;
			case SER_MODE_RAW:
				#if SER_ENABLE_RAW
				delete m_host[i].raw;
				m_host[i].raw = nullptr;
				#endif
				break;
			case SER_MODE_NET_CLIENT:
			case SER_MODE_NET_SERVER:
				if(m_host[i].socket_id != INVALID_SOCKET) {
					closesocket(m_host[i].socket_id);
					m_host[i].socket_id = INVALID_SOCKET;
				}
				break;
			case SER_MODE_PIPE_CLIENT:
			case SER_MODE_PIPE_SERVER:
				#if SER_WIN32
				if(m_host[i].pipe) {
					CloseHandle(m_host[i].pipe);
					m_host[i].pipe = INVALID_HANDLE_VALUE;
				}
				#endif
				break;
			default:
				break;
		}
		m_host[i].io_mode = SER_MODE_DUMMY;
	}
}

void Serial::save_state(StateBuf &_state)
{
	PINFOF(LOG_V1, LOG_COM, "saving state (not implemented)\n");

	StateHeader h;
	h.name = name();
	h.data_size = sizeof(m_s);
	_state.write(&m_s,h);
}

void Serial::restore_state(StateBuf &_state)
{
	PINFOF(LOG_V1, LOG_COM, "restoring state (not implemented)\n");

	//TODO restoring the serial state is a lot more than just copy the m_s struct.
	//for the time being, don't save the machine state while using the serial port
	_state.skip();
}

void Serial::set_port(uint8_t _port, uint8_t _com)
{
	if(_port >= SER_PORTS) {
		PWARNF(LOG_V0, LOG_COM, "Invalid serial port\n");
		return;
	}
	if(_com >= 4 && _com != SER_COM_DISABLED) {
		PWARNF(LOG_V0, LOG_COM, "Invalid serial COM\n");
		return;
	}

	if(m_s.uart[_port].com == _com) {
		return;
	}

	if(m_s.uart[_port].com != SER_COM_DISABLED) {
		IODevice::remove(&ioports()->at(m_s.uart[_port].com), 1);
		g_machine.unregister_irq(m_s.uart[_port].IRQ, m_s.uart[_port].name());
		m_s.portmap[m_s.uart[_port].com] = SER_PORT_DISABLED;
		m_s.uart[_port].com = SER_COM_DISABLED;
	}

	if(_com != SER_COM_DISABLED) {
		if(m_s.portmap[_com] != SER_COM_DISABLED) {
			PWARNF(LOG_V0, LOG_COM, "COM%u is already binded to %s\n",
					_com+1, m_host[m_s.portmap[_com]].name());
			return;
		}
		m_s.portmap[_com] = _port;
		m_s.uart[_port].com = _com;

		IODevice::install(&ioports()->at(_com), 1);
		constexpr uint8_t com_irqs[4] = { COM1_IRQ, COM2_IRQ, COM3_IRQ, COM4_IRQ };
		m_s.uart[_port].IRQ = com_irqs[_com];
		g_machine.register_irq(m_s.uart[_port].IRQ, m_s.uart[_port].name());

		PINFOF(LOG_V0, LOG_COM, "%s at 0x%04x, irq %u (%s)\n",
			m_host[_port].name(),
			ioports()->at(_com).from,
			m_s.uart[_port].IRQ,
			m_s.uart[_port].name()
		);
	}
}

void Serial::set_enabled(bool _enabled)
{
	if(_enabled != m_enabled) {
		PINFOF(LOG_V1, LOG_COM, "Serial interface %s\n", _enabled?"ENABLED":"DISABLED");
		m_enabled = _enabled;
		if(_enabled) {
			reset(DEVICE_SOFT_RESET);
		}
	}
}

void Serial::Port::init_mode_file(std::string dev)
{
	if(dev.empty()) {
		throw std::runtime_error("output file name not specified");
	}

	// tx_timer() opens the output file on demand
	io_mode = SER_MODE_FILE;
	filename = g_program.config().get_file_path(dev, FILE_TYPE_USER);
}

void Serial::Port::init_mode_term(std::string dev)
{
	#if SER_POSIX

	if(dev.empty()) {
		throw std::runtime_error("device name not specified");
	}

	tty_id = ::open(dev.c_str(), O_RDWR|O_NONBLOCK, 600);
	if(tty_id < 0) {
		throw std::runtime_error("open of device '" + dev + "' failed");
	} else {
		tcgetattr(tty_id, &term_orig);
		memcpy(&term_orig, &term_new, sizeof(struct termios));
		term_new.c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP|INLCR|IGNCR|ICRNL|IXON);
		term_new.c_lflag &= ~(ECHO|ECHONL|ICANON|ISIG|IEXTEN);
		term_new.c_cflag &= ~(CSIZE|PARENB);
		term_new.c_cflag |= CS8;
		term_new.c_oflag |= OPOST | ONLCR;  // Enable NL to CR-NL translation
		if(SER_TERM_BRKINT) {
			// Ctrl-C will cause SIGINT and exit the emulator
			term_new.c_iflag &= ~IGNBRK;
			term_new.c_iflag |= BRKINT;
			term_new.c_lflag |= ISIG;
		} else {
			// Ctrl-C will be delivered to the serial comn
			term_new.c_iflag |= IGNBRK;
			term_new.c_iflag &= ~BRKINT;
		}
		term_new.c_iflag = 0;
		term_new.c_oflag = 0;
		term_new.c_cflag = CS8|CREAD|CLOCAL;
		term_new.c_lflag = 0;
		term_new.c_cc[VMIN] = 1;
		term_new.c_cc[VTIME] = 0;
		//term_new.c_iflag |= IXOFF;
		tcsetattr(tty_id, TCSAFLUSH, &term_new);

		io_mode = SER_MODE_TERM;
		PINFOF(LOG_V0, LOG_COM, "%s: opened tty on device '%s' (id:%d)\n", name(), dev.c_str(), tty_id);
	}

	#else

	throw std::runtime_error("tty mode support not available");

	#endif
}

void Serial::Port::init_mode_raw(std::string dev)
{
	#if SER_ENABLE_RAW

	if(dev.empty()) {
		throw std::runtime_error("device name not specified");
	}
	raw = new serial_raw(dev.c_str());
	io_mode = SER_MODE_RAW;

	#else

	throw std::runtime_error("support for raw serial mode not available");

	#endif
}

void Serial::Port::init_mode_mouse()
{
	io_mode = SER_MODE_MOUSE;
}

void Serial::Port::init_mode_socket(std::string dev, uint mode)
{
	if(dev.empty()) {
		throw std::runtime_error("device address not specified");
	}
	
	#if SER_WIN32
	static bool winsock_init = false;
	if(!winsock_init) {
		WORD wVersionRequested;
		WSADATA wsaData;
		int err;
		wVersionRequested = MAKEWORD(2, 0);
		err = WSAStartup(wVersionRequested, &wsaData);
		if(err != 0) {
			throw std::runtime_error("WSAStartup failed");
		}
		PDEBUGF(LOG_V1, LOG_COM, "WinSock initialized\n");
		winsock_init = true;
	}
	#endif

	auto address = str_parse_tokens(dev, ":");

	if(address.size() < 2) {
		throw std::runtime_error("port number missing");
	}
	if(address.size() > 2) {
		throw std::runtime_error("invalid address, must be 'host:port'");
	}

	auto host = address[0];
	unsigned long port = 0;
	try {
		port = std::stoul(address[1]);
	} catch(...) {
		throw std::runtime_error("invalid port number");
	}
	if(port < 1024 || port > 65535) {
		throw std::runtime_error("port number must be between 1024 and 65535");
	}

	struct hostent *hp = gethostbyname(host.c_str());
	if(!hp) {
		throw std::runtime_error("host name lookup failed for '" + host + "'");
	}

	struct sockaddr_in sin;
	memset((char*) &sin, 0, sizeof(sin));
	#if HAVE_SOCKADDR_IN_SIN_LEN
	sin.sin_len = sizeof(sin);
	#endif
	memcpy((char*) &(sin.sin_addr), hp->h_addr, hp->h_length);
	sin.sin_family = hp->h_addrtype;
	sin.sin_port = htons(port);

	SOCKET socket = ::socket(AF_INET, SOCK_STREAM, 0);
	if(socket == INVALID_SOCKET) {
		throw std::runtime_error("socket creation failed for " + dev);
	}

	bool server_mode = (mode == SER_MODE_NET_SERVER);
	if(server_mode) {
		if(::bind(socket, (sockaddr *) &sin, sizeof (sin)) < 0 || ::listen(socket, SOMAXCONN) < 0) {
			closesocket(socket);
			socket = (SOCKET) -1;
			throw std::runtime_error("bind() or listen() failed for " + dev);
		} else {
			PINFOF(LOG_V0, LOG_COM, "%s: waiting for client to connect to host:%s, port:%d\n",
					name(), host.c_str(), port);
			SOCKET client;
			if((client = ::accept(socket, nullptr, 0)) == INVALID_SOCKET) {
				throw std::runtime_error("accept() failed for " + dev);
			}
			closesocket(socket);
			socket = client;
		}
	} else if(::connect(socket, (sockaddr *) &sin, sizeof (sin)) < 0) {
		closesocket(socket);
		socket = (SOCKET) -1;
		throw std::runtime_error("connection to '" + dev + "' failed");
	}

	socket_id = socket;

	if(socket > 0) {
		io_mode = mode;
		PINFOF(LOG_V0, LOG_COM, "%s: inet %s initialized: host:%s, port:%u (socket_id:%d)\n",
				name(), server_mode ? "server" : "client", host.c_str(), port, socket);
	}
}

void Serial::Port::init_mode_pipe(std::string dev, unsigned mode)
{
	#if SER_WIN32

	if(dev.empty()) {
		throw std::runtime_error("pipe device name not specified");
	}
	HANDLE hpipe;
	bool server = (mode == SER_MODE_PIPE_SERVER);
	if(server) {
		hpipe = CreateNamedPipe(dev.c_str(),
				PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
				PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
				1, 4096, 4096, 0, nullptr);

		if(hpipe == INVALID_HANDLE_VALUE) {
			throw std::runtime_error("CreateNamedPipe() failed for " + dev);
		}
		PINFOF(LOG_V0, LOG_COM, "%s: waiting for client to connect to %s\n", name(), dev.c_str());
		if(!ConnectNamedPipe(hpipe, nullptr) && GetLastError() != ERROR_PIPE_CONNECTED) {
			CloseHandle(hpipe);
			pipe = INVALID_HANDLE_VALUE;
			throw std::runtime_error("ConnectNamedPipe() failed for " + dev);
		}
	} else {
		hpipe = CreateFile(dev.c_str(), GENERIC_READ | GENERIC_WRITE,
				0, nullptr, OPEN_EXISTING, 0, nullptr);

		if(hpipe == INVALID_HANDLE_VALUE) {
			throw std::runtime_error("failed to open pipe " + dev);
		}
	}

	if(hpipe != INVALID_HANDLE_VALUE) {
		pipe = hpipe;
		io_mode = mode;
	}

	#else

	throw std::runtime_error("support for 'pipe' modes not available");

	#endif
}

void Serial::lower_interrupt(uint8_t port)
{
	// If there are no more ints pending, clear the irq
	if((m_s.uart[port].rx_interrupt == 0) &&
	   (m_s.uart[port].tx_interrupt == 0) &&
	   (m_s.uart[port].ls_interrupt == 0) &&
	   (m_s.uart[port].ms_interrupt == 0) &&
	   (m_s.uart[port].fifo_interrupt == 0))
	{
		m_devices->pic()->lower_irq(m_s.uart[port].IRQ);
	}
}

void Serial::reset(unsigned)
{
	{
	std::lock_guard<std::mutex> lock(m_mouse.mtx);
	m_mouse.delayed_dx = 0;
	m_mouse.delayed_dy = 0;
	m_mouse.delayed_dz = 0;
	m_mouse.buttons = 0;
	m_mouse.update = false;

	m_s.mouse.buffer.elements = 0;
	std::memset(m_s.mouse.buffer.data, 0, MOUSE_BUFF_SIZE);
	m_s.mouse.buffer.head = 0;
	}

	// Put the UART registers into their RESET state

	for(unsigned i=0; i<SER_PORTS; i++) {

		// internal state
		m_s.uart[i].ls_ipending = 0;
		m_s.uart[i].ms_ipending = 0;
		m_s.uart[i].rx_ipending = 0;
		m_s.uart[i].fifo_ipending = 0;
		m_s.uart[i].ls_interrupt = 0;
		m_s.uart[i].ms_interrupt = 0;
		m_s.uart[i].rx_interrupt = 0;
		m_s.uart[i].tx_interrupt = 0;
		m_s.uart[i].fifo_interrupt = 0;

		// int enable: b0000 0000
		m_s.uart[i].int_enable.rxdata_enable = 0;
		m_s.uart[i].int_enable.txhold_enable = 0;
		m_s.uart[i].int_enable.rxlstat_enable = 0;
		m_s.uart[i].int_enable.modstat_enable = 0;

		// int ID: b0000 0001
		m_s.uart[i].int_ident.ipending = 1;
		m_s.uart[i].int_ident.int_ID = 0;

		// FIFO control: b0000 0000
		m_s.uart[i].fifo_cntl.enable = 0;
		m_s.uart[i].fifo_cntl.rxtrigger = 0;
		m_s.uart[i].rx_fifo_end = 0;
		m_s.uart[i].tx_fifo_end = 0;

		// Line Control reg: b0000 0000
		m_s.uart[i].line_cntl.wordlen_sel = 0;
		m_s.uart[i].line_cntl.stopbits = 0;
		m_s.uart[i].line_cntl.parity_enable = 0;
		m_s.uart[i].line_cntl.evenparity_sel = 0;
		m_s.uart[i].line_cntl.stick_parity = 0;
		m_s.uart[i].line_cntl.break_cntl = 0;
		m_s.uart[i].line_cntl.dlab = 0;

		// Modem Control reg: b0000 0000
		m_s.uart[i].modem_cntl.dtr = 0;
		m_s.uart[i].modem_cntl.rts = 0;
		m_s.uart[i].modem_cntl.out1 = 0;
		m_s.uart[i].modem_cntl.out2 = 0;
		m_s.uart[i].modem_cntl.local_loopback = 0;

		// Line Status register: b0110 0000
		m_s.uart[i].line_status.rxdata_ready = 0;
		m_s.uart[i].line_status.overrun_error = 0;
		m_s.uart[i].line_status.parity_error = 0;
		m_s.uart[i].line_status.framing_error = 0;
		m_s.uart[i].line_status.break_int = 0;
		m_s.uart[i].line_status.thr_empty = 1;
		m_s.uart[i].line_status.tsr_empty = 1;
		m_s.uart[i].line_status.fifo_error = 0;

		// Modem Status register: bXXXX 0000
		m_s.uart[i].modem_status.delta_cts = 0;
		m_s.uart[i].modem_status.delta_dsr = 0;
		m_s.uart[i].modem_status.ri_trailedge = 0;
		m_s.uart[i].modem_status.delta_dcd = 0;
		m_s.uart[i].modem_status.cts = 0;
		m_s.uart[i].modem_status.dsr = 0;
		m_s.uart[i].modem_status.ri = 0;
		m_s.uart[i].modem_status.dcd = 0;

		m_s.uart[i].scratch = 0;      // scratch register
		m_s.uart[i].divisor_lsb = 1;  // divisor-lsb register
		m_s.uart[i].divisor_msb = 0;  // divisor-msb register

		m_s.uart[i].baudrate = 19200;
		m_s.uart[i].databyte_usec = 87;

		memset(m_s.uart[i].rx_fifo, 0, 16);   // receive FIFO (internal)
		memset(m_s.uart[i].tx_fifo, 0, 16);   // transmit FIFO (internal)

		// simulate device connected
		if(m_host[i].io_mode != SER_MODE_RAW) {
			m_s.uart[i].modem_status.cts = 1;
			m_s.uart[i].modem_status.dsr = 1;
		}
	}
}

void Serial::raise_interrupt(uint8_t port, int type)
{
	bool gen_int = false;

	switch(type) {
		case SER_INT_IER: // IER has changed
			gen_int = true;
			break;
		case SER_INT_RXDATA:
			if(m_s.uart[port].int_enable.rxdata_enable) {
				m_s.uart[port].rx_interrupt = true;
				gen_int = true;
			} else {
				m_s.uart[port].rx_ipending = true;
			}
			break;
		case SER_INT_TXHOLD:
			if(m_s.uart[port].int_enable.txhold_enable) {
				m_s.uart[port].tx_interrupt = true;
				gen_int = true;
			}
			break;
		case SER_INT_RXLSTAT:
			if(m_s.uart[port].int_enable.rxlstat_enable) {
				m_s.uart[port].ls_interrupt = true;
				gen_int = true;
			} else {
				m_s.uart[port].ls_ipending = true;
			}
			break;
		case SER_INT_MODSTAT:
			if((m_s.uart[port].ms_ipending) && (m_s.uart[port].int_enable.modstat_enable)) {
				m_s.uart[port].ms_interrupt = true;
				m_s.uart[port].ms_ipending = false;
				gen_int = true;
			}
			break;
		case SER_INT_FIFO:
			if(m_s.uart[port].int_enable.rxdata_enable) {
				m_s.uart[port].fifo_interrupt = true;
				gen_int = true;
			} else {
				m_s.uart[port].fifo_ipending = true;
			}
			break;
		default:
			PDEBUGF(LOG_V0, LOG_COM, "invalid int type!\n");
			return;
	}

	if(gen_int && m_s.uart[port].modem_cntl.out2) {
		constexpr const char * int_names[] = {
			"IER", "RXDATA", "TXHOLD", "RXLSTAT", "MODSTAT", "FIFO"
		};
		PDEBUGF(LOG_V2, LOG_COM, "%s: raising IRQ %u (%s)\n",
			m_s.uart[port].name(), m_s.uart[port].IRQ, int_names[type]
		);
		m_devices->pic()->raise_irq(m_s.uart[port].IRQ);
	}
}

uint16_t Serial::read(uint16_t _address, unsigned _io_len)
{
	UNUSED(_io_len);

	m_devices->sysboard()->set_feedback();

	if(!m_enabled) {
		// POST tests only LCR with port disabled
		// see BIOS at F000:2062, the OUT_PORT_TEST must fail with CF=1
		return 0;
	}

	uint8_t com = 0;
	switch(_address & 0x03f8) {
		case 0x03f8: com = SER_COM1; break;
		case 0x02f8: com = SER_COM2; break;
		case 0x03e8: com = SER_COM3; break;
		case 0x02e8: com = SER_COM4; break;
		default: return 0;
	}
	uint8_t port = m_s.portmap[com];
	if(port == SER_PORT_DISABLED) {
		PDEBUGF(LOG_V0, LOG_COM, "invalid port 0x%x\n", _address);
		return 0;
	}

	PDEBUGF(LOG_V2, LOG_COM, "%s: read  0x%x -> ", m_s.uart[port].name(), _address);

	uint8_t val = 0;

	switch(_address & 0x07) {
		case SER_RBR: // receive buffer, or divisor latch LSB if DLAB set
			if(m_s.uart[port].line_cntl.dlab) {
				val = m_s.uart[port].divisor_lsb;
				PDEBUGF(LOG_V2, LOG_COM, "0x%02x div LSB\n", val);
			} else {
				if(m_s.uart[port].fifo_cntl.enable) {
					val = m_s.uart[port].rx_fifo[0];
					PDEBUGF(LOG_V2, LOG_COM, "0x%02x RX fifo\n", val);
					if(m_s.uart[port].rx_fifo_end > 0) {
						memcpy(&m_s.uart[port].rx_fifo[0], &m_s.uart[port].rx_fifo[1], 15);
						m_s.uart[port].rx_fifo_end--;
					}
					if(m_s.uart[port].rx_fifo_end == 0) {
						m_s.uart[port].line_status.rxdata_ready = 0;
						m_s.uart[port].rx_interrupt = 0;
						m_s.uart[port].rx_ipending = 0;
						m_s.uart[port].fifo_interrupt = 0;
						m_s.uart[port].fifo_ipending = 0;
						lower_interrupt(port);
					}
				} else {
					val = m_s.uart[port].rxbuffer;
					PDEBUGF(LOG_V2, LOG_COM, "0x%02x RX buff\n", val);
					m_s.uart[port].line_status.rxdata_ready = 0;
					m_s.uart[port].rx_interrupt = 0;
					m_s.uart[port].rx_ipending = 0;
					lower_interrupt(port);
				}
			}
			break;

		case SER_IER: // interrupt enable register, or div. latch MSB
			if(m_s.uart[port].line_cntl.dlab) {
				val = m_s.uart[port].divisor_msb;
				PDEBUGF(LOG_V2, LOG_COM, "0x%02x div MSB\n", val);
			} else {
				val = m_s.uart[port].int_enable.rxdata_enable |
					(m_s.uart[port].int_enable.txhold_enable  << 1) |
					(m_s.uart[port].int_enable.rxlstat_enable << 2) |
					(m_s.uart[port].int_enable.modstat_enable << 3);
				PDEBUGF(LOG_V2, LOG_COM, "0x%02x IER %s\n", val,
						bitfield_to_string(val, { "rxdata", "txhold", "rxlstat", "modstat", "", "", "", "" }).c_str());
			}
			break;

		case SER_IIR: // interrupt ID register

			// Set the interrupt ID based on interrupt source

			if(m_s.uart[port].ls_interrupt) {
				m_s.uart[port].int_ident.int_ID = 0x3;
				m_s.uart[port].int_ident.ipending = 0;
			} else if(m_s.uart[port].fifo_interrupt) {
				m_s.uart[port].int_ident.int_ID = 0x6;
				m_s.uart[port].int_ident.ipending = 0;
			} else if(m_s.uart[port].rx_interrupt) {
				m_s.uart[port].int_ident.int_ID = 0x2;
				m_s.uart[port].int_ident.ipending = 0;
			} else if(m_s.uart[port].tx_interrupt) {
				m_s.uart[port].int_ident.int_ID = 0x1;
				m_s.uart[port].int_ident.ipending = 0;
			} else if(m_s.uart[port].ms_interrupt) {
				m_s.uart[port].int_ident.int_ID = 0x0;
				m_s.uart[port].int_ident.ipending = 0;
			} else {
				m_s.uart[port].int_ident.int_ID = 0x0;
				m_s.uart[port].int_ident.ipending = 1;
			}
			m_s.uart[port].tx_interrupt = 0;

			val = m_s.uart[port].int_ident.ipending  |
				(m_s.uart[port].int_ident.int_ID << 1) |
				(m_s.uart[port].fifo_cntl.enable ? 0xc0 : 0x00);
			PDEBUGF(LOG_V2, LOG_COM, "0x%02x IIR int:%x %s\n", val,
					m_s.uart[port].int_ident.int_ID,
					m_s.uart[port].int_ident.ipending?"pending":"");

			lower_interrupt(port);
			break;

		case SER_LCR: // Line control register
			val = m_s.uart[port].line_cntl.wordlen_sel |
				(m_s.uart[port].line_cntl.stopbits       << 2) |
				(m_s.uart[port].line_cntl.parity_enable  << 3) |
				(m_s.uart[port].line_cntl.evenparity_sel << 4) |
				(m_s.uart[port].line_cntl.stick_parity   << 5) |
				(m_s.uart[port].line_cntl.break_cntl     << 6) |
				(m_s.uart[port].line_cntl.dlab           << 7);
			PDEBUGF(LOG_V2, LOG_COM, "0x%02x LCR %s\n", val,
					bitfield_to_string(val, { "wl0", "wl1", "stop", "par", "epar", "spar", "brk", "dlab" }).c_str());
			break;

		case SER_MCR: // MODEM control register
			val = m_s.uart[port].modem_cntl.dtr |
				(m_s.uart[port].modem_cntl.rts << 1) |
				(m_s.uart[port].modem_cntl.out1 << 2) |
				(m_s.uart[port].modem_cntl.out2 << 3) |
				(m_s.uart[port].modem_cntl.local_loopback << 4);
			PDEBUGF(LOG_V2, LOG_COM, "0x%02x MCR %s\n", val,
					bitfield_to_string(val, { "dtr", "rts", "out1", "out2", "loop", "", "", "" }).c_str());
			break;

		case SER_LSR: // Line status register
			val = m_s.uart[port].line_status.rxdata_ready |
				(m_s.uart[port].line_status.overrun_error  << 1) |
				(m_s.uart[port].line_status.parity_error   << 2) |
				(m_s.uart[port].line_status.framing_error  << 3) |
				(m_s.uart[port].line_status.break_int      << 4) |
				(m_s.uart[port].line_status.thr_empty      << 5) |
				(m_s.uart[port].line_status.tsr_empty      << 6) |
				(m_s.uart[port].line_status.fifo_error     << 7);
			PDEBUGF(LOG_V2, LOG_COM, "0x%02x LSR %s\n", val,
					bitfield_to_string(val, { "rxrdy", "ovr", "parerr", "frmerr", "brk", "thre", "tsre", "fifoerr" }).c_str());

			m_s.uart[port].line_status.overrun_error = 0;
			m_s.uart[port].line_status.framing_error = 0;
			m_s.uart[port].line_status.break_int = 0;
			m_s.uart[port].ls_interrupt = 0;
			m_s.uart[port].ls_ipending = 0;
			lower_interrupt(port);
			break;

		case SER_MSR: // MODEM status register
			#if SER_ENABLE_RAW
			if(m_host[port].io_mode == SER_MODE_RAW) {
				bool prev_cts = m_s.uart[port].modem_status.cts;
				bool prev_dsr = m_s.uart[port].modem_status.dsr;
				bool prev_ri  = m_s.uart[port].modem_status.ri;
				bool prev_dcd = m_s.uart[port].modem_status.dcd;

				val = m_host[port].raw->get_modem_status();
				m_s.uart[port].modem_status.cts = (val & 0x10) >> 4;
				m_s.uart[port].modem_status.dsr = (val & 0x20) >> 5;
				m_s.uart[port].modem_status.ri  = (val & 0x40) >> 6;
				m_s.uart[port].modem_status.dcd = (val & 0x80) >> 7;
				if(m_s.uart[port].modem_status.cts != prev_cts) {
					m_s.uart[port].modem_status.delta_cts = 1;
				}
				if(m_s.uart[port].modem_status.dsr != prev_dsr) {
					m_s.uart[port].modem_status.delta_dsr = 1;
				}
				if((m_s.uart[port].modem_status.ri == 0) && (prev_ri == 1))
					m_s.uart[port].modem_status.ri_trailedge = 1;
				if(m_s.uart[port].modem_status.dcd != prev_dcd) {
					m_s.uart[port].modem_status.delta_dcd = 1;
				}
			}
			#endif
			val = m_s.uart[port].modem_status.delta_cts |
				(m_s.uart[port].modem_status.delta_dsr    << 1) |
				(m_s.uart[port].modem_status.ri_trailedge << 2) |
				(m_s.uart[port].modem_status.delta_dcd    << 3) |
				(m_s.uart[port].modem_status.cts          << 4) |
				(m_s.uart[port].modem_status.dsr          << 5) |
				(m_s.uart[port].modem_status.ri           << 6) |
				(m_s.uart[port].modem_status.dcd          << 7);
			PDEBUGF(LOG_V2, LOG_COM, "0x%02x MSR %s\n", val,
					bitfield_to_string(val, { "dcts", "ddsr", "rit", "ddcd", "cts", "dsr", "ri", "dcd" }).c_str());
			m_s.uart[port].modem_status.delta_cts = 0;
			m_s.uart[port].modem_status.delta_dsr = 0;
			m_s.uart[port].modem_status.ri_trailedge = 0;
			m_s.uart[port].modem_status.delta_dcd = 0;
			m_s.uart[port].ms_interrupt = 0;
			m_s.uart[port].ms_ipending = 0;
			lower_interrupt(port);
			break;

		case SER_SCR: // scratch register
			val = m_s.uart[port].scratch;
			PDEBUGF(LOG_V2, LOG_COM, "0x%02x SCR\n", val);
			break;

		default:
			PDEBUGF(LOG_V2, LOG_COM, "0x%02x ???\n", val);
			break;
	}

	return val;
}


void Serial::write(uint16_t _address, uint16_t _value, unsigned _io_len)
{
	UNUSED(_io_len);

	m_devices->sysboard()->set_feedback();

	if(!m_enabled) {
		return;
	}

	uint8_t com = 0;
	switch(_address & 0x03f8) {
		case 0x03f8: com = SER_COM1; break;
		case 0x02f8: com = SER_COM2; break;
		case 0x03e8: com = SER_COM3; break;
		case 0x02e8: com = SER_COM4; break;
		default: return;
	}
	uint8_t port = m_s.portmap[com];
	if(port == SER_PORT_DISABLED) {
		PDEBUGF(LOG_V0, LOG_COM, "invalid port 0x%x\n", _address);
		return;
	}

	PDEBUGF(LOG_V2, LOG_COM, "%s: write 0x%x <- 0x%02x ", m_s.uart[port].name(), _address, _value);

	bool new_b0 =  _value & 0x01;
	bool new_b1 = (_value & 0x02) >> 1;
	bool new_b2 = (_value & 0x04) >> 2;
	bool new_b3 = (_value & 0x08) >> 3;
	bool new_b4 = (_value & 0x10) >> 4;
	bool new_b5 = (_value & 0x20) >> 5;
	bool new_b6 = (_value & 0x40) >> 6;
	bool new_b7 = (_value & 0x80) >> 7;

	switch(_address & 0x07) {
		case SER_THR: // transmit buffer, or divisor latch LSB if DLAB set
			if(m_s.uart[port].line_cntl.dlab) {
				m_s.uart[port].divisor_lsb = _value;
				PDEBUGF(LOG_V2, LOG_COM, "div LSB\n");
			} else {
				PDEBUGF(LOG_V2, LOG_COM, "TX buff\n");
				uint8_t bitmask = 0xff >> (3 - m_s.uart[port].line_cntl.wordlen_sel);
				_value &= bitmask;
				if(m_s.uart[port].line_status.thr_empty) {
					if(m_s.uart[port].fifo_cntl.enable) {
						m_s.uart[port].tx_fifo[m_s.uart[port].tx_fifo_end++] = _value;
					} else {
						m_s.uart[port].thrbuffer = _value;
					}
					m_s.uart[port].line_status.thr_empty = 0;
					if(m_s.uart[port].line_status.tsr_empty) {
						if(m_s.uart[port].fifo_cntl.enable) {
							m_s.uart[port].tsrbuffer = m_s.uart[port].tx_fifo[0];
							memcpy(&m_s.uart[port].tx_fifo[0], &m_s.uart[port].tx_fifo[1], 15);
							m_s.uart[port].line_status.thr_empty = (--m_s.uart[port].tx_fifo_end == 0);
						} else {
							m_s.uart[port].tsrbuffer = m_s.uart[port].thrbuffer;
							m_s.uart[port].line_status.thr_empty = 1;
						}
						m_s.uart[port].line_status.tsr_empty = 0;
						raise_interrupt(port, SER_INT_TXHOLD);
						g_machine.activate_timer(m_host[port].tx_timer,
								uint64_t(m_s.uart[port].databyte_usec)*1_us,
								false); // not continuous
					} else {
						m_s.uart[port].tx_interrupt = 0;
						lower_interrupt(port);
					}
				} else {
					if(m_s.uart[port].fifo_cntl.enable) {
						if(m_s.uart[port].tx_fifo_end < 16) {
							m_s.uart[port].tx_fifo[m_s.uart[port].tx_fifo_end++] = _value;
						} else {
							PWARNF(LOG_V2, LOG_COM, "%s: transmit FIFO overflow\n", m_s.uart[port].name());
						}
					} else {
						PWARNF(LOG_V2, LOG_COM, "%s: write to tx hold register when not empty\n", m_s.uart[port].name());
					}
				}
			}
			break;

		case SER_IER: // interrupt enable register, or div. latch MSB
			if(m_s.uart[port].line_cntl.dlab) {
				m_s.uart[port].divisor_msb = _value;
				PDEBUGF(LOG_V2, LOG_COM, "div MSB\n");
			} else {
				bool gen_int = false;
				PDEBUGF(LOG_V2, LOG_COM, "IER %s\n",
						bitfield_to_string(_value, { "rxdata", "txhold", "rxlstat", "modstat", "", "", "", "" }).c_str());
				if(new_b3 != m_s.uart[port].int_enable.modstat_enable) {
					m_s.uart[port].int_enable.modstat_enable  = new_b3;
					if(m_s.uart[port].int_enable.modstat_enable == 1) {
						if(m_s.uart[port].ms_ipending == 1) {
							m_s.uart[port].ms_interrupt = 1;
							m_s.uart[port].ms_ipending = 0;
							gen_int = true;
						}
					} else {
						if(m_s.uart[port].ms_interrupt == 1) {
							m_s.uart[port].ms_interrupt = 0;
							m_s.uart[port].ms_ipending = 1;
							lower_interrupt(port);
						}
					}
				}
				if(new_b1 != m_s.uart[port].int_enable.txhold_enable) {
					m_s.uart[port].int_enable.txhold_enable  = new_b1;
					if(m_s.uart[port].int_enable.txhold_enable == 1) {
						m_s.uart[port].tx_interrupt = m_s.uart[port].line_status.thr_empty;
						if(m_s.uart[port].tx_interrupt) {
							gen_int = true;
						}
					} else {
						m_s.uart[port].tx_interrupt = 0;
						lower_interrupt(port);
					}
				}
				if(new_b0 != m_s.uart[port].int_enable.rxdata_enable) {
					m_s.uart[port].int_enable.rxdata_enable  = new_b0;
					if(m_s.uart[port].int_enable.rxdata_enable == 1) {
						if(m_s.uart[port].fifo_ipending == 1) {
							m_s.uart[port].fifo_interrupt = 1;
							m_s.uart[port].fifo_ipending = 0;
							gen_int = true;
						}
						if(m_s.uart[port].rx_ipending == 1) {
							m_s.uart[port].rx_interrupt = 1;
							m_s.uart[port].rx_ipending = 0;
							gen_int = true;
						}
					} else {
						if(m_s.uart[port].rx_interrupt == 1) {
							m_s.uart[port].rx_interrupt = 0;
							m_s.uart[port].rx_ipending = 1;
							lower_interrupt(port);
						}
						if(m_s.uart[port].fifo_interrupt == 1) {
							m_s.uart[port].fifo_interrupt = 0;
							m_s.uart[port].fifo_ipending = 1;
							lower_interrupt(port);
						}
					}
				}
				if(new_b2 != m_s.uart[port].int_enable.rxlstat_enable) {
					m_s.uart[port].int_enable.rxlstat_enable  = new_b2;
					if(m_s.uart[port].int_enable.rxlstat_enable == 1) {
						if(m_s.uart[port].ls_ipending == 1) {
							m_s.uart[port].ls_interrupt = 1;
							m_s.uart[port].ls_ipending = 0;
							gen_int = true;
						}
					} else {
						if(m_s.uart[port].ls_interrupt == 1) {
							m_s.uart[port].ls_interrupt = 0;
							m_s.uart[port].ls_ipending = 1;
							lower_interrupt(port);
						}
					}
				}
				if(gen_int) {
					raise_interrupt(port, SER_INT_IER);
				}
			}
			break;

		case SER_FCR: // FIFO control register
			PDEBUGF(LOG_V2, LOG_COM, "FCR %s\n",
					bitfield_to_string(_value, { "en", "rx", "tx", "", "", "", "", "" }).c_str());
			if(new_b0 && !m_s.uart[port].fifo_cntl.enable) {
				PDEBUGF(LOG_V1, LOG_COM, "%s: FIFO enabled\n", m_s.uart[port].name());
				m_s.uart[port].rx_fifo_end = 0;
				m_s.uart[port].tx_fifo_end = 0;
			}
			m_s.uart[port].fifo_cntl.enable = new_b0;
			if(new_b1) {
				m_s.uart[port].rx_fifo_end = 0;
			}
			if(new_b2) {
				m_s.uart[port].tx_fifo_end = 0;
			}
			m_s.uart[port].fifo_cntl.rxtrigger = (_value & 0xc0) >> 6;
			break;

		case SER_LCR: { // Line control register
			uint8_t new_wordlen = _value & 0x03;
			PDEBUGF(LOG_V2, LOG_COM, "LCR %s\n", 
					bitfield_to_string(_value, { "wl0", "wl1", "stop", "par", "epar", "spar", "brk", "dlab" }).c_str());
			#if SER_ENABLE_RAW
			if(m_host[port].io_mode == SER_MODE_RAW) {
				if(m_s.uart[port].line_cntl.wordlen_sel != new_wordlen) {
					m_host[port].raw->set_data_bits(new_wordlen + 5);
				}
				if(new_b2 != m_s.uart[port].line_cntl.stopbits) {
					m_host[port].raw->set_stop_bits(new_b2 ? 2 : 1);
				}
				if((new_b3 != m_s.uart[port].line_cntl.parity_enable) ||
						(new_b4 != m_s.uart[port].line_cntl.evenparity_sel) ||
						(new_b5 != m_s.uart[port].line_cntl.stick_parity))
				{
					uint8_t p_mode;
					if(new_b3 == 0) {
						p_mode = P_NONE;
					} else {
						p_mode = ((value & 0x30) >> 4) + 1;
					}
					m_host[port].raw->set_parity_mode(p_mode);
				}
				if((new_b6 != m_s.uart[port].line_cntl.break_cntl) &&
						(!m_s.uart[port].modem_cntl.local_loopback)) {
					m_host[port].raw->set_break(new_b6);
				}
			}
			#endif
			// These are ignored, but set them up so they can be read back
			m_s.uart[port].line_cntl.stopbits = new_b2;
			m_s.uart[port].line_cntl.parity_enable = new_b3;
			m_s.uart[port].line_cntl.evenparity_sel = new_b4;
			m_s.uart[port].line_cntl.stick_parity = new_b5;
			m_s.uart[port].line_cntl.break_cntl = new_b6;
			if(m_s.uart[port].modem_cntl.local_loopback && m_s.uart[port].line_cntl.break_cntl) {
				m_s.uart[port].line_status.break_int = 1;
				m_s.uart[port].line_status.framing_error = 1;
				rx_fifo_enq(port, 0x00);
			}
			bool restart_timer = false;
			if(!new_b7 && m_s.uart[port].line_cntl.dlab) {
				if((m_s.uart[port].divisor_lsb | m_s.uart[port].divisor_msb) != 0) {
					int new_baudrate = (int)(PC_CLOCK_XTL /
							(16 * ((m_s.uart[port].divisor_msb << 8) | m_s.uart[port].divisor_lsb)));
					if(new_baudrate != m_s.uart[port].baudrate) {
						m_s.uart[port].baudrate = new_baudrate;
						restart_timer = true;
						PDEBUGF(LOG_V1, LOG_COM, "%s: baud rate set to %d\n",
								m_s.uart[port].name(), m_s.uart[port].baudrate);
						#if SER_ENABLE_RAW
						if(m_host[port].io_mode == SER_MODE_RAW) {
							m_host[port].raw->set_baudrate(m_s.uart[port].baudrate);
						}
						#endif
					}
				} else {
					PWARNF(LOG_V1, LOG_COM, "%s: ignoring invalid baud rate divisor\n", m_s.uart[port].name());
				}
			}
			m_s.uart[port].line_cntl.dlab = new_b7;
			if(new_wordlen != m_s.uart[port].line_cntl.wordlen_sel) {
				m_s.uart[port].line_cntl.wordlen_sel = new_wordlen;
				restart_timer = true;
			}
			if(restart_timer) {
				// Start the receive polling process if not already started
				// and there is a valid baudrate.
				m_s.uart[port].databyte_usec = (uint32_t)(1000000.0 / m_s.uart[port].baudrate *
						(m_s.uart[port].line_cntl.wordlen_sel + 7));
				g_machine.activate_timer(m_host[port].rx_timer,
						uint64_t(m_s.uart[port].databyte_usec)*1_us,
						false); // not continuous
			}
			break;
		}

		case SER_MCR: // MODEM control register
			PDEBUGF(LOG_V2, LOG_COM, "MCR %s\n",
					bitfield_to_string(_value, { "dtr", "rts", "out1", "out2", "loop", "", "", "" }).c_str());
			if(m_host[port].io_mode == SER_MODE_MOUSE && 
			  (m_s.uart[port].line_cntl.wordlen_sel == 2 || m_s.uart[port].line_cntl.wordlen_sel == 3))
			{
				if(!m_s.uart[port].modem_cntl.dtr && new_b0) {
					m_s.mouse.detect = 1;
				}
				if((m_s.mouse.detect == 1) && new_b1) {
					m_s.mouse.detect = 2;
				}
			}
			#if SER_ENABLE_RAW
			bool mcr_changed = false;
			if(m_host[port].io_mode == SER_MODE_RAW) {
				mcr_changed = (m_s.uart[port].modem_cntl.dtr != new_b0) |
						(m_s.uart[port].modem_cntl.rts != new_b1);
			}
			#endif
			m_s.uart[port].modem_cntl.dtr  = new_b0;
			m_s.uart[port].modem_cntl.rts  = new_b1;
			m_s.uart[port].modem_cntl.out1 = new_b2;
			m_s.uart[port].modem_cntl.out2 = new_b3;

			if(new_b4 != m_s.uart[port].modem_cntl.local_loopback) {
				m_s.uart[port].modem_cntl.local_loopback = new_b4;
				if(m_s.uart[port].modem_cntl.local_loopback) {
					// transition to loopback mode
					#if SER_ENABLE_RAW
					if(m_host[port].io_mode == SER_MODE_RAW) {
						if(m_s.uart[port].modem_cntl.dtr || m_s.uart[port].modem_cntl.rts) {
							m_host[port].raw->set_modem_control(0);
						}
					}
					#endif
					if(m_s.uart[port].line_cntl.break_cntl) {
						#if SER_ENABLE_RAW
						if(m_host[port].io_mode == SER_MODE_RAW) {
							m_host[port].raw->set_break(0);
						}
						#endif
						m_s.uart[port].line_status.break_int = 1;
						m_s.uart[port].line_status.framing_error = 1;
						rx_fifo_enq(port, 0x00);
					}
				} else {
					// transition to normal mode
					#if SER_ENABLE_RAW
					if(m_host[port].io_mode == SER_MODE_RAW) {
						mcr_changed = true;
						if(m_s.uart[port].line_cntl.break_cntl) {
							m_host[port].raw->set_break(0);
						}
					}
					#endif
				}
			}

			if(m_s.uart[port].modem_cntl.local_loopback) {
				bool prev_cts = m_s.uart[port].modem_status.cts;
				bool prev_dsr = m_s.uart[port].modem_status.dsr;
				bool prev_ri  = m_s.uart[port].modem_status.ri;
				bool prev_dcd = m_s.uart[port].modem_status.dcd;
				m_s.uart[port].modem_status.cts = m_s.uart[port].modem_cntl.rts;
				m_s.uart[port].modem_status.dsr = m_s.uart[port].modem_cntl.dtr;
				m_s.uart[port].modem_status.ri  = m_s.uart[port].modem_cntl.out1;
				m_s.uart[port].modem_status.dcd = m_s.uart[port].modem_cntl.out2;
				if(m_s.uart[port].modem_status.cts != prev_cts) {
					m_s.uart[port].modem_status.delta_cts = 1;
					m_s.uart[port].ms_ipending = 1;
				}
				if(m_s.uart[port].modem_status.dsr != prev_dsr) {
					m_s.uart[port].modem_status.delta_dsr = 1;
					m_s.uart[port].ms_ipending = 1;
				}
				if(m_s.uart[port].modem_status.ri != prev_ri)
					m_s.uart[port].ms_ipending = 1;
				if((m_s.uart[port].modem_status.ri == 0) && (prev_ri == 1))
					m_s.uart[port].modem_status.ri_trailedge = 1;
				if(m_s.uart[port].modem_status.dcd != prev_dcd) {
					m_s.uart[port].modem_status.delta_dcd = 1;
					m_s.uart[port].ms_ipending = 1;
				}
				raise_interrupt(port, SER_INT_MODSTAT);
			} else {
				if(m_host[port].io_mode == SER_MODE_MOUSE) {
					if(m_s.mouse.detect == 2) {
						PDEBUGF(LOG_V1, LOG_COM, "%s: mouse detection mode\n",
								m_host[port].name());
						std::lock_guard<std::mutex> lock(m_mouse.mtx);
						if((m_mouse.type == MOUSE_TYPE_SERIAL) || 
						   (m_mouse.type == MOUSE_TYPE_SERIAL_MSYS))
						{
							m_s.mouse.buffer.head = 0;
							m_s.mouse.buffer.elements = 1;
							m_s.mouse.buffer.data[0] = 'M';
						} else if(m_mouse.type == MOUSE_TYPE_SERIAL_WHEEL) {
							m_s.mouse.buffer.head = 0;
							m_s.mouse.buffer.elements = 6;
							m_s.mouse.buffer.data[0] = 'M';
							m_s.mouse.buffer.data[1] = 'Z';
							m_s.mouse.buffer.data[2] = '@';
							m_s.mouse.buffer.data[3] = '\0';
							m_s.mouse.buffer.data[4] = '\0';
							m_s.mouse.buffer.data[5] = '\0';
						}
						g_machine.activate_timer(m_host[port].rx_timer,
								uint64_t(m_s.uart[port].databyte_usec)*1_us,
								false); // not continuous
						m_s.mouse.detect = 0;
					}
				}

				if(m_host[port].io_mode == SER_MODE_RAW) {
					#if SER_ENABLE_RAW
					if(mcr_changed) {
						m_host[port].raw->set_modem_control(value & 0x03);
					}
					#endif
				} else {
					// simulate device connected
					m_s.uart[port].modem_status.cts = 1;
					m_s.uart[port].modem_status.dsr = 1;
					m_s.uart[port].modem_status.ri  = 0;
					m_s.uart[port].modem_status.dcd = 0;
				}
			}
			break;

		case SER_LSR: // Line status register
			PDEBUGF(LOG_V2, LOG_COM, "LSR\n");
			PWARNF(LOG_V0, LOG_COM, "%s: write to line status register ignored\n", m_s.uart[port].name());
			break;

		case SER_MSR: // MODEM status register
			PDEBUGF(LOG_V2, LOG_COM, "MSR\n");
			PWARNF(LOG_V0, LOG_COM, "%s: write to MODEM status register ignored\n", m_s.uart[port].name());
			break;

		case SER_SCR: // scratch register
			PDEBUGF(LOG_V2, LOG_COM, "SCR\n");
			m_s.uart[port].scratch = _value;
			break;

		default:
			PDEBUGF(LOG_V2, LOG_COM, "???\n");
			break;
	}
}

void Serial::rx_fifo_enq(uint8_t port, uint8_t data)
{
	bool gen_int = false;

	if(m_s.uart[port].fifo_cntl.enable) {
		if(m_s.uart[port].rx_fifo_end == 16) {
			PWARNF(LOG_V2, LOG_COM, "%s: receive FIFO overflow\n", m_s.uart[port].name());
			m_s.uart[port].line_status.overrun_error = true;
			raise_interrupt(port, SER_INT_RXLSTAT);
		} else {
			m_s.uart[port].rx_fifo[m_s.uart[port].rx_fifo_end++] = data;
			switch (m_s.uart[port].fifo_cntl.rxtrigger) {
				case 1:
					if(m_s.uart[port].rx_fifo_end == 4) {
						gen_int = true;
					}
					break;
				case 2:
					if(m_s.uart[port].rx_fifo_end == 8) {
						gen_int = true;
					}
					break;
				case 3:
					if(m_s.uart[port].rx_fifo_end == 14) {
						gen_int = true;
					}
					break;
				default:
					gen_int = true;
					break;
			}
			if(gen_int) {
				g_machine.deactivate_timer(m_host[port].fifo_timer);
				m_s.uart[port].line_status.rxdata_ready = true;
				raise_interrupt(port, SER_INT_RXDATA);
			} else {
				g_machine.activate_timer(m_host[port].fifo_timer,
						uint64_t(m_s.uart[port].databyte_usec * 3)*1_us,
						false); // not continuous
			}
		}
	} else {
		if(m_s.uart[port].line_status.rxdata_ready) {
			PWARNF(LOG_V2, LOG_COM, "%s: overrun error\n", m_s.uart[port].name());
			m_s.uart[port].line_status.overrun_error = true;
			raise_interrupt(port, SER_INT_RXLSTAT);
		}
		m_s.uart[port].rxbuffer = data;
		m_s.uart[port].line_status.rxdata_ready = true;
		raise_interrupt(port, SER_INT_RXDATA);
	}
}

void Serial::tx_timer(uint8_t port, uint64_t)
{
	bool gen_int = false;

	if(m_s.uart[port].modem_cntl.local_loopback) {
		rx_fifo_enq(port, m_s.uart[port].tsrbuffer);
	} else {
		switch (m_host[port].io_mode) {
			case SER_MODE_FILE:
				if(m_host[port].output == nullptr) {
					assert(!m_host[port].filename.empty()); // filename must be set at init!
					m_host[port].output = fopen(m_host[port].filename.c_str(), "wb");
					if(m_host[port].output == nullptr) {
						PERRF(LOG_COM, "%s: could not open file '%s' to write\n",
								m_host[port].name(), m_host[port].filename.c_str());
						m_host[port].io_mode = SER_MODE_DUMMY;
						break;
					} else {
						PINFOF(LOG_V0, LOG_COM, "%s: opened output file '%s'\n",
								m_host[port].name(), m_host[port].filename.c_str());
					}
				}
				fputc(m_s.uart[port].tsrbuffer, m_host[port].output);
				fflush(m_host[port].output);
				break;
			case SER_MODE_TERM:
				#if SER_POSIX
				PDEBUGF(LOG_V1, LOG_COM, "%s: term write: '%c'\n", m_host[port].name(), m_s.uart[port].tsrbuffer);
				if(m_host[port].tty_id >= 0) {
					ssize_t res = ::write(m_host[port].tty_id, (void*) & m_s.uart[port].tsrbuffer, 1);
					if(res != 1) {
						PWARNF(LOG_V1, LOG_COM, "%s: term write failed!\n", m_host[port].name());
					}
				}
				#endif
				break;
			case SER_MODE_RAW:
				#if SER_ENABLE_RAW
				if(m_host[port].raw->ready_transmit()) {
					m_host[port].raw->transmit(m_s.uart[port].tsrbuffer);
				} else {
					PWARNF(LOG_V0, LOG_COM, "%s: not ready to transmit\n", m_host[port].name());
				}
				#endif
				break;
			case SER_MODE_MOUSE:
				PDEBUGF(LOG_V1, LOG_COM, "%s: write to mouse ignored: 0x%02x\n", m_host[port].name(), m_s.uart[port].tsrbuffer);
				break;
			case SER_MODE_NET_CLIENT:
			case SER_MODE_NET_SERVER:
				if(m_host[port].socket_id >= 0) {
					PDEBUGF(LOG_V1, LOG_COM, "%s: sock write: %02x\n", m_host[port].name(), m_s.uart[port].tsrbuffer);
					#if SER_WIN32
					::send(m_s[port].socket_id, (const char*) & m_s[port].tsrbuffer, 1, 0);
					#else
					ssize_t res = ::write(m_host[port].socket_id, (void*)&m_s.uart[port].tsrbuffer, 1);
					if(res != 1) {
						PWARNF(LOG_V1, LOG_COM, "%s: sock write failed!\n", m_host[port].name());
					}
					#endif
				}
				break;
			case SER_MODE_PIPE_CLIENT:
			case SER_MODE_PIPE_SERVER:
				#if SER_WIN32
				if(m_host[port].pipe) {
					DWORD written;
					WriteFile(m_host[port].pipe, (void*)&m_s.uart[port].tsrbuffer, 1, &written, nullptr);
				}
				#endif
				break;
			default:
				break;
		}
	}

	m_s.uart[port].line_status.tsr_empty = true;
	if(m_s.uart[port].fifo_cntl.enable && (m_s.uart[port].tx_fifo_end > 0)) {
		m_s.uart[port].tsrbuffer = m_s.uart[port].tx_fifo[0];
		m_s.uart[port].line_status.tsr_empty = false;
		memcpy(&m_s.uart[port].tx_fifo[0], &m_s.uart[port].tx_fifo[1], 15);
		gen_int = (--m_s.uart[port].tx_fifo_end == 0);
	} else if(!m_s.uart[port].line_status.thr_empty) {
		m_s.uart[port].tsrbuffer = m_s.uart[port].thrbuffer;
		m_s.uart[port].line_status.tsr_empty = false;
		gen_int = true;
	}
	if(!m_s.uart[port].line_status.tsr_empty) {
		if(gen_int) {
			m_s.uart[port].line_status.thr_empty = true;
			raise_interrupt(port, SER_INT_TXHOLD);
		}
		// not continuous
		g_machine.activate_timer(m_host[port].tx_timer,
				uint64_t(m_s.uart[port].databyte_usec)*1_us,
				false);
	}
}

void Serial::rx_timer(uint8_t port, uint64_t)
{
	#if HAVE_SYS_SELECT_H && SERIAL_ENABLE
	struct timeval tval;
	fd_set fds;
	#endif
	bool data_ready = false;
	int db_usec = m_s.uart[port].databyte_usec;
	unsigned char chbuf = 0;

	if(m_host[port].io_mode == SER_MODE_TERM) {
		#if HAVE_SYS_SELECT_H && SERIAL_ENABLE
		tval.tv_sec  = 0;
		tval.tv_usec = 0;
		FD_ZERO(&fds);
		if(m_host[port].tty_id >= 0) {
			FD_SET(m_host[port].tty_id, &fds);
		}
		#endif
	}
	if((!m_s.uart[port].line_status.rxdata_ready) || m_s.uart[port].fifo_cntl.enable) {
		switch (m_host[port].io_mode) {
			case SER_MODE_NET_CLIENT:
			case SER_MODE_NET_SERVER:
				#if HAVE_SYS_SELECT_H && SERIAL_ENABLE
				if(!m_s.uart[port].line_status.rxdata_ready) {
					tval.tv_sec  = 0;
					tval.tv_usec = 0;
					FD_ZERO(&fds);
					SOCKET socketid = m_host[port].socket_id;
					if(socketid >= 0) {
						FD_SET(socketid, &fds);
					}
					if((socketid >= 0) && (select(socketid+1, &fds, nullptr, nullptr, &tval) == 1)) {
						ssize_t bytes = (ssize_t)
						#if SER_WIN32
							::recv(socketid, (char*) &chbuf, 1, 0);
						#else
							::read(socketid, &chbuf, 1);
						#endif
						if(bytes > 0) {
							PDEBUGF(LOG_V1, LOG_COM, "%s: sock read: %02x\n", m_host[port].name(), chbuf);
							data_ready = true;
						}
					}
				}
				#endif
				break;
			case SER_MODE_RAW:
				#if SER_ENABLE_RAW
				int data;
				if((data_ready = m_host[port].raw->ready_receive())) {
					data = m_host[port].raw->receive();
					if(data < 0) {
						data_ready = 0;
						switch (data) {
							case RAW_EVENT_BREAK:
								m_s.uart[port].line_status.break_int = 1;
								raise_interrupt(port, SER_INT_RXLSTAT);
								break;
							case RAW_EVENT_FRAME:
								m_s.uart[port].line_status.framing_error = 1;
								raise_interrupt(port, SER_INT_RXLSTAT);
								break;
							case RAW_EVENT_OVERRUN:
								m_s.uart[port].line_status.overrun_error = 1;
								raise_interrupt(port, SER_INT_RXLSTAT);
								break;
							case RAW_EVENT_PARITY:
								m_s.uart[port].line_status.parity_error = 1;
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
							default:
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
				if((m_host[port].tty_id >= 0) && (select(m_host[port].tty_id + 1, &fds, nullptr, nullptr, &tval) == 1)) {
					ssize_t res = ::read(m_host[port].tty_id, &chbuf, 1);
					if(res == 1) {
						PDEBUGF(LOG_V1, LOG_COM, "%s: term read: '%c'\n", m_host[port].name(), chbuf);
						data_ready = true;
					} else {
						PWARNF(LOG_V0, LOG_COM, "%s: error reading from term\n", m_host[port].name());
					}
				}
				#endif
				break;
			case SER_MODE_MOUSE: {
				std::lock_guard<std::mutex> lock(m_mouse.mtx);
				if(m_mouse.update && (m_s.mouse.buffer.elements == 0)) {
					update_mouse_data();
				}
				if(m_s.mouse.buffer.elements > 0) {
					chbuf = m_s.mouse.buffer.data[m_s.mouse.buffer.head];
					m_s.mouse.buffer.head = (m_s.mouse.buffer.head + 1) % MOUSE_BUFF_SIZE;
					m_s.mouse.buffer.elements--;
					PDEBUGF(LOG_V1, LOG_COM, "%s: mouse read: 0x%02x\n", m_host[port].name(), chbuf);
					data_ready = true;
				}
				break;
			}
			case SER_MODE_PIPE_CLIENT:
			case SER_MODE_PIPE_SERVER:
				#if SER_WIN32
				DWORD avail = 0;
				if(m_s[port].pipe &&
						PeekNamedPipe(m_s[port].pipe, nullptr, 0, nullptr, &avail, nullptr) &&
						avail > 0)
				{
					ReadFile(m_s[port].pipe, &chbuf, 1, &avail, nullptr);
					data_ready = true;
				}
				#endif
				break;
			default:
				break;
		}
		if(data_ready) {
			if(!m_s.uart[port].modem_cntl.local_loopback) {
				rx_fifo_enq(port, chbuf);
			}
		} else {
			if(!m_s.uart[port].fifo_cntl.enable) {
				// TODO why? is it an attempt to solve some net related issue?
				db_usec = 100000; // Poll frequency is 100ms
			}
		}
	} else {
		// Poll at 4x baud rate to see if the next-char can be read
		// TODO why? is it an attempt to solve some net related issue? 
		db_usec *= 4;
	}

	if(db_usec != m_s.uart[port].databyte_usec) {
		PDEBUGF(LOG_V2, LOG_COM, "%s: next rx timer: %u us\n", m_host[port].name(), db_usec);
	}
	// not continuous
	g_machine.activate_timer(m_host[port].rx_timer, uint64_t(db_usec)*1_us, false);
}

void Serial::fifo_timer(uint8_t port, uint64_t)
{
	m_s.uart[port].line_status.rxdata_ready = 1;
	raise_interrupt(port, SER_INT_FIFO);
}

void Serial::mouse_button(MouseButton _button, bool _state)
{
	// This function is called by the GUI thread

	if(m_mouse.port == SER_PORT_DISABLED) {
		// This condition should not happen. Evts are fired only if mouse is enabled at init.
		assert(false);
		PERRF(LOG_COM, "Mouse not connected to a serial port\n");
		return;
	}

	// if the DTR and RTS lines aren't up, the mouse doesn't have any power to send packets.
	if(!m_s.uart[m_mouse.port].modem_cntl.dtr || !m_s.uart[m_mouse.port].modem_cntl.rts) {
		PDEBUGF(LOG_V2, LOG_COM, "%s: mouse button: ignored (dtr/rts not up)\n",
			m_host[m_mouse.port].name());
		return;
	}

	std::lock_guard<std::mutex> lock(m_mouse.mtx);

	int btnid = ec_to_i(_button) - 1;
	m_mouse.buttons &= ~(1 << btnid);
	m_mouse.buttons |= (_state << btnid);
	m_mouse.update = true;

	PDEBUGF(LOG_V2, LOG_COM, "%s: mouse button: id=%u, state=%u\n", m_host[m_mouse.port].name(),
		ec_to_i(_button), _state);
}

void Serial::mouse_motion(int delta_x, int delta_y, int delta_z)
{
	// This function is called by the GUI thread

	if(m_mouse.port == SER_PORT_DISABLED) {
		// This condition should not happen. Evts are fired only if mouse is enabled at init.
		assert(false);
		PERRF(LOG_COM, "Mouse not connected to a serial port\n");
		return;
	}

	if((delta_x==0) && (delta_y==0) && (delta_z==0))
	{
		PDEBUGF(LOG_V2, LOG_COM, "%s: mouse motion: useless call. ignoring.\n",
			m_host[m_mouse.port].name());
		return;
	}

	// if the DTR and RTS lines aren't up, the mouse doesn't have any power to send packets.
	if(!m_s.uart[m_mouse.port].modem_cntl.dtr || !m_s.uart[m_mouse.port].modem_cntl.rts) {
		PDEBUGF(LOG_V2, LOG_COM, "%s: mouse motion: ignored (dtr/rts not up)\n",
			m_host[m_mouse.port].name());
		return;
	}

	PDEBUGF(LOG_V2, LOG_COM, "%s: mouse motion: d:[%d,%d,%d]->", m_host[m_mouse.port].name(),
		delta_x, delta_y, delta_z);

	// scale down the motion
	if((delta_x < -1) || (delta_x > 1)) {
		delta_x /= 2;
	}
	if((delta_y < -1) || (delta_y > 1)) {
		delta_y /= 2;
	}

	if(delta_x > 127)  { delta_x = 127;  }
	if(delta_y > 127)  { delta_y = 127;  }
	if(delta_x < -128) { delta_x = -128; }
	if(delta_y < -128) { delta_y = -128; }

	std::lock_guard<std::mutex> lock(m_mouse.mtx);

	m_mouse.delayed_dx += delta_x;
	m_mouse.delayed_dy -= delta_y;
	m_mouse.delayed_dz  = delta_z;
	m_mouse.update = true;

	PDEBUGF(LOG_V2, LOG_COM, "[%d,%d], delayed:[%d,%d,%d]\n",
		delta_x, delta_y, m_mouse.delayed_dx, m_mouse.delayed_dy, m_mouse.delayed_dz);
}

void Serial::update_mouse_data()
{
	// This function is called by the Machine thread
	// mouse mutex must be locked by the caller!

	int delta_x, delta_y;
	uint8_t b1, b2, b3, button_state;
	if(m_mouse.delayed_dx > 127) {
		delta_x = 127;
		m_mouse.delayed_dx -= 127;
	} else if(m_mouse.delayed_dx < -128) {
		delta_x = -128;
		m_mouse.delayed_dx += 128;
	} else {
		delta_x = m_mouse.delayed_dx;
		m_mouse.delayed_dx = 0;
	}
	if(m_mouse.delayed_dy > 127) {
		delta_y = 127;
		m_mouse.delayed_dy -= 127;
	} else if(m_mouse.delayed_dy < -128) {
		delta_y = -128;
		m_mouse.delayed_dy += 128;
	} else {
		delta_y = m_mouse.delayed_dy;
		m_mouse.delayed_dy = 0;
	}
	button_state = m_mouse.buttons;

	PDEBUGF(LOG_V2, LOG_COM, "%s: mouse d:[%d,%d", m_host[m_mouse.port].name(),
		delta_x, delta_y);
	int nbytes;
	uint8_t mouse_data[5];
	if(m_mouse.type != MOUSE_TYPE_SERIAL_MSYS) {
		b1 = (uint8_t) delta_x;
		b2 = (uint8_t) delta_y;
		b3 = (uint8_t) -((int8_t) m_mouse.delayed_dz);
		mouse_data[0] = 0x40 | ((b1 & 0xc0) >> 6) | ((b2 & 0xc0) >> 4);
		mouse_data[0] |= ((button_state & 0x01) << 5) | ((button_state & 0x02) << 3);
		mouse_data[1] = b1 & 0x3f;
		mouse_data[2] = b2 & 0x3f;
		mouse_data[3] = b3 & 0x0f;
		mouse_data[3] |= ((button_state & 0x04) << 2);
		nbytes = 3;
		if(m_mouse.type == MOUSE_TYPE_SERIAL_WHEEL) {
			nbytes = 4;
			PDEBUGF(LOG_V2, LOG_COM, ",%d", m_mouse.delayed_dz);
		}
	} else {
		b1 = (uint8_t) (delta_x / 2);
		b2 = (uint8_t) -((int8_t) (delta_y / 2));
		mouse_data[0] = 0x80 | ((~button_state & 0x01) << 2);
		mouse_data[0] |= ((~button_state & 0x06) >> 1);
		mouse_data[1] = b1;
		mouse_data[2] = b2;
		mouse_data[3] = 0;
		mouse_data[4] = 0;
		nbytes = 5;
	}

	// enqueue mouse data in multibyte internal mouse buffer
	PDEBUGF(LOG_V2, LOG_COM, "], b:0x%x, data:0x[", button_state);
	for(int i = 0; i < nbytes; i++) {
		int tail = (m_s.mouse.buffer.head + m_s.mouse.buffer.elements) % MOUSE_BUFF_SIZE;
		m_s.mouse.buffer.data[tail] = mouse_data[i];
		m_s.mouse.buffer.elements++;
		PDEBUGF(LOG_V2, LOG_COM, "%02x%s", mouse_data[i],
			(m_s.mouse.buffer.elements >= MOUSE_BUFF_SIZE)?" OF":"");
		if(i < nbytes - 1) {
			PDEBUGF(LOG_V2, LOG_COM, ",");
		}
	}
	PDEBUGF(LOG_V2, LOG_COM, "]\n");

	m_mouse.update = false;
}
