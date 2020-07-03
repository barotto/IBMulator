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

#include "ibmulator.h"
#include "devices.h"
#include "iodevice.h"

IODEVICE_PORTS(IODevice) = {};

void IODevice::install()
{
	size_t len = ioports()->size();
	if(len) {
		install(&ioports()->at(0), len);
	}
}

void IODevice::remove()
{
	size_t len = ioports()->size();
	if(len) {
		remove(&ioports()->at(0), len);
	}
}

void IODevice::install(const IOPortsInterval *_io, unsigned _len)
{
	while(_len--) {
		if(_io->from == _io->to) {
		PDEBUGF(LOG_V2, LOG_MACHINE, "installing IO port  %03X     for %s\n", _io->from, name());
		} else {
			PDEBUGF(LOG_V2, LOG_MACHINE, "installing IO ports %03X-%03X for %s\n",
					_io->from, _io->to, name());
		}
		for(unsigned p=_io->from; p<=_io->to; p++) {
			if(_io->mask & PORT_READ) {
				m_devices->register_read_handler(this, p, _io->mask);
			}
			if(_io->mask & PORT_WRITE) {
				m_devices->register_write_handler(this, p, _io->mask);
			}
		}
		_io++;
	}
}

void IODevice::remove(const IOPortsInterval *_io, unsigned _len)
{
	while(_len--) {
		PDEBUGF(LOG_V2, LOG_MACHINE, "removing ioports %03X-%03X for %s\n",
				_io->from, _io->to, name());
		for(unsigned p=_io->from; p<=_io->to; p++) {
			if(_io->mask & PORT_READ) {
				m_devices->unregister_read_handler(p);
			}
			if(_io->mask & PORT_WRITE) {
				m_devices->unregister_write_handler(p);
			}
		}
		_io++;
	}
}

void IODevice::rebase_ports(IOPorts::iterator _port0, IOPorts::iterator _portN, unsigned _old_base, unsigned _new_base)
{
	IOPorts::iterator port = _port0;
	while(port != _portN) {
		assert(port->from >= _old_base);
		assert(port->to >= _old_base);
		assert(port->from <= port->to);
		port->from = port->from - _old_base + _new_base;
		port->to = port->to - _old_base + _new_base;
		port++;
	}
}
