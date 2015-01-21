/*
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

#ifndef IBMULATOR_HW_PORTS_H
#define IBMULATOR_HW_PORTS_H

#include "statebuf.h"
#include <set>

class IODevice;
class Devices;
extern Devices g_devices;

#define PORTSCNT 0x10000
#define PORTMAX  0xFFFF

class Devices
{
private:

	struct io_handler_t {
		IODevice * device;
		uint mask;

		io_handler_t() : device(NULL), mask(0) {}
	};

	io_handler_t m_read_handlers[PORTSCNT];
	io_handler_t m_write_handlers[PORTSCNT];

	std::map<std::string, IODevice *> m_devices;

public:
	Devices();
	~Devices();

	//called by the machine:
	void register_device(IODevice *_iodev);

	//called by devices:
	void register_read_handler(IODevice *_iodev, uint16_t _port, uint _mask);
	void register_write_handler(IODevice *_iodev, uint16_t _port, uint _mask);
	void unregister_read_handler(uint16_t _port);
	void unregister_write_handler(uint16_t _port);

	void init();
	void reset(uint _signal);
	void power_off();
	void config_changed();

	uint8_t read_byte(uint16_t _port);
	uint16_t read_word(uint16_t _port);
	void write_byte(uint16_t _port, uint8_t _value);
	void write_word(uint16_t _port, uint16_t _value);

	void save_state(StateBuf &_state);
	void restore_state(StateBuf &_state);
};

#endif
