/*
 * Copyright (c) 2001-2009  The Bochs Project
 * Copyright (C) 2015-2022  Marco Bortolin
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

#ifndef IBMULATOR_HW_PARPORT_H
#define IBMULATOR_HW_PARPORT_H

#include "hardware/iodevice.h"
#include "hardware/printer/mps_printer.h"

enum ParportModes {
	PARPORT_EXTENDED = 0,
	PARPORT_COMPATIBLE = 1
};

typedef struct {
	uint8_t data;
	struct {
		bool error; // inverted; 0=printer encountered an error 
		bool slct;  // select; 1=printer selected
		bool pe;    // paper end; 1=end of the paper
		bool ack;   // inverted; 0=char received and ready to receive another
		bool busy;  // inverted; 0=printer busy, cannot receive data
	} STATUS;
	struct {
		bool strobe;   // 1=data is clocked into the printer
		bool autofeed; // 1=auto line feed
		bool init;     // inverted; 0=printer starts
		bool slct_in;  // 1=printer is selected
		bool irq;      // 1=an interrupt occurs when the -ACK signal changes to inactive.
		bool input;    // direction
	} CONTROL;
	FILE *output;
	bool initmode;
	uint8_t mode;
	uint8_t port;
} parport_t;


class Parallel : public IODevice
{
	IODEVICE(Parallel, "Parallel");

private:
	parport_t m_s;
	static uint16_t ms_irqs[3];
	bool m_enabled = false;
	void virtual_printer();
	std::shared_ptr<MpsPrinter> m_printer;

public:
	Parallel(Devices *_dev);
	~Parallel();

	void install();
	void remove();
	void reset(unsigned type);
	void config_changed();
	uint16_t read(uint16_t address, unsigned io_len);
	void write(uint16_t address, uint16_t value, unsigned io_len);

	void set_mode(uint8_t _mode);
	void set_port(uint8_t _port);
	void set_enabled(bool _enabled);
	
	void connect_printer(std::shared_ptr<MpsPrinter> _prn) {
		m_printer = _prn;
	}

	static std::map<std::string, uint> ms_lpt_ports;

	void save_state(StateBuf &_state);
	void restore_state(StateBuf &_state);
};

#endif
