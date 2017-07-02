/*
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

#ifndef IBMULATOR_HW_DEVICES_H
#define IBMULATOR_HW_DEVICES_H

#include "statebuf.h"
#include <set>

class IODevice;
class Machine;
class Devices;
class SystemBoard;
class DMA;
class PIC;
class PIT;
class VGA;
class CMOS;
extern Devices g_devices;

#define PORT_8BIT  0x01
#define PORT_16BIT 0x02
#define PORT_32BIT 0x04
#define PORT_READ  0x08
#define PORT_R_    PORT_READ
#define PORT_WRITE 0x10
#define PORT__W    PORT_WRITE
#define PORT_RW    PORT_READ | PORT_WRITE

#define PORT_MAX   0xFFFF

class Devices
{
private:
	struct io_handler_t {
		IODevice * device;
		uint mask;

		io_handler_t() : device(nullptr), mask(0) {}
	};

	io_handler_t m_read_handlers[PORT_MAX+1];
	io_handler_t m_write_handlers[PORT_MAX+1];

	std::map<std::string, IODevice *> m_devices;

	Machine * m_machine;
	SystemBoard * m_sysboard;
	DMA * m_dma;
	PIC * m_pic;
	PIT * m_pit;
	VGA * m_vga;
	CMOS * m_cmos;

	unsigned m_last_io_time;

public:
	Devices();
	~Devices();

	void init(Machine*);
	void reset(uint _signal);
	void power_off();
	void config_changed();

	template<class T>
	inline T* device() {
		for(auto dev : m_devices) {
			T* devptr = dynamic_cast<T*>(dev.second);
			if(devptr != nullptr) {
				return devptr;
			}
		}
		return nullptr;
	}
	inline SystemBoard* sysboard() { return m_sysboard; }
	inline DMA* dma() { return m_dma; }
	inline PIC* pic() { return m_pic; }
	inline PIT* pit() { return m_pit; }
	inline VGA* vga() { return m_vga; }
	inline CMOS* cmos() { return m_cmos; }

	void register_read_handler(IODevice *_iodev, uint16_t _port, uint _mask);
	void register_write_handler(IODevice *_iodev, uint16_t _port, uint _mask);
	void unregister_read_handler(uint16_t _port);
	void unregister_write_handler(uint16_t _port);

	uint8_t read_byte(uint16_t _port);
	uint16_t read_word(uint16_t _port);
	uint32_t read_dword(uint16_t _port);
	void write_byte(uint16_t _port, uint8_t _value);
	void write_word(uint16_t _port, uint16_t _value);
	void write_dword(uint16_t _port, uint32_t _value);

	inline void set_io_time(unsigned _io_time) {
		m_last_io_time = _io_time;
	}
	inline unsigned get_last_io_time() const {
		return m_last_io_time;
	}
	inline void reset_io_time() {
		m_last_io_time = 0;
	}

	void save_state(StateBuf &_state);
	void restore_state(StateBuf &_state);

private:
	template<class T> T* install();
	template<class T> T* install_only_if(bool _condition);
	void remove(const char *);
};

#endif
