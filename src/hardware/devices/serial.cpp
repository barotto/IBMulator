/*
 * Copyright (C) 2001-2014  The Bochs Project
 * Copyright (C) 2015-2025  Marco Bortolin
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
	#include <unistd.h>
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

Serial::Serial(Devices *_dev)
: IODevice(_dev)
{
	memset(&m_s, 0, sizeof(m_s));
}

Serial::~Serial()
{
	for(unsigned p=0; p<SER_PORTS; p++) {
		close(p);
	}
}

void Serial::install()
{
	// don't install I/O ports here, POS will do this

	using namespace std::placeholders;
	for(unsigned p=0; p<SER_PORTS; p++) {
		m_s.uart[p].com = COM_DISABLED; // POS determines the COM port number

		m_host[p].port_id = 1 << p;
		m_host[p].present = p == 0; // presence is determined in config_change(), port A default present
		m_host[p].io_mode = MODE_NONE;
		m_host[p].tty_id = -1;
		m_host[p].network.set_log_name(m_host[p].name());
		m_host[p].network.set_mex_callback([](std::string _mex){
			GUI::instance()->show_message(_mex.c_str());
		});
		m_host[p].modem.set_MSR_callback(std::bind(&Serial::set_MSR, this, p, _1));
		m_host[p].output = nullptr;
		m_host[p].tx_timer = g_machine.register_timer(
			std::bind(&Serial::tx_timer, this, p, _1),
			std::string(name()) + m_host[p].name() + " TX");
		m_host[p].rx_timer = g_machine.register_timer(
			std::bind(&Serial::rx_timer, this, p, _1),
			std::string(name()) + m_host[p].name() + " RX");
		m_host[p].fifo_timer = g_machine.register_timer(
			std::bind(&Serial::fifo_timer, this, p, _1),
			std::string(name()) + m_host[p].name() + " FIFO");
	}
	m_s.portmap[COM1] = PORT_DISABLED;
	m_s.portmap[COM2] = PORT_DISABLED;
	m_s.portmap[COM3] = PORT_DISABLED;
	m_s.portmap[COM4] = PORT_DISABLED;
	m_s.enabled = false;

	PINFOF(LOG_V0, LOG_COM, "Installed serial interface.\n");
}

void Serial::remove()
{
	for(unsigned port=0; port<SER_PORTS; port++) {
		close(port);
		bind_port(1 << port, COM_DISABLED);
		g_machine.unregister_timer(m_host[port].tx_timer);
		g_machine.unregister_timer(m_host[port].rx_timer);
		g_machine.unregister_timer(m_host[port].fifo_timer);
	}
}

void Serial::config_changed()
{
	int mouse_type = g_program.config().get_enum(GUI_SECTION, GUI_MOUSE_TYPE, g_mouse_types);
	bool mouse_serial =
		(mouse_type == MOUSE_TYPE_SERIAL) || 
		(mouse_type == MOUSE_TYPE_SERIAL_WHEEL) || 
		(mouse_type == MOUSE_TYPE_SERIAL_MSYS);

	if(m_mouse.port != PORT_DISABLED) {
		close(m_mouse.port);
	}

	for(unsigned p=0; p<SER_PORTS; p++) {
		m_host[p].present = (g_machine.model().serial >> p) & 1;
	}

	unsigned modem_port = PORT_DISABLED;
	unsigned speak_port = PORT_DISABLED;

	double tx_delay = g_program.initial_config().get_real_or_default(SERIAL_SECTION, SERIAL_TX_DELAY);
	bool tcp_nodelay = g_program.initial_config().get_bool_or_default(SERIAL_SECTION, SERIAL_TCP_NODELAY);

	for(unsigned p=0; p<SER_PORTS; p++) {
		const std::string mode_name[] = {
			SERIAL_A_MODE,SERIAL_B_MODE,SERIAL_C_MODE,SERIAL_D_MODE
		}, dev_name[] = {
			SERIAL_A_DEV,SERIAL_B_DEV,SERIAL_C_DEV,SERIAL_D_DEV
		}, dump_name[] = {
			SERIAL_A_DUMP,SERIAL_B_DUMP,SERIAL_C_DUMP,SERIAL_D_DUMP
		};

		auto initial_mode_str = g_program.initial_config().get_string(SERIAL_SECTION, mode_name[p]);
		auto new_mode_str = g_program.config().get_string(SERIAL_SECTION, mode_name[p]);
		auto dump_file = g_program.config().get_string(SERIAL_SECTION, dump_name[p]);
		if(!dump_file.empty()) {
			m_host[p].dump = FileSys::make_ofstream((g_program.config().get_cfg_home() + FS_SEP + dump_file).c_str(), std::ofstream::binary);
		}

		unsigned new_mode = g_program.config().get_enum(SERIAL_SECTION, mode_name[p], {
			{ "none",  MODE_NONE  },
			{ "dummy", MODE_DUMMY },
			{ "mouse", MODE_MOUSE },
			{ "file",  MODE_FILE  },
			{ "term",  MODE_TERM  },
			// TODO { "raw", MODE_RAW },
			{ "net-client",  MODE_NET_CLIENT  },
			{ "net-server",  MODE_NET_SERVER  },
			{ "pipe-client", MODE_PIPE_CLIENT },
			{ "pipe-server", MODE_PIPE_SERVER },
			{ "modem", MODE_MODEM },
			{ "speak", MODE_SPEAK }
		}, MODE_INVALID);

		std::string dev;

		if(new_mode == MODE_INVALID) {
			if(new_mode_str.empty() || new_mode_str == "auto") {
				// empty and "auto" are valid in initial config
				// assuming initial config, because restore state has this setting resolved and valid
				if(mouse_serial && m_mouse.port == PORT_DISABLED) {
					new_mode = MODE_MOUSE;
					new_mode_str = "mouse";
				} else {
					if(m_host[p].present) {
						new_mode = MODE_DUMMY;
						new_mode_str = "dummy";
					} else {
						new_mode = MODE_NONE;
						new_mode_str = "none";
					}
				}
			} else {
				PERRF(LOG_COM, "%s: mode '%s' is invalid\n", m_host[p].name(), new_mode_str.c_str());
				throw std::exception();
			}
		} else {
			if(new_mode == MODE_MODEM && modem_port != PORT_DISABLED) {
				// only 1 modem allowed
				new_mode = MODE_DUMMY;
				PWARNF(LOG_V0, LOG_COM, "Modem already installed in %s\n", m_host[modem_port].name());
			}
			if(new_mode == MODE_SPEAK && speak_port != PORT_DISABLED) {
				// only 1 speech device allowed
				new_mode = MODE_DUMMY;
				PWARNF(LOG_V0, LOG_COM, "Speech device already installed in %s\n", m_host[speak_port].name());
			}
			if(new_mode == MODE_MOUSE && m_mouse.port != PORT_DISABLED) {
				// only 1 mouse allowed
				new_mode = MODE_DUMMY;
				PWARNF(LOG_V0, LOG_COM, "Mouse already installed in %s\n", m_host[m_mouse.port].name());
			}
			if(new_mode == m_host[p].io_mode && new_mode != MODE_MODEM && new_mode != MODE_SPEAK) {
				// the mode has not changed, continue except for modem/speech 
				continue;
			}
			// either a different mode or it's modem/speech
			if(new_mode == MODE_MOUSE || new_mode == MODE_MODEM || new_mode == MODE_SPEAK) {
				// mouse and modem/speech modes override everything, so close any open connection
				// and go through initialization again
				PDEBUGF(LOG_V0, LOG_COM, "%s: forcing '%s' mode\n", m_host[p].name(), new_mode_str.c_str());
			} else if(initial_mode_str == new_mode_str) {
				// if strings are the same then this is the initial config.
				// read the device configuration
				dev = g_program.initial_config().get_string(SERIAL_SECTION, dev_name[p]);
			} else {
				// this is a state restore, current host port config will not be changed
				// open network connections will be kept open
				continue;
			}
		}

		close(p);

		if(new_mode != MODE_NONE) {
			PINFOF(LOG_V0, LOG_COM, "%s: initializing mode '%s'", m_host[p].name(), new_mode_str.c_str());
			if(!dev.empty() && new_mode != MODE_MOUSE && new_mode != MODE_DUMMY) {
				PINFOF(LOG_V0, LOG_COM, " on device '%s'", dev.c_str());
			}
			PINFOF(LOG_V0, LOG_COM, "\n");
		}

		try {
			switch(new_mode) {
				case MODE_MOUSE:
					m_host[p].init_mode_mouse();
					m_mouse.port = p;
					m_mouse.type = mouse_type;
					if(mouse_serial) {
						using namespace std::placeholders;
						g_machine.register_mouse_fun(
							std::bind(&Serial::mouse_motion, this, _1, _2, _3),
							std::bind(&Serial::mouse_button, this, _1, _2)
						);
						PINFOF(LOG_V0, LOG_COM, "%s: mouse installed\n", m_host[p].name());
					} else {
						PWARNF(LOG_V0, LOG_COM, "%s: mouse mode is enabled but the mouse type is '%s'\n",
								m_host[p].name(), g_program.config().get_string(GUI_SECTION, GUI_MOUSE_TYPE).c_str());
					}
					g_program.config().set_string(SERIAL_SECTION, mode_name[p], "mouse");
					break;
				case MODE_FILE:
					m_host[p].init_mode_file(dev);
					break;
				case MODE_TERM:
					m_host[p].init_mode_term(dev);
					break;
				case MODE_RAW:
					m_host[p].init_mode_raw(dev);
					break;
				case MODE_NET_CLIENT:
				case MODE_NET_SERVER: {
					m_host[p].init_mode_net(dev, new_mode, tx_delay, tcp_nodelay);
					break;
				}
				case MODE_MODEM:
					m_host[p].init_mode_modem(tx_delay, tcp_nodelay);
					modem_port = p;
					break;
				case MODE_SPEAK:
					m_host[p].init_mode_speech(dev);
					speak_port = p;
					break;
				case MODE_PIPE_CLIENT:
				case MODE_PIPE_SERVER:
					m_host[p].init_mode_pipe(dev, new_mode);
					break;
				case MODE_DUMMY:
					g_program.config().set_string(SERIAL_SECTION, mode_name[p], "dummy");
					break;
				case MODE_NONE:
					g_program.config().set_string(SERIAL_SECTION, mode_name[p], "none");
					break;
				default:
					throw std::runtime_error("unknown mode");
			}
		} catch(std::runtime_error &e) {
			PERRF(LOG_COM, "%s: initialization error: %s\n", m_host[p].name(), e.what());
			m_host[p].io_mode = MODE_NONE;
		}
	}
	if(mouse_serial && m_mouse.port == PORT_DISABLED) {
		PWARNF(LOG_V0, LOG_COM, "Mouse type is set to 'serial' but there are no serial ports available\n");
	}
}

void Serial::close(unsigned _port)
{
	switch(m_host[_port].io_mode) {
		case MODE_MOUSE:
			if((m_mouse.type == MOUSE_TYPE_SERIAL) ||
			   (m_mouse.type == MOUSE_TYPE_SERIAL_WHEEL) ||
			   (m_mouse.type == MOUSE_TYPE_SERIAL_MSYS))
			{
				g_machine.register_mouse_fun(nullptr, nullptr);
			}
			m_mouse.type = MOUSE_TYPE_NONE;
			m_mouse.port = PORT_DISABLED;
			break;
		case MODE_FILE:
			if(m_host[_port].output != nullptr) {
				fclose(m_host[_port].output);
				m_host[_port].output = nullptr;
			}
			break;
		case MODE_TERM:
			#if SERIAL_ENABLE && !SER_WIN32
			if(m_host[_port].tty_id >= 0) {
				tcsetattr(m_host[_port].tty_id, TCSAFLUSH, &m_host[_port].term_orig);
				m_host[_port].tty_id = -1;
			}
			#endif
			break;
		case MODE_RAW:
			#if SER_ENABLE_RAW
			delete m_host[i].raw;
			m_host[i].raw = nullptr;
			#endif
			break;
		case MODE_NET_CLIENT:
		case MODE_NET_SERVER:
			m_host[_port].network.close();
			break;
		case MODE_MODEM:
			m_host[_port].modem.close();
			break;
		case MODE_PIPE_CLIENT:
		case MODE_PIPE_SERVER:
			#if SER_WIN32
			if(m_host[_port].pipe) {
				CloseHandle(m_host[_port].pipe);
				m_host[_port].pipe = INVALID_HANDLE_VALUE;
			}
			#endif
			break;
		default:
			break;
	}
	m_host[_port].io_mode = MODE_NONE;
}

void Serial::save_state(StateBuf &_state)
{
	PINFOF(LOG_V1, LOG_COM, "Saving state\n");

	StateHeader h;
	h.name = name();
	h.data_size = sizeof(m_s);
	_state.write(&m_s,h);
}

void Serial::restore_state(StateBuf &_state)
{
	PINFOF(LOG_V1, LOG_COM, "Restoring state\n");

	for(unsigned p=0; p<SER_PORTS; p++) {
		// the saved state has the correct port binding info
		remove_com(p);
	}

	StateHeader h;
	h.name = name();
	h.data_size = sizeof(m_s);
	_state.read(&m_s,h);

	for(unsigned p=0; p<SER_PORTS; p++) {
		if(m_s.uart[p].com != COM_DISABLED) {
			install_com(p, m_s.uart[p].com);
		}
		if(m_host[p].io_mode == MODE_NET_CLIENT || m_host[p].io_mode == MODE_NET_SERVER) {
			m_host[p].network.rx_fifo().clear();
			m_host[p].network.tx_fifo().clear();
			m_host[p].network.set_tx_threshold(m_host[p].tx_delay_ms, m_s.uart[p].baudrate);
		} else if(m_host[p].io_mode == MODE_MODEM) {
			m_host[p].modem.reset(MACHINE_POWER_ON);
		}
	}

	std::lock_guard<std::mutex> mlock(m_mouse.mtx);
	m_mouse.reset();
}

bool Serial::has_network_modes() const
{
	for(unsigned p=0; p<SER_PORTS; p++) {
		if(is_network_mode(p)) {
			return true;
		}
	}
	return false;
}

bool Serial::is_network_connected() const
{
	for(unsigned p=0; p<SER_PORTS; p++) {
		if(is_network_connected(p)) {
			return true;
		}
	}
	return false;
}

bool Serial::is_network_rx_active() const
{
	for(unsigned p=0; p<SER_PORTS; p++) {
		if(is_network_rx_active(p)) {
			return true;
		}
	}
	return false;
}

bool Serial::is_network_tx_active() const
{
	for(unsigned p=0; p<SER_PORTS; p++) {
		if(is_network_tx_active(p)) {
			return true;
		}
	}
	return false;
}

bool Serial::is_network_mode(uint8_t _port) const
{
	assert(_port < SER_PORTS);
	switch(m_host[_port].io_mode) {
		case MODE_NET_CLIENT: 
		case MODE_NET_SERVER:
		case MODE_MODEM:
			return true;
		default:
			break;
	}
	return false;
}

bool Serial::is_network_connected(uint8_t _port) const
{
	assert(_port < SER_PORTS);
	return (m_host[_port].network.is_connected());
}

bool Serial::is_network_rx_active(uint8_t _port) const
{
	assert(_port < SER_PORTS);
	return m_host[_port].network.is_rx_active();
}

bool Serial::is_network_tx_active(uint8_t _port) const
{
	assert(_port < SER_PORTS);
	return (m_host[_port].network.is_tx_active());
}

void Serial::install_com(uint8_t _port, uint8_t _com)
{
	IODevice::install(&ioports()->at(_com), 1);
	constexpr uint8_t com_irqs[4] = { COM1_IRQ, COM2_IRQ, COM3_IRQ, COM4_IRQ };
	m_s.uart[_port].IRQ = com_irqs[_com];
	m_s.uart[_port].com = _com;
	m_s.portmap[_com] = _port;
	g_machine.register_irq(m_s.uart[_port].IRQ, m_s.uart[_port].name());
	PINFOF(LOG_V0, LOG_COM, "%s -> %s, I/O 0x%04x, IRQ %u\n",
		m_host[_port].name(),
		m_s.uart[_port].name(),
		ioports()->at(_com).from,
		m_s.uart[_port].IRQ
	);
};

void Serial::remove_com(uint8_t _port)
{
	if(m_s.uart[_port].com != COM_DISABLED) {
		PDEBUGF(LOG_V1, LOG_COM, "Removing COM%u ...\n", m_s.uart[_port].com + 1);
		IODevice::remove(&ioports()->at(m_s.uart[_port].com), 1);
		g_machine.unregister_irq(m_s.uart[_port].IRQ, m_s.uart[_port].name());
		m_s.portmap[m_s.uart[_port].com] = PORT_DISABLED;
		m_s.uart[_port].com = COM_DISABLED;
	}
};

void Serial::swap_com(uint8_t _port, uint8_t _com)
{
	PDEBUGF(LOG_V1, LOG_COM, "COM%u is bound to %s, swapping.\n",
			_com+1, m_host[m_s.portmap[_com]].name());

	// the port currently bound to _com ...
	uint8_t old_port = m_s.portmap[_com];

	// ... will receive my COM ...
	m_s.uart[old_port].com = m_s.uart[_port].com;
	m_s.portmap[m_s.uart[_port].com] = old_port;

	// ... and I will reveive its COM
	m_s.uart[_port].com = _com;
	m_s.portmap[_com] = _port;
};

void Serial::bind_port(uint8_t _port_name, uint8_t _com)
{
	unsigned port = PORT_DISABLED;
	for(uint8_t p=0; p<3; p++) {
		if((_port_name >> p) & 1) {
			port = p;
			break;
		}
	}

	if(port == PORT_DISABLED || port >= SER_PORTS) {
		PDEBUGF(LOG_V0, LOG_COM, "Invalid serial port name.\n");
		return;
	}

	if(_com >= 4 && _com != COM_DISABLED) {
		PWARNF(LOG_V0, LOG_COM, "Invalid serial COM.\n");
		return;
	}

	if(m_s.uart[port].com == _com) {
		return;
	}

	if(_com == COM_DISABLED) {
		remove_com(port);
	} else {
		if(m_s.portmap[_com] != PORT_DISABLED) {
			swap_com(port, _com);
		} else {
			if(!m_host[port].present && m_host[port].io_mode == MODE_NONE) {
				if(m_s.uart[port].com != COM_DISABLED) {
					PDEBUGF(LOG_V1, LOG_COM, "%s: no device connected, disabling COM binding.\n", m_host[port].name());
					remove_com(port);
				}
			} else {
				install_com(port, _com);
			}
		}
	}
}

void Serial::set_enabled(bool _enabled)
{
	if(_enabled != m_s.enabled) {
		PINFOF(LOG_V1, LOG_COM, "Serial interface %s\n", _enabled?"ENABLED":"DISABLED");
		m_s.enabled = _enabled;
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
	io_mode = MODE_FILE;
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

		io_mode = MODE_TERM;
		PINFOF(LOG_V0, LOG_COM, "%s: opened tty on device '%s' (id:%d)\n", name(), dev.c_str(), tty_id);
	}

	#else

	UNUSED(dev);
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
	io_mode = MODE_RAW;

	#else

	UNUSED(dev);
	throw std::runtime_error("support for raw serial mode not available");

	#endif
}

void Serial::Port::init_mode_mouse()
{
	io_mode = MODE_MOUSE;
}

void Serial::Port::init_mode_net(std::string dev, unsigned mode, double _tx_delay_ms, bool _tcp_nodelay)
{
	if(dev.empty()) {
		throw std::runtime_error("device address not specified");
	}

	tx_delay_ms = _tx_delay_ms;
	
	network.set_log_name(name());
	network.set_tcp_nodelay(_tcp_nodelay);
	network.set_rx_queue(DEFAULT_RX_FIFO_SIZE, true); // allow overflows on read
	network.set_tx_queue(DEFAULT_TX_FIFO_SIZE);

	auto [host,port] = NetService::parse_address(dev, 2323);
	network.open(
		host.c_str(), port,
		mode == MODE_NET_CLIENT ? NetService::Mode::Client : NetService::Mode::Server,
		0
	);

	io_mode = mode;
}

void Serial::Port::init_mode_pipe(std::string dev, unsigned mode)
{
	#if SER_WIN32

	if(dev.empty()) {
		throw std::runtime_error("pipe device name not specified");
	}
	HANDLE hpipe;
	bool server = (mode == MODE_PIPE_SERVER);
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

	UNUSED(dev);
	UNUSED(mode);
	throw std::runtime_error("support for 'pipe' modes not available");

	#endif
}

void Serial::Port::init_mode_modem(double _tx_delay_ms, bool _tcp_nodelay)
{
	tx_delay_ms = _tx_delay_ms;
	network.set_log_name("MODEM");
	network.set_tcp_nodelay(_tcp_nodelay);
	modem.init(&network, _tx_delay_ms);

	io_mode = MODE_MODEM;
}

void Serial::Port::init_mode_speech(std::string)
{
	speech.init();

	io_mode = MODE_SPEAK;
}

void Serial::set_MSR(uint8_t _port, const ModemStatus &_status)
{
	bool gen_int = false;

	if(_status.RI != m_s.uart[_port].modem_status.ri) {
		m_s.uart[_port].modem_status.ri = _status.RI;
		if(!m_s.uart[_port].modem_cntl.local_loopback) {
			if(_status.RI == false) {
				m_s.uart[_port].modem_status.ri_trailedge = true;
			}
			gen_int = true;
		}
	}
	if(_status.DCD != m_s.uart[_port].modem_status.dcd) {
		m_s.uart[_port].modem_status.dcd = _status.DCD;
		if(!m_s.uart[_port].modem_cntl.local_loopback) {
			m_s.uart[_port].modem_status.delta_dcd = true;
			gen_int = true;
		}
	}
	if(_status.DSR != m_s.uart[_port].modem_status.dsr) {
		m_s.uart[_port].modem_status.dsr = _status.DSR;
		if(!m_s.uart[_port].modem_cntl.local_loopback) {
			m_s.uart[_port].modem_status.delta_dsr = true;
			gen_int = true;
		}
	}
	if(_status.CTS != m_s.uart[_port].modem_status.cts) {
		m_s.uart[_port].modem_status.cts = _status.CTS;
		if(!m_s.uart[_port].modem_cntl.local_loopback) {
			m_s.uart[_port].modem_status.delta_cts = true;
			gen_int = true;
		}
	}
	if(gen_int) {
		raise_interrupt(_port, INT_MODSTAT);
	}
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

void Serial::reset(unsigned _type)
{
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
		PDEBUGF(LOG_V1, LOG_COM, "%s: FIFO disabled\n", port_name(i));

		// Line Control reg: b0000 0000
		m_s.uart[i].line_cntl.wordlen_sel = 3;   // 8 bit
		m_s.uart[i].line_cntl.stopbits = 0;      // 1 stop
		m_s.uart[i].line_cntl.parity_enable = 0; // N parity
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
		PDEBUGF(LOG_V1, LOG_COM, "%s: INTs disabled\n", port_name(i));

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

		m_s.uart[i].scratch = 0;
		m_s.uart[i].divisor_lsb = 0xc; // divisor-lsb register (9600)
		m_s.uart[i].divisor_msb = 0;   // divisor-msb register (9600)

		set_baudrate(i);
		set_databyte_time(i);

		memset(m_s.uart[i].rx_fifo, 0, 16);   // receive FIFO (internal)
		memset(m_s.uart[i].tx_fifo, 0, 16);   // transmit FIFO (internal)

		switch(m_host[i].io_mode) {
			case MODE_NET_CLIENT:
			case MODE_NET_SERVER:
				// do not reset the connection
				m_host[i].network.clear_queues();
				break;
			case MODE_MODEM:
				m_host[i].modem.set_MCR({
					m_s.uart[i].modem_cntl.dtr,
					m_s.uart[i].modem_cntl.rts
				});
				m_host[i].modem.reset(_type);
				break;
			case MODE_SPEAK:
				m_host[i].speech.reset(_type);
				break;
			case MODE_NONE:
			case MODE_RAW:
				break;
			default:
				// simulate device connected
				m_s.uart[i].modem_status.cts = 1;
				m_s.uart[i].modem_status.dsr = 1;
				break;
		}
	}

	std::lock_guard<std::mutex> lock(m_mouse.mtx);
	m_mouse.reset();
	m_s.mouse.detect = 0;
	m_s.mouse.buffer.elements = 0;
	std::memset(m_s.mouse.buffer.data, 0, MOUSE_BUFF_SIZE);
	m_s.mouse.buffer.head = 0;
}

void Serial::power_off()
{
	for(unsigned i=0; i<SER_PORTS; i++) {
		switch(m_host[i].io_mode) {
			case MODE_MODEM:
				m_host[i].modem.power_off();
				break;
			case MODE_SPEAK:
				m_host[i].speech.power_off();
				break;
			default:
				break;
		}
	}
}

void Serial::raise_interrupt(uint8_t port, int type)
{
	bool gen_int = false;

	switch(type) {
		case INT_IER: // IER has changed
			gen_int = true;
			break;
		case INT_RXDATA:
			if(m_s.uart[port].int_enable.rxdata_enable) {
				m_s.uart[port].rx_interrupt = true;
				gen_int = true;
			} else {
				m_s.uart[port].rx_ipending = true;
			}
			break;
		case INT_TXHOLD:
			if(m_s.uart[port].int_enable.txhold_enable) {
				m_s.uart[port].tx_interrupt = true;
				gen_int = true;
			}
			break;
		case INT_RXLSTAT:
			if(m_s.uart[port].int_enable.rxlstat_enable) {
				m_s.uart[port].ls_interrupt = true;
				gen_int = true;
			} else {
				m_s.uart[port].ls_ipending = true;
			}
			break;
		case INT_MODSTAT:
			if((m_s.uart[port].ms_ipending) && (m_s.uart[port].int_enable.modstat_enable)) {
				m_s.uart[port].ms_interrupt = true;
				m_s.uart[port].ms_ipending = false;
				gen_int = true;
			}
			break;
		case INT_FIFO:
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

	if(!m_s.enabled) {
		// POST tests only LCR with port disabled
		// see BIOS at F000:2062, the OUT_PORT_TEST must fail with CF=1
		return 0;
	}

	uint8_t com = 0;
	switch(_address & 0x03f8) {
		case 0x03f8: com = COM1; break;
		case 0x02f8: com = COM2; break;
		case 0x03e8: com = COM3; break;
		case 0x02e8: com = COM4; break;
		default: return 0;
	}
	uint8_t port = m_s.portmap[com];
	if(port == PORT_DISABLED) {
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
						memmove(&m_s.uart[port].rx_fifo[0], &m_s.uart[port].rx_fifo[1], 15);
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
				m_s.uart[port].tx_interrupt = 0;
			} else if(m_s.uart[port].ms_interrupt) {
				m_s.uart[port].int_ident.int_ID = 0x0;
				m_s.uart[port].int_ident.ipending = 0;
			} else {
				m_s.uart[port].int_ident.int_ID = 0x0;
				m_s.uart[port].int_ident.ipending = 1;
			}

			val = m_s.uart[port].int_ident.ipending  |
				(m_s.uart[port].int_ident.int_ID << 1) |
				(m_s.uart[port].fifo_cntl.enable ? 0xc0 : 0x00);
			PDEBUGF(LOG_V2, LOG_COM, "0x%02x IIR int:%x %s\n", val,
					m_s.uart[port].int_ident.int_ID,
					m_s.uart[port].int_ident.ipending?"":"pending");

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
			if(m_host[port].io_mode == MODE_RAW) {
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

	if(!m_s.enabled) {
		return;
	}

	uint8_t com = 0;
	switch(_address & 0x03f8) {
		case 0x03f8: com = COM1; break;
		case 0x02f8: com = COM2; break;
		case 0x03e8: com = COM3; break;
		case 0x02e8: com = COM4; break;
		default: return;
	}
	uint8_t port = m_s.portmap[com];
	if(port == PORT_DISABLED) {
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
		case SER_THR: // Transmit Holding Register, or Divisor Latch LSB if DLAB set
			if(m_s.uart[port].line_cntl.dlab) {
				m_s.uart[port].divisor_lsb = _value;
				PDEBUGF(LOG_V2, LOG_COM, "div LSB\n");
			} else {
				PDEBUGF(LOG_V2, LOG_COM, "TX buff\n");
				if(m_s.uart[port].tx_interrupt) {
					m_s.uart[port].tx_interrupt = 0;
					lower_interrupt(port);
				}
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
							memmove(&m_s.uart[port].tx_fifo[0], &m_s.uart[port].tx_fifo[1], 15);
							m_s.uart[port].line_status.thr_empty = (--m_s.uart[port].tx_fifo_end == 0);
						} else {
							m_s.uart[port].tsrbuffer = m_s.uart[port].thrbuffer;
							m_s.uart[port].line_status.thr_empty = 1;
						}
						m_s.uart[port].line_status.tsr_empty = 0;
						if(m_s.uart[port].line_status.thr_empty) {
							raise_interrupt(port, INT_TXHOLD);
						}
						PDEBUGF(LOG_V2, LOG_COM, "%s: activating TX timer: %u us.\n", port_name(port), m_s.uart[port].databyte_usec);
						g_machine.activate_timer(m_host[port].tx_timer,
								uint64_t(m_s.uart[port].databyte_usec)*1_us,
								false); // not continuous
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
					raise_interrupt(port, INT_IER);
				}
			}
			break;

		case SER_FCR: { // FIFO control register
			PDEBUGF(LOG_V2, LOG_COM, "FCR %s\n",
					bitfield_to_string(_value, { "en", "rx", "tx", "", "", "", "", "" }).c_str());
			bool enabled = new_b0 && !m_s.uart[port].fifo_cntl.enable;
			if(enabled) {
				m_s.uart[port].rx_fifo_end = 0;
				m_s.uart[port].tx_fifo_end = 0;
			} else if(!new_b0 && m_s.uart[port].fifo_cntl.enable) {
				PDEBUGF(LOG_V1, LOG_COM, "%s: FIFO disabled\n", port_name(port));
			}
			m_s.uart[port].fifo_cntl.enable = new_b0;
			if(new_b1) {
				m_s.uart[port].rx_fifo_end = 0;
			}
			if(new_b2) {
				m_s.uart[port].tx_fifo_end = 0;
			}
			m_s.uart[port].fifo_cntl.rxtrigger = (_value & 0xc0) >> 6;
			if(enabled) {
				PDEBUGF(LOG_V1, LOG_COM, "%s: FIFO enabled, rxtrigger=%u\n",
						port_name(port),
						m_s.uart[port].fifo_cntl.rxtrigger);
			}
			break;
		}

		case SER_LCR: { // Line control register
			uint8_t new_wordlen = _value & 0x03;
			PDEBUGF(LOG_V2, LOG_COM, "LCR %s\n", 
					bitfield_to_string(_value, { "wl0", "wl1", "stop", "par", "epar", "spar", "brk", "dlab" }).c_str());

			#if SER_ENABLE_RAW
			if(m_host[port].io_mode == MODE_RAW) {
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

			m_s.uart[port].line_cntl.wordlen_sel = new_wordlen;
			m_s.uart[port].line_cntl.stopbits = new_b2;
			m_s.uart[port].line_cntl.parity_enable = new_b3;
			m_s.uart[port].line_cntl.evenparity_sel = new_b4;
			m_s.uart[port].line_cntl.stick_parity = new_b5;
			m_s.uart[port].line_cntl.break_cntl = new_b6;

			if(m_s.uart[port].modem_cntl.local_loopback && m_s.uart[port].line_cntl.break_cntl) {
				m_s.uart[port].line_status.break_int = 1;
				m_s.uart[port].line_status.framing_error = 1;
				rx_fifo_enq(port, 0x00);
			} else if(m_host[port].io_mode == MODE_MODEM) {
				// m_host[port].modem.set_LCR_break(new_b6);
			}

			if(!new_b7 && m_s.uart[port].line_cntl.dlab) {
				set_baudrate(port);
			}
			m_s.uart[port].line_cntl.dlab = new_b7;

			set_databyte_time(port);
			if(m_s.uart[port].databyte_usec) {
				PDEBUGF(LOG_V2, LOG_COM, "%s: activating RX timer: %u us.\n", port_name(port), m_s.uart[port].databyte_usec);
				g_machine.activate_timer(m_host[port].rx_timer,
						uint64_t(m_s.uart[port].databyte_usec)*1_us,
						false); // not continuous
			}
			break;
		}

		case SER_MCR: // MODEM control register
			PDEBUGF(LOG_V2, LOG_COM, "MCR %s\n",
					bitfield_to_string(_value, { "dtr", "rts", "out1", "out2", "loop", "", "", "" }).c_str());
			if(m_host[port].io_mode == MODE_MOUSE && 
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
			if(m_host[port].io_mode == MODE_RAW) {
				mcr_changed = (m_s.uart[port].modem_cntl.dtr != new_b0) |
						(m_s.uart[port].modem_cntl.rts != new_b1);
			}
			#endif
			m_s.uart[port].modem_cntl.dtr  = new_b0;
			m_s.uart[port].modem_cntl.rts  = new_b1;
			m_s.uart[port].modem_cntl.out1 = new_b2;
			if(new_b3 != m_s.uart[port].modem_cntl.out2) {
				PDEBUGF(LOG_V1, LOG_COM, "%s: INTs %sabled\n", port_name(port), new_b3?"en":"dis");
			}
			m_s.uart[port].modem_cntl.out2 = new_b3;

			if(new_b4 != m_s.uart[port].modem_cntl.local_loopback) {
				m_s.uart[port].modem_cntl.local_loopback = new_b4;
				if(m_s.uart[port].modem_cntl.local_loopback) {
					// transition to loopback mode
					#if SER_ENABLE_RAW
					if(m_host[port].io_mode == MODE_RAW) {
						if(m_s.uart[port].modem_cntl.dtr || m_s.uart[port].modem_cntl.rts) {
							m_host[port].raw->set_modem_control(0);
						}
					}
					#endif
					if(m_s.uart[port].line_cntl.break_cntl) {
						#if SER_ENABLE_RAW
						if(m_host[port].io_mode == MODE_RAW) {
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
					if(m_host[port].io_mode == MODE_RAW) {
						mcr_changed = true;
						if(m_s.uart[port].line_cntl.break_cntl) {
							m_host[port].raw->set_break(0);
						}
					}
					#endif
					// Modem status is modified in loopback mode
					if(m_host[port].io_mode == MODE_MODEM) {
						set_MSR(port, m_host[port].modem.get_MSR());
					}
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
				raise_interrupt(port, INT_MODSTAT);
			} else {
				if(m_host[port].io_mode == MODE_MOUSE) {
					if(m_s.mouse.detect == 2) {
						PDEBUGF(LOG_V1, LOG_COM, "%s: mouse detection mode\n", port_name(port));
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
						PDEBUGF(LOG_V2, LOG_COM, "%s: activating RX timer: %u us.\n", port_name(port), m_s.uart[port].databyte_usec);
						g_machine.activate_timer(m_host[port].rx_timer,
								uint64_t(m_s.uart[port].databyte_usec)*1_us,
								false); // not continuous
						m_s.mouse.detect = 0;
					}
				}
				if(m_host[port].io_mode == MODE_MODEM) {
					m_host[port].modem.set_MCR({
						m_s.uart[port].modem_cntl.dtr,
						m_s.uart[port].modem_cntl.rts
					});
				} else if(m_host[port].io_mode == MODE_RAW) {
					#if SER_ENABLE_RAW
					if(mcr_changed) {
						m_host[port].raw->set_modem_control(value & 0x03);
					}
					#endif
				} else if(m_host[port].io_mode != MODE_NONE) {
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

void Serial::set_baudrate(uint8_t _port)
{
	if((m_s.uart[_port].divisor_lsb | m_s.uart[_port].divisor_msb) != 0) {
		int new_baudrate = (int)(PC_CLOCK_XTL /
				(16 * ((m_s.uart[_port].divisor_msb << 8) | m_s.uart[_port].divisor_lsb)));
		if(new_baudrate != m_s.uart[_port].baudrate) {
			m_s.uart[_port].baudrate = new_baudrate;
			PDEBUGF(LOG_V1, LOG_COM, "%s: baud rate set to %d\n",
					port_name(_port), m_s.uart[_port].baudrate);
			if(
			   ( m_host[_port].io_mode == MODE_NET_CLIENT ||
			     m_host[_port].io_mode == MODE_NET_SERVER
			   ) &&
			   m_host[_port].tx_delay_ms > 0.0
			)
			{
				m_host[_port].network.set_tx_threshold(m_host[_port].tx_delay_ms, new_baudrate);
				PDEBUGF(LOG_V1, LOG_COM, "%s: tx buffer threshold set to %u bytes (%.1f ms)\n",
						port_name(_port), m_host[_port].network.tx_fifo().get_threshold(), m_host[_port].tx_delay_ms);
			}
			#if SER_ENABLE_RAW
			if(m_host[port].io_mode == MODE_RAW) {
				m_host[port].raw->set_baudrate(m_s.uart[port].baudrate);
			}
			#endif
		}
	} else {
		PWARNF(LOG_V1, LOG_COM, "%s: ignoring invalid baud rate divisor\n", m_s.uart[_port].name());
		return;
	}
}

void Serial::set_databyte_time(uint8_t _port)
{
	if(!m_s.uart[_port].baudrate) {
		return;
	}
	double baud_usec = (1'000'000.0 / m_s.uart[_port].baudrate);
	double word_len = 5.0 + m_s.uart[_port].line_cntl.wordlen_sel;
	double stop_bits = m_s.uart[_port].line_cntl.stopbits ? 2.0 : 1.0;
	if(m_s.uart[_port].line_cntl.stopbits && word_len == 5.0) {
		stop_bits = 1.5;
	}
	const char *parity_str = "N";
	if(m_s.uart[_port].line_cntl.parity_enable) {
		uint8_t par = m_s.uart[_port].line_cntl.stick_parity << 1 | m_s.uart[_port].line_cntl.evenparity_sel;
		switch(par) {
			case 0: parity_str = "O"; break;
			case 1: parity_str = "E"; break;
			case 2: parity_str = "M"; break;
			case 3: parity_str = "S"; break;
			default: break;
		}
	}
	double packet_len = 1.0 + word_len + m_s.uart[_port].line_cntl.parity_enable + stop_bits;
	m_s.uart[_port].databyte_usec = uint32_t(packet_len * baud_usec);

	PDEBUGF(LOG_V1, LOG_COM, "%s: databyte is %u us (%u baud, %g-%s-%g)\n",
			port_name(_port), m_s.uart[_port].databyte_usec,
			m_s.uart[_port].baudrate,
			word_len, parity_str, stop_bits);
}

void Serial::rx_fifo_enq(uint8_t port, uint8_t data)
{
	bool gen_int = false;

	if(data <= 31) {
		PDEBUGF(LOG_V3, LOG_COM, "%s: recvd '%s'\n", m_s.uart[port].name(), ASCII_control_characters[data]);
	} else if(data >= 32 && data <= 126) {
		PDEBUGF(LOG_V3, LOG_COM, "%s: recvd '%c'\n", m_s.uart[port].name(), data);
	} else {
		PDEBUGF(LOG_V3, LOG_COM, "%s: recvd 0x%02X\n", m_s.uart[port].name(), data);
	}

	if(m_s.uart[port].fifo_cntl.enable) {
		if(m_s.uart[port].rx_fifo_end == 16) {
			PWARNF(LOG_V2, LOG_COM, "%s: receive FIFO overflow\n", m_s.uart[port].name());
			m_s.uart[port].line_status.overrun_error = true;
			raise_interrupt(port, INT_RXLSTAT);
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
				PDEBUGF(LOG_V2, LOG_COM, "%s: deactivating FIFO timer.\n", m_s.uart[port].name());
				g_machine.deactivate_timer(m_host[port].fifo_timer);
				m_s.uart[port].line_status.rxdata_ready = true;
				raise_interrupt(port, INT_RXDATA);
			} else {
				PDEBUGF(LOG_V2, LOG_COM, "%s: activating FIFO timer: %u us.\n", m_s.uart[port].name(), m_s.uart[port].databyte_usec*3);
				g_machine.activate_timer(m_host[port].fifo_timer,
						uint64_t(m_s.uart[port].databyte_usec * 3)*1_us,
						false); // not continuous
			}
			if(m_host[port].dump.is_open()) {
				m_host[port].dump.write((const char*)(&data), 1);
			}
		}
	} else {
		if(m_s.uart[port].line_status.rxdata_ready) {
			PWARNF(LOG_V2, LOG_COM, "%s: overrun error\n", m_s.uart[port].name());
			m_s.uart[port].line_status.overrun_error = true;
			raise_interrupt(port, INT_RXLSTAT);
		}
		m_s.uart[port].rxbuffer = data;
		m_s.uart[port].line_status.rxdata_ready = true;
		raise_interrupt(port, INT_RXDATA);
		if(m_host[port].dump.is_open()) {
			m_host[port].dump.write((const char*)(&data), 1);
		}
	}
}

void Serial::tx_timer(uint8_t port, uint64_t)
{
	bool sent = true;

	if(m_s.uart[port].modem_cntl.local_loopback) {
		rx_fifo_enq(port, m_s.uart[port].tsrbuffer);
	} else {
		switch (m_host[port].io_mode) {
			case MODE_MODEM:
				sent = m_host[port].modem.serial_write_byte(m_s.uart[port].tsrbuffer);
				break;
			case MODE_SPEAK:
				sent = m_host[port].speech.serial_write_byte(m_s.uart[port].tsrbuffer);
				break;
			case MODE_FILE:
				if(m_host[port].output == nullptr) {
					assert(!m_host[port].filename.empty()); // filename must be set at init!
					m_host[port].output = FileSys::fopen(m_host[port].filename, "wb");
					if(m_host[port].output == nullptr) {
						PERRF(LOG_COM, "%s: could not open file '%s' to write\n",
								port_name(port), m_host[port].filename.c_str());
						m_host[port].io_mode = MODE_DUMMY;
						break;
					} else {
						PINFOF(LOG_V0, LOG_COM, "%s: opened output file '%s'\n",
								port_name(port), m_host[port].filename.c_str());
					}
				}
				if(fputc(m_s.uart[port].tsrbuffer, m_host[port].output) == EOF) {
					PERRF(LOG_COM, "%s: cannot write to file!\n", port_name(port));
					close(port);
					m_host[port].io_mode = MODE_DUMMY;
				} else {
					fflush(m_host[port].output);
				}
				break;
			case MODE_TERM:
				#if SER_POSIX
				if(m_host[port].tty_id >= 0) {
					ssize_t res = ::write(m_host[port].tty_id, (void*) & m_s.uart[port].tsrbuffer, 1);
					if(res == 1) {
						PDEBUGF(LOG_V1, LOG_COM, "%s: term write: '%c'\n", port_name(port), m_s.uart[port].tsrbuffer);
					} else {
						PWARNF(LOG_V1, LOG_COM, "%s: term write failed!\n", port_name(port));
					}
				}
				#endif
				break;
			case MODE_RAW:
				#if SER_ENABLE_RAW
				if(m_host[port].raw->ready_transmit()) {
					m_host[port].raw->transmit(m_s.uart[port].tsrbuffer);
				} else {
					PWARNF(LOG_V0, LOG_COM, "%s: not ready to transmit\n", port_name(port));
				}
				#endif
				break;
			case MODE_MOUSE:
				PDEBUGF(LOG_V1, LOG_COM, "%s: write to mouse ignored: 0x%02x\n", port_name(port), m_s.uart[port].tsrbuffer);
				break;
			case MODE_NET_CLIENT:
			case MODE_NET_SERVER:
				if(m_host[port].network.is_connected()) {
					sent = m_host[port].network.tx_fifo().write(&m_s.uart[port].tsrbuffer, 1);
					if(!sent) {
						PDEBUGF(LOG_V0, LOG_COM, "%s: tx buffer overflow: %02x\n", port_name(port), m_s.uart[port].tsrbuffer);
					}
				}
				break;
			case MODE_PIPE_CLIENT:
			case MODE_PIPE_SERVER:
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

	bool gen_int = false;
	if(sent) {
		if(m_s.uart[port].tsrbuffer <= 31) {
			PDEBUGF(LOG_V3, LOG_COM, "%s: sent '%s'\n", m_s.uart[port].name(), ASCII_control_characters[m_s.uart[port].tsrbuffer]);
		} else if(m_s.uart[port].tsrbuffer >= 32 && m_s.uart[port].tsrbuffer <= 126) {
			PDEBUGF(LOG_V3, LOG_COM, "%s: sent '%c'\n", m_s.uart[port].name(), m_s.uart[port].tsrbuffer);
		} else {
			PDEBUGF(LOG_V3, LOG_COM, "%s: sent 0x%02X\n", m_s.uart[port].name(), m_s.uart[port].tsrbuffer);
		}
		m_s.uart[port].line_status.tsr_empty = true;
		if(m_s.uart[port].fifo_cntl.enable && (m_s.uart[port].tx_fifo_end > 0)) {
			m_s.uart[port].tsrbuffer = m_s.uart[port].tx_fifo[0];
			m_s.uart[port].line_status.tsr_empty = false;
			memmove(&m_s.uart[port].tx_fifo[0], &m_s.uart[port].tx_fifo[1], 15);
			gen_int = (--m_s.uart[port].tx_fifo_end == 0);
		} else if(!m_s.uart[port].line_status.thr_empty) {
			m_s.uart[port].tsrbuffer = m_s.uart[port].thrbuffer;
			m_s.uart[port].line_status.tsr_empty = false;
			gen_int = true;
		}
	}
	if(!m_s.uart[port].line_status.tsr_empty) {
		if(gen_int) {
			m_s.uart[port].line_status.thr_empty = true;
			raise_interrupt(port, INT_TXHOLD);
		}
		// not continuous
		g_machine.activate_timer(m_host[port].tx_timer,
				uint64_t(m_s.uart[port].databyte_usec)*1_us,
				false);
	} else {
		PDEBUGF(LOG_V2, LOG_COM, "%s: deactivating TX timer.\n", port_name(port));
	}
}

void Serial::rx_timer(uint8_t port, uint64_t)
{
	if(!m_s.enabled || m_host[port].io_mode == MODE_NONE || m_host[port].io_mode == MODE_DUMMY) {
		PDEBUGF(LOG_V2, LOG_COM, "%s: rx timer disabled\n", port_name(port));
		return;
	}

	bool data_ready = false;
	uint64_t db_usec = m_s.uart[port].databyte_usec;
	uint8_t chbuf = 0;

	if((!m_s.uart[port].line_status.rxdata_ready) || m_s.uart[port].fifo_cntl.enable) {
		switch (m_host[port].io_mode) {
			case MODE_MODEM:
				data_ready = m_host[port].modem.serial_read_byte(&chbuf);
				break;
			case MODE_SPEAK:
				data_ready = m_host[port].speech.serial_read_byte(&chbuf);
				break;
			case MODE_NET_CLIENT:
			case MODE_NET_SERVER:
				if(m_host[port].network.is_connected() && !m_s.uart[port].line_status.rxdata_ready) {
					data_ready = m_host[port].network.rx_fifo().pop(chbuf);
				}
				break;
			case MODE_RAW:
				#if SER_ENABLE_RAW
				int data;
				if((data_ready = m_host[port].raw->ready_receive())) {
					data = m_host[port].raw->receive();
					if(data < 0) {
						data_ready = 0;
						switch (data) {
							case RAW_EVENT_BREAK:
								m_s.uart[port].line_status.break_int = 1;
								raise_interrupt(port, INT_RXLSTAT);
								break;
							case RAW_EVENT_FRAME:
								m_s.uart[port].line_status.framing_error = 1;
								raise_interrupt(port, INT_RXLSTAT);
								break;
							case RAW_EVENT_OVERRUN:
								m_s.uart[port].line_status.overrun_error = 1;
								raise_interrupt(port, INT_RXLSTAT);
								break;
							case RAW_EVENT_PARITY:
								m_s.uart[port].line_status.parity_error = 1;
								raise_interrupt(port, INT_RXLSTAT);
								break;
							case RAW_EVENT_CTS_ON:
							case RAW_EVENT_CTS_OFF:
							case RAW_EVENT_DSR_ON:
							case RAW_EVENT_DSR_OFF:
							case RAW_EVENT_RING_ON:
							case RAW_EVENT_RING_OFF:
							case RAW_EVENT_RLSD_ON:
							case RAW_EVENT_RLSD_OFF:
								raise_interrupt(port, INT_MODSTAT);
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
			case MODE_TERM:
				#if HAVE_SYS_SELECT_H && SER_POSIX
				if(m_host[port].tty_id >= 0) {
					struct timeval tval = {0,0};
					fd_set fds;
					FD_ZERO(&fds);
					FD_SET(m_host[port].tty_id, &fds);
					if(select(m_host[port].tty_id + 1, &fds, nullptr, nullptr, &tval) == 1) {
						ssize_t res = ::read(m_host[port].tty_id, &chbuf, 1);
						if(res == 1) {
							PDEBUGF(LOG_V1, LOG_COM, "%s: term read: '%c'\n", port_name(port), chbuf);
							data_ready = true;
						} else {
							PWARNF(LOG_V0, LOG_COM, "%s: error reading from term\n", port_name(port));
						}
					}
				}
				#endif
				break;
			case MODE_MOUSE: {
				std::lock_guard<std::mutex> lock(m_mouse.mtx);
				if(m_mouse.update && (m_s.mouse.buffer.elements == 0)) {
					update_mouse_data();
				}
				if(m_s.mouse.buffer.elements > 0) {
					chbuf = m_s.mouse.buffer.data[m_s.mouse.buffer.head];
					m_s.mouse.buffer.head = (m_s.mouse.buffer.head + 1) % MOUSE_BUFF_SIZE;
					m_s.mouse.buffer.elements--;
					PDEBUGF(LOG_V1, LOG_COM, "%s: mouse read: 0x%02x\n", port_name(port), chbuf);
					data_ready = true;
				}
				break;
			}
			case MODE_PIPE_CLIENT:
			case MODE_PIPE_SERVER: {
				#if SER_WIN32
				DWORD avail = 0;
				if(m_host[port].pipe &&
						PeekNamedPipe(m_host[port].pipe, nullptr, 0, nullptr, &avail, nullptr) &&
						avail > 0)
				{
					ReadFile(m_host[port].pipe, &chbuf, 1, &avail, nullptr);
					data_ready = true;
				}
				#endif
				break;
			}
			default:
				break;
		}
		if(data_ready) {
			if(!m_s.uart[port].modem_cntl.local_loopback) {
				rx_fifo_enq(port, chbuf);
			}
		} else {
			if(!m_s.uart[port].fifo_cntl.enable && m_host[port].io_mode != MODE_MOUSE) {
				// why does Bochs use this particular value?
				// idk but it has the effect of making the mouse motion stuttery.
				db_usec = 100000; // Poll frequency is 100ms
			}
		}
	} else {
		// Poll at 4x baud rate to see if the next-char can be read
		// why does Bochs use this particular value?
		// is it an attempt to solve some net related issue?
		db_usec *= 4;
	}

	if(db_usec != m_s.uart[port].databyte_usec) {
		PDEBUGF(LOG_V5, LOG_COM, "%s: next RX timer: %llu us\n", port_name(port), db_usec);
	}
	// not continuous
	g_machine.activate_timer(m_host[port].rx_timer, uint64_t(db_usec)*1_us, false);
}

void Serial::fifo_timer(uint8_t port, uint64_t)
{
	m_s.uart[port].line_status.rxdata_ready = 1;
	raise_interrupt(port, INT_FIFO);
}

void Serial::mouse_button(MouseButton _button, bool _state)
{
	// This function is called by the GUI thread

	if(m_mouse.port == PORT_DISABLED) {
		// This condition should not happen. Evts are fired only if mouse is enabled at init.
		assert(false);
		PERRF(LOG_COM, "Mouse not connected to a serial port\n");
		return;
	}

	// if the DTR and RTS lines aren't up, the mouse doesn't have any power to send packets.
	if(!m_s.uart[m_mouse.port].modem_cntl.dtr || !m_s.uart[m_mouse.port].modem_cntl.rts) {
		PDEBUGF(LOG_V2, LOG_COM, "%s: mouse button: ignored (dtr/rts not up)\n",
			port_name(m_mouse.port));
		return;
	}

	std::lock_guard<std::mutex> lock(m_mouse.mtx);

	int btnid = ec_to_i(_button) - 1;
	m_mouse.buttons &= ~(1 << btnid);
	m_mouse.buttons |= (_state << btnid);
	m_mouse.update = true;

	PDEBUGF(LOG_V2, LOG_COM, "%s: mouse button: id=%u, state=%u\n", port_name(m_mouse.port),
		ec_to_i(_button), _state);
}

void Serial::mouse_motion(int delta_x, int delta_y, int delta_z)
{
	// This function is called by the GUI thread

	if(m_mouse.port == PORT_DISABLED) {
		// This condition should not happen. Evts are fired only if mouse is enabled at init.
		assert(false);
		PERRF(LOG_COM, "Mouse not connected to a serial port\n");
		return;
	}

	if((delta_x==0) && (delta_y==0) && (delta_z==0))
	{
		PDEBUGF(LOG_V2, LOG_COM, "%s: mouse motion: useless call. ignoring.\n",
			port_name(m_mouse.port));
		return;
	}

	// if the DTR and RTS lines aren't up, the mouse doesn't have any power to send packets.
	if(!m_s.uart[m_mouse.port].modem_cntl.dtr || !m_s.uart[m_mouse.port].modem_cntl.rts) {
		PDEBUGF(LOG_V2, LOG_COM, "%s: mouse motion: ignored (dtr/rts not up)\n",
			port_name(m_mouse.port));
		return;
	}

	PDEBUGF(LOG_V2, LOG_COM, "%s: mouse motion: d:[%d,%d,%d]->", port_name(m_mouse.port),
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

	PDEBUGF(LOG_V2, LOG_COM, "%s: mouse d:[%d,%d", port_name(m_mouse.port), delta_x, delta_y);

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

const char * Serial::port_name(unsigned _port) const
{
	assert(_port < SER_PORTS);
	static thread_local std::string str_name;
	if(m_s.uart[_port].com != COM_DISABLED) {
		str_name = str_format("%s (%s)", m_host[_port].name(), m_s.uart[_port].name());
	} else {
		str_name = m_host[_port].name();
	}
	return str_name.c_str();
}
