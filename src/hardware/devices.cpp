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

#include "ibmulator.h"
#include "devices.h"
#include "iodevice.h"
#include "hardware/cpu/core.h"
#include "machine.h"
#include <cstring>

Devices g_devices;



Devices::Devices()
{
}

Devices::~Devices()
{
}

void Devices::init()
{
	for(auto dev : m_devices) {
		dev.second->init();
	}
}

void Devices::reset(uint _signal)
{
	//only reset signals handled by devices are
	//MACHINE_HARD_RESET, MACHINE_POWER_ON, and DEVICE_SOFT_RESET.
	if(_signal == CPU_SOFT_RESET) {
		return;
	}
	for(auto dev : m_devices) {
		dev.second->reset(_signal);
	}
}

void Devices::config_changed()
{
	for(auto dev : m_devices) {
		dev.second->config_changed();
	}
}

void Devices::save_state(StateBuf &_state)
{
	for(auto dev : m_devices) {
		dev.second->save_state(_state);
	}
}

void Devices::restore_state(StateBuf &_state)
{
	uint restored = 0;
	while(_state.get_bytesleft() && restored<m_devices.size()) {
		StateHeader h;
		_state.get_next_lump_header(h);
		if(h.name.length()==0) {
			PERRF(LOG_MACHINE, "unknown device state %u\n", restored);
			throw std::exception();
		}
		if(!m_devices.count(h.name)) {
			PERRF(LOG_MACHINE, "can't find device '%s'\n", h.name.c_str());
			throw std::exception();
		}
		m_devices[h.name]->restore_state(_state);
		restored++;
	}
	if(restored != m_devices.size()) {
		PERRF(LOG_MACHINE, "restored %u out of %u devices\n", restored, m_devices.size());
		throw std::exception();
	}
}

void Devices::register_device(IODevice *_iodev)
{
	std::string name = _iodev->get_name();
	if(m_devices.count(name)) {
		PERRF(LOG_MACHINE, "a device named '%s' is already registered\n", name.c_str());
		return;
	}
	m_devices[name] = _iodev;
}

void Devices::register_read_handler(IODevice *_iodev, uint16_t _port, uint _mask)
{
	ASSERT(_iodev);

	if(m_read_handlers[_port].device != NULL) {
		PERRF(LOG_MACHINE, "IO device %s address conflict(read) with device %s at IO address 0x%04X\n",
				_iodev->get_name(), m_read_handlers[_port].device->get_name(), _port);
		return;
	}
	if(_mask & 2) {
		if(_port==PORTMAX) {
			PDEBUGF(LOG_V2, LOG_MACHINE,
				"Registering 16-bit IO device %s at address 0x%04Xh\n",
				_iodev->get_name(),
				_port
			);
		}
		if(m_read_handlers[_port+1].device != NULL) {
			PDEBUGF(LOG_V2, LOG_MACHINE,
				"IO device %s at address 0x%04Xh is 16-bit but address 0x%04Xh is registered to %s\n",
				_iodev->get_name(),
				_port,
				_port+1,
				m_read_handlers[_port+1].device->get_name()
			);
		}
	}

	m_read_handlers[_port].device = _iodev;
	m_read_handlers[_port].mask = _mask;
}

void Devices::register_write_handler(IODevice *_iodev, uint16_t _port, uint _mask)
{
	ASSERT(_iodev);

	if(m_write_handlers[_port].device != NULL) {
		PERRF(LOG_MACHINE, "IO device %s address conflict(write) with device %s at IO address 0x%04X\n",
				_iodev->get_name(), m_write_handlers[_port].device->get_name(), _port);
		return;
	}
	if(_mask & 2) {
		if(_port==PORTMAX) {
			PDEBUGF(LOG_V2, LOG_MACHINE,
				"Registering 16-bit IO device %s at address 0x%04Xh\n",
				_iodev->get_name(),
				_port
			);
		}
		if(m_write_handlers[_port+1].device != NULL) {
			PDEBUGF(LOG_V2, LOG_MACHINE,
				"IO device %s at address 0x%04X is 16-bit but address 0x%04X is registered to %s\n",
				_iodev->get_name(),
				_port,
				_port+1,
				m_write_handlers[_port+1].device->get_name()
			);
		}
	}
	m_write_handlers[_port].device = _iodev;
	m_write_handlers[_port].mask = _mask;
}

void Devices::unregister_read_handler(uint16_t _port)
{
	m_read_handlers[_port].device = NULL;
}

void Devices::unregister_write_handler(uint16_t _port)
{
	m_write_handlers[_port].device = NULL;
}



void Devices::power_off()
{
	for(auto dev : m_devices) {
		dev.second->power_off();
	}
}

uint8_t Devices::read_byte(uint16_t _port)
{
	io_handler_t &iohdl = m_read_handlers[_port];

	if(iohdl.device == NULL || !(iohdl.mask & 1)) {
		PINFOF(LOG_V2, LOG_MACHINE, "Unhandled read from port 0x%04X (CS:IP=%X:%X)\n",
				_port, REG_CS.sel.value, REG_IP);
		return 0xFF;
	}
	return uint8_t(iohdl.device->read(_port, 1));
}

uint16_t Devices::read_word(uint16_t _port)
{
	if(_port & 1) {
		PDEBUGF(LOG_V2, LOG_MACHINE, "16-bit port read at odd address 0x%04Xh\n", _port);
	}
	io_handler_t &iohdl = m_read_handlers[_port];
	if(iohdl.mask & 2) {
		return iohdl.device->read(_port, 2);
	}

	uint8_t b0, b1;
	b0 = read_byte(_port);
	b1 = read_byte(_port+1);
	return b0 | (b1<<8);
}


void Devices::write_byte(uint16_t _port, uint8_t _value)
{
	io_handler_t &iohdl = m_write_handlers[_port];

	if(iohdl.device == NULL || !(iohdl.mask & 1)) {
		PINFOF(LOG_V2, LOG_MACHINE, "Unhandled write to port 0x%04X (CS:IP=%X:%X)\n",
				_port, REG_CS.sel.value, REG_IP);
		return;
	}
	iohdl.device->write(_port, _value, 1);
}

void Devices::write_word(uint16_t _port, uint16_t _value)
{
	if(_port & 1) {
		PDEBUGF(LOG_V2, LOG_MACHINE, "16-bit port write at odd address 0x%04X\n", _port);
	}

	io_handler_t &iohdl = m_write_handlers[_port];
	if(iohdl.mask & 2) {
		iohdl.device->write(_port, _value, 2);
		return;
	}

	write_byte(_port, uint8_t(_value));
	write_byte(_port+1, uint8_t(_value>>8));
}
