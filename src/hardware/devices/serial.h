/*
 * Copyright (c) 2001-2014  The Bochs Project
 * Copyright (C) 2015, 2016  Marco Bortolin
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

#include "keyboard.h"

#define SERIAL_MAXDEV   2

#define PC_CLOCK_XTL   1843200.0

#define SER_RXIDLE  0
#define SER_RXPOLL  1
#define SER_RXWAIT  2

#define SER_THR  0 //3f8
#define SER_RBR  0 //3f8
#define SER_IER  1 //3f9
#define SER_IIR  2 //3fa
#define SER_FCR  2 //3fa
#define SER_LCR  3 //3fb
#define SER_MCR  4 //3fc
#define SER_LSR  5 //3fd
#define SER_MSR  6 //3fe
#define SER_SCR  7 //3ff

#define SER_MODE_NULL          0
#define SER_MODE_FILE          1
#define SER_MODE_TERM          2
#define SER_MODE_RAW           3
#define SER_MODE_MOUSE         4
#define SER_MODE_SOCKET_CLIENT 5
#define SER_MODE_SOCKET_SERVER 6
#define SER_MODE_PIPE_CLIENT   7
#define SER_MODE_PIPE_SERVER   8

enum {
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

typedef struct {
	bool enabled;
	/*
	* UART internal state
	*/
	bool  ls_interrupt;
	bool  ms_interrupt;
	bool  rx_interrupt;
	bool  tx_interrupt;
	bool  fifo_interrupt;
	bool  ls_ipending;
	bool  ms_ipending;
	bool  rx_ipending;
	bool  fifo_ipending;

	uint8_t IRQ;

	uint8_t rx_fifo_end;
	uint8_t tx_fifo_end;

	int  baudrate;
	uint32_t databyte_usec;

	int  rx_timer_index;
	int  tx_timer_index;
	int  fifo_timer_index;

	int io_mode;
	int tty_id;
	SOCKET socket_id;
	FILE *output;

	#if SER_WIN32
	HANDLE pipe;
	#endif

	#if SERIAL_RAW
	serial_raw* raw;
	#endif

	#if SER_POSIX
	struct termios term_orig, term_new;
	#endif

	/*
	* Register definitions
	*/
	uint8_t     rxbuffer;     /* receiver buffer register (r/o) */
	uint8_t     thrbuffer;    /* transmit holding register (w/o) */
	/* Interrupt Enable Register */
	struct {
		bool    rxdata_enable;      /* 1=enable receive data interrupts */
		bool    txhold_enable;      /* 1=enable tx. holding reg. empty ints */
		bool    rxlstat_enable;     /* 1=enable rx line status interrupts */
		bool    modstat_enable;     /* 1=enable modem status interrupts */
	} int_enable;
	/* Interrupt Identification Register (r/o) */
	struct {
		bool    ipending;           /* 0=interrupt pending */
		uint8_t int_ID;             /* 3-bit interrupt ID */
	} int_ident;
	/* FIFO Control Register (w/o) */
	struct {
		bool    enable;             /* 1=enable tx and rx FIFOs */
		uint8_t rxtrigger;          /* 2-bit code for rx fifo trigger level */
	} fifo_cntl;
	/* Line Control Register (r/w) */
	struct {
		uint8_t wordlen_sel;        /* 2-bit code for char length */
		bool    stopbits;           /* select stop bit len */
		bool    parity_enable;      /* ... */
		bool    evenparity_sel;     /* ... */
		bool    stick_parity;       /* ... */
		bool    break_cntl;         /* 1=send break signal */
		bool    dlab;               /* divisor latch access bit */
	} line_cntl;
	/* MODEM Control Register (r/w) */
	struct {
		bool    dtr;                /* DTR output value */
		bool    rts;                /* RTS output value */
		bool    out1;               /* OUTPUT1 value */
		bool    out2;               /* OUTPUT2 value */
		bool    local_loopback;     /* 1=loopback mode */
	} modem_cntl;
	/* Line Status Register (r/w) */
	struct {
		bool    rxdata_ready;       /* 1=receiver data ready */
		bool    overrun_error;      /* 1=receive overrun detected */
		bool    parity_error;       /* 1=rx char has a bad parity bit */
		bool    framing_error;      /* 1=no stop bit detected for rx char */
		bool    break_int;          /* 1=break signal detected */
		bool    thr_empty;          /* 1=tx hold register (or fifo) is empty */
		bool    tsr_empty;          /* 1=shift reg and hold reg empty */
		bool    fifo_error;         /* 1=at least 1 err condition in fifo */
	} line_status;
	/* Modem Status Register (r/w) */
	struct {
		bool    delta_cts;          /* 1=CTS changed since last read */
		bool    delta_dsr;          /* 1=DSR changed since last read */
		bool    ri_trailedge;       /* 1=RI moved from low->high */
		bool    delta_dcd;          /* 1=CD changed since last read */
		bool    cts;                /* CTS input value */
		bool    dsr;                /* DSR input value */
		bool    ri;                 /* RI input value */
		bool    dcd;                /* DCD input value */
	} modem_status;

	uint8_t  scratch;       /* Scratch Register (r/w) */
	uint8_t  tsrbuffer;     /* transmit shift register (internal) */
	uint8_t  rx_fifo[16];   /* receive FIFO (internal) */
	uint8_t  tx_fifo[16];   /* transmit FIFO (internal) */
	uint8_t  divisor_lsb;   /* Divisor latch, least-sig. byte */
	uint8_t  divisor_msb;   /* Divisor latch, most-sig. byte */

	uint8_t port; // the POS selectable port index

} serial_t;


