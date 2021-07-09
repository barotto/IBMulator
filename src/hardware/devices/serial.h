/*
 * Copyright (c) 2001-2014  The Bochs Project
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

#ifndef IBMULATOR_HW_SERIAL_H
#define IBMULATOR_HW_SERIAL_H

#include "shared_fifo.h"
#include "ring_buffer.h"

// Peter Grehan (grehan@iprg.nokia.com) coded most of this
// serial emulation.

#if !defined(_WIN32) && (defined(__NetBSD__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__linux__) || defined(__GNU__) || defined(__GLIBC__) || defined(__APPLE__) || defined(__sun__) || defined(__CYGWIN__))
	#define SER_POSIX 1
#else
	#define SER_POSIX 0
#endif

#if defined(_WIN32)
	#define SER_WIN32 1
#else
	#define SER_WIN32 0
#endif

#if SER_WIN32 || SER_POSIX
	#define SERIAL_ENABLE 1
#else
	#define SERIAL_ENABLE 0
#endif

#if SER_POSIX
	extern "C" {
	#include <termios.h>
	};
	typedef int SOCKET;
#endif

#if SER_WIN32
	#include "wincompat.h"
	#include <winsock2.h>
#endif

#define SER_PORTS       1     // number of serial ports (1 to 4)
#define SER_ENABLE_RAW  false // enable raw serial port access (TODO NOT IMPLEMENTED)
#define SER_TERM_BRKINT false // if true when in term mode CTRL-C will cause SIGINT

#if (SER_PORTS > 4)
#error "SER_PORTS cannot be bigger than 4"
#endif

#define PC_CLOCK_XTL   1843200.0

#define SER_RXIDLE  0
#define SER_RXPOLL  1
#define SER_RXWAIT  2

#define SER_THR  0   // 3f8 Transmit Holding Register
#define SER_RBR  0   // 3f8 Receiver Buffer Register
#define SER_IER  1   // 3f9 Interrupt Enable Register
#define SER_IIR  2   // 3fa Interrupt ID Register
#define SER_FCR  2   // 3fa FIFO Control Register
#define SER_LCR  3   // 3fb Line Control Register
#define SER_MCR  4   // 3fc MODEM Control Register
#define SER_LSR  5   // 3fd Line Status Register
#define SER_MSR  6   // 3fe MODEM Status Register
#define SER_SCR  7   // 3ff Scratch Register

enum SerialPortMode {
	SER_MODE_INVALID,
	SER_MODE_NONE,         // no i/o, no device connected
	SER_MODE_DUMMY,        // no i/o, dummy device connected (CTS and DSR asserted)
	SER_MODE_FILE,         // file output
	SER_MODE_TERM,         // tty input/output (Linux only)
	SER_MODE_RAW,          // raw hardware serial port access (TODO)
	SER_MODE_MOUSE,        // serial mouse connected
	SER_MODE_NET_CLIENT,   // network client input/output
	SER_MODE_NET_SERVER,   // network server input/output
	SER_MODE_PIPE_CLIENT,  // pipe client input/output (Windows only)
	SER_MODE_PIPE_SERVER   // pipe server input/output (Windows only)
};

enum SerialIntType {
	SER_INT_IER,
	SER_INT_RXDATA,
	SER_INT_TXHOLD,
	SER_INT_RXLSTAT,
	SER_INT_MODSTAT,
	SER_INT_FIFO
};

#if USE_RAW_SERIAL
class serial_raw;
#endif

// TODO these are arbitrary values
#define SER_RX_BUF_LEN  1024
#define SER_TX_BUF_THRE 16
#define SER_TX_BUF_LEN (SER_TX_BUF_THRE * 2)


class Serial : public IODevice
{
	IODEVICE(Serial, "Serial")

private:
	enum ComName {
		SER_COM1 = 0,
		SER_COM2 = 1,
		SER_COM3 = 2,
		SER_COM4 = 3,
		SER_COM_DISABLED = 0xFF
	};

	enum PortName {
		SER_PORT_A = 0,
		SER_PORT_B = 1,
		SER_PORT_C = 2,
		SER_PORT_D = 3,
		SER_PORT_DISABLED = 0xFF
	};

	struct UART16550A {
		//
		// UART internal state
		//
		bool  ls_interrupt;
		bool  ms_interrupt;
		bool  rx_interrupt;
		bool  tx_interrupt;
		bool  fifo_interrupt;
		bool  ls_ipending;
		bool  ms_ipending;
		bool  rx_ipending;
		bool  fifo_ipending;

		uint8_t  IRQ;
		uint8_t  rx_fifo_end;
		uint8_t  tx_fifo_end;
		int      baudrate;
		uint32_t databyte_usec;

		//
		// Register definitions
		//
		uint8_t     rxbuffer;        // receiver buffer register (r/o)
		uint8_t     thrbuffer;       // transmit holding register (w/o)
		// Interrupt Enable Register
		struct {
			bool    rxdata_enable;   // 1=enable receive data interrupts
			bool    txhold_enable;   // 1=enable tx. holding reg. empty ints
			bool    rxlstat_enable;  // 1=enable rx line status interrupts
			bool    modstat_enable;  // 1=enable modem status interrupts
		} int_enable;
		// Interrupt Identification Register (r/o)
		struct {
			bool    ipending;        // 0=interrupt pending
			uint8_t int_ID;          // 3-bit interrupt ID
		} int_ident;
		// FIFO Control Register (w/o)
		struct {
			bool    enable;          // 1=enable tx and rx FIFOs
			uint8_t rxtrigger;       // 2-bit code for rx fifo trigger level
		} fifo_cntl;
		// Line Control Register (r/w)
		struct {
			uint8_t wordlen_sel;     // 2-bit code for char length
			bool    stopbits;        // select stop bit len
			bool    parity_enable;   // ...
			bool    evenparity_sel;  // ...
			bool    stick_parity;    // ...
			bool    break_cntl;      // 1=send break signal
			bool    dlab;            // divisor latch access bit
		} line_cntl;
		// MODEM Control Register (r/w)
		struct {
			bool    dtr;             // DTR (Data Terminal Ready)
			bool    rts;             // RTS (Request-to-send)
			bool    out1;            // OUTPUT1 value
			bool    out2;            // OUTPUT2 value
			bool    local_loopback;  // 1=loopback mode
		} modem_cntl;
		// Line Status Register (r/w)
		struct {
			bool    rxdata_ready;    // 1=receiver data ready
			bool    overrun_error;   // 1=receive overrun detected
			bool    parity_error;    // 1=rx char has a bad parity bit
			bool    framing_error;   // 1=no stop bit detected for rx char
			bool    break_int;       // 1=break signal detected
			bool    thr_empty;       // 1=tx hold register (or fifo) is empty
			bool    tsr_empty;       // 1=shift reg and hold reg empty
			bool    fifo_error;      // 1=at least 1 err condition in fifo
		} line_status;
		// Modem Status Register (r/w)
		struct {
			bool    delta_cts;       // 1=CTS changed since last read
			bool    delta_dsr;       // 1=DSR changed since last read
			bool    ri_trailedge;    // 1=RI moved from low->high
			bool    delta_dcd;       // 1=DCD changed since last read
			bool    cts;             // CTS (Clear To Send)
			bool    dsr;             // DSR (Data Set Ready)
			bool    ri;              // RI (Ring Indicator)
			bool    dcd;             // DCD (Data Carrier Detect)
		} modem_status;

		uint8_t  scratch;       // Scratch Register (r/w)
		uint8_t  tsrbuffer;     // transmit shift register (internal)
		uint8_t  rx_fifo[16];   // receive FIFO (internal)
		uint8_t  tx_fifo[16];   // transmit FIFO (internal)
		uint8_t  divisor_lsb;   // Divisor latch, least-sig. byte
		uint8_t  divisor_msb;   // Divisor latch, most-sig. byte

		uint8_t  com; // the POS selectable COM port index

		constexpr const char * name() const {
			switch(com) {
				case SER_COM1: return "COM1";
				case SER_COM2: return "COM2";
				case SER_COM3: return "COM3";
				case SER_COM4: return "COM4";
				default: return "COM?";
			}
		}
	};

	// The PS/1 2011 has 1 serial port that can only be set to COM1 or DISABLED
	// The PS/1 2121 has 1 serial port that can be set to COM1, COM2, or DISABLED.
	// The PS/1 2133 has 2 serial ports (A and B):
	//   A can be set to COM1, COM3, DISABLED
	//   B can be set to COM2, COM4, DISABLED

	struct {
		bool enabled; // TODO this should probably go inside uart or removed; reevaluate for 2133
		UART16550A uart[SER_PORTS];
		uint8_t portmap[4];
		struct {
			// mouse state (on the attached mouse device itself)
			int detect = 0; // detection protocol state
			struct {
				uint8_t data[MOUSE_BUFF_SIZE];
				int head;
				int elements;
			} buffer;
		} mouse;
	} m_s;

	struct Port {
		int port_id;

		int rx_timer;
		int tx_timer;
		int fifo_timer;

		int io_mode;

		// socket server/client mode

		class TXFifo : public RingBuffer {
		protected:
			std::condition_variable m_data_cond;
		public:
			virtual size_t read(uint8_t *_data, size_t _len, unsigned _max_wait_ns);
			virtual size_t write(uint8_t *_data, size_t _len);
		};
		#if SER_POSIX
		std::string server_host;
		unsigned long server_port;
		std::string client_name;
		std::atomic<SOCKET> server_socket_id;
		std::atomic<SOCKET> client_socket_id;
		std::thread net_thread;
		SharedFifo<uint8_t, SER_RX_BUF_LEN> rx_data;
		TXFifo tx_data;
		#endif
		void start_net_server();
		void start_net_client();
		void net_data_loop();
		void net_tx_loop();
		void close_client_socket();

		// file mode
		std::string filename;
		FILE *output;

		// pipe mode (Win only)
		#if SER_WIN32
		HANDLE pipe;
		#endif

		// term mode (POSIX only)
		int tty_id; // TODO move inside the prepro if?
		#if SER_POSIX
		struct termios term_orig, term_new;
		#endif

		#if SERIAL_RAW
		serial_raw* raw;
		#endif

		void init_mode_file(std::string dev);
		void init_mode_term(std::string dev);
		void init_mode_raw(std::string dev);
		void init_mode_mouse();
		void init_mode_net(std::string dev, unsigned mode);
		void init_mode_pipe(std::string dev, unsigned mode);

		constexpr const char * name() const {
			switch(port_id) {
				case SER_PORT_A: return "Serial port A";
				case SER_PORT_B: return "Serial port B";
				case SER_PORT_C: return "Serial port C";
				case SER_PORT_D: return "Serial port D";
				default: return "Serial port ?";
			}
		}
	} m_host[SER_PORTS];

	struct {
		int port = SER_PORT_DISABLED;
		int type = 0;
		int delayed_dx = 0;
		int delayed_dy = 0;
		int delayed_dz = 0;
		uint8_t buttons = 0;
		bool update = false;
		std::mutex mtx;

		void reset() {
			delayed_dx = 0;
			delayed_dy = 0;
			delayed_dz = 0;
			buttons = 0;
			update = false;
		}
	} m_mouse;

public:
	Serial(Devices *_dev);
	~Serial();

	void install();
	void remove();
	void reset(unsigned type);
	void config_changed();
	uint16_t read(uint16_t address, unsigned io_len);
	void write(uint16_t address, uint16_t value, unsigned io_len);

	void set_port(uint8_t _port, uint8_t _com);
	void set_enabled(bool _enabled);

	void save_state(StateBuf &_state);
	void restore_state(StateBuf &_state);

private:
	void lower_interrupt(uint8_t port);
	void raise_interrupt(uint8_t port, int type);

	void rx_fifo_enq(uint8_t port, uint8_t data);

	void tx_timer(uint8_t, uint64_t);
	void rx_timer(uint8_t, uint64_t);
	void fifo_timer(uint8_t, uint64_t);

	void mouse_button(MouseButton _button, bool _state);
	void mouse_motion(int delta_x, int delta_y, int delta_z);
	void update_mouse_data(void);

	void close(unsigned _port);
};

#endif
