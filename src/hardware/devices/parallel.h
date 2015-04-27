/*
 * 	Copyright (c) 2001-2009  The Bochs Project
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

#ifndef IBMULATOR_HW_PARPORT_H
#define IBMULATOR_HW_PARPORT_H

#include "hardware/iodevice.h"

enum ParportModes {
	PARPORT_EXTENDED = 0,
	PARPORT_COMPATIBLE = 1
};

typedef struct {
	uint8_t data;
	struct {
		bool error;
		bool slct;
		bool pe;
		bool ack;
		bool busy;
	} STATUS;
	struct {
		bool strobe;
		bool autofeed;
		bool init;
		bool slct_in;
		bool irq;
		bool input;
	} CONTROL;
	FILE *output;
	bool initmode;
	uint8_t mode;
	uint8_t port;
} parport_t;

class Parallel;
extern Parallel g_parallel;

class Parallel : public IODevice
{
private:
	parport_t m_s;
	static uint16_t ms_ports[3];
	static uint16_t ms_irqs[3];
	bool m_enabled;
	void virtual_printer();

public:

	Parallel();
	~Parallel();

	void init();
	void reset(unsigned type);
	void config_changed();
	uint16_t read(uint16_t address, unsigned io_len);
	void   write(uint16_t address, uint16_t value, unsigned io_len);
	const char *get_name() { return "Parallel"; }

	void set_mode(uint8_t _mode);
	void set_port(uint8_t _port);
	void set_enabled(bool _enabled);

	static std::map<std::string, uint> ms_lpt_ports;

	void save_state(StateBuf &_state);
	void restore_state(StateBuf &_state);
};

#endif
