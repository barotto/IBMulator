/*
 * Copyright (C) 2015-2020  Marco Bortolin
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

#ifndef IBMULATOR_HW_DEVICE_H
#define IBMULATOR_HW_DEVICE_H

#include "statebuf.h"
#include <vector>
class Devices;

#define IODEVICE(_CLASSNAME_, _DEVNAME_) \
	public: \
		static constexpr const char* NAME = _DEVNAME_; \
		virtual const char *name() { return _CLASSNAME_::NAME; } \
	protected: \
		static IODevice::IOPorts ms_ioports; \
		virtual const IOPorts * ioports() { return &_CLASSNAME_::ms_ioports; }

#define IODEVICE_PORTS(_CLASSNAME_) \
		IODevice::IOPorts _CLASSNAME_::ms_ioports

class IODevice
{
public:
	struct IOPortsInterval {
		uint16_t from, to;
		uint8_t  mask;
	};
	typedef std::vector<IOPortsInterval> IOPorts;
	
protected:
	Devices *m_devices;

	IODEVICE(IODevice, "null device")

public:
	IODevice(Devices *_dev) : m_devices(_dev) {}
	virtual ~IODevice() {}

	IODevice() = delete;
	IODevice(IODevice const&) = delete;
	IODevice& operator=(IODevice const&) = delete;

	virtual void install();
	virtual void remove();
	virtual void reset(uint /*_signal*/) {}
	virtual void power_off() {}
	virtual void config_changed() {}
	virtual uint16_t read(uint16_t /*_address*/, unsigned /*_io_len*/) { return ~0; }
	virtual void write(uint16_t /*_address*/, uint16_t /*_value*/, unsigned /*_io_len*/) {}
	virtual void save_state(StateBuf &) {}
	virtual void restore_state(StateBuf &) {}

protected:
	void install(const IOPortsInterval *_io, unsigned _len);
	void remove(const IOPortsInterval *_io, unsigned _len);
	static void rebase_ports(IOPorts::iterator _port0, IOPorts::iterator _portN, unsigned _old_base, unsigned _new_base);
};


#endif