class Serial;
extern Serial g_serial;


class Serial : public IODevice
{
private:
	serial_t m_s[SERIAL_MAXDEV];
	bool m_enabled;
	uint8_t m_post_reg; //used as scratch memory to pass POST

	static uint16_t ms_ports[SERIAL_MAXDEV];
	static uint8_t ms_irqs[SERIAL_MAXDEV];

	int   detect_mouse;
	int   mouse_port;
	int   mouse_type;
	int   mouse_delayed_dx;
	int   mouse_delayed_dy;
	int   mouse_delayed_dz;
	uint8_t mouse_buttons;
	bool mouse_update;
	struct {
		int     num_elements;
		uint8_t buffer[MOUSE_BUFF_SIZE];
		int     head;
	} mouse_internal_buffer;

	void lower_interrupt(uint8_t port);
	void raise_interrupt(uint8_t port, int type);

	void rx_fifo_enq(uint8_t port, uint8_t data);

	void tx_timer(uint8_t);
	void rx_timer(uint8_t);
	void fifo_timer(uint8_t);

	void mouse_motion(int delta_x, int delta_y, int delta_z, unsigned button_state);
	void update_mouse_data(void);

	void init_mode_file(uint port, std::string dev);
	void init_mode_term(uint port, std::string dev);
	void init_mode_raw(uint port, std::string dev);
	void init_mode_mouse(uint port);
	void init_mode_socket(uint port, std::string dev, uint mode);
	void init_mode_pipe(uint port, std::string dev, uint mode);

	uint16_t read_disabled(uint16_t address, unsigned io_len);
	void write_disabled(uint16_t address, uint16_t value, unsigned io_len);

public:

	Serial();
	~Serial();

	void init(void);
	void reset(unsigned type);
	void config_changed(); //TODO
	uint16_t read(uint16_t address, unsigned io_len);
	void write(uint16_t address, uint16_t value, unsigned io_len);
	const char *get_name() { return "Serial"; }

	void set_port(uint8_t _port);
	void set_enabled(bool _enabled);

	void save_state(StateBuf &_state);
	void restore_state(StateBuf &_state);
};

#endif
