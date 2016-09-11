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

#include "ibmulator.h"
#include "devices.h"
#include "iodevice.h"
#include "hardware/cpu/core.h"
#include "machine.h"
#include "program.h"
#include <cstring>

#include "devices/cmos.h"
#include "devices/pic.h"
#include "devices/pit.h"
#include "devices/dma.h"
#include "devices/vga.h"
#include "devices/keyboard.h"
#include "devices/floppy.h"
#include "devices/harddrv.h"
#include "devices/serial.h"
#include "devices/parallel.h"
#include "devices/systemboard.h"
#include "devices/pcspeaker.h"
#include "devices/ps1audio.h"
#include "devices/adlib.h"
#include "devices/gameport.h"

Devices g_devices;

Devices::Devices()
:
m_sysboard(nullptr),
m_dma(nullptr),
m_pic(nullptr),
m_pit(nullptr),
m_vga(nullptr),
m_cmos(nullptr)
{
}

Devices::~Devices()
{
	// call the devices' destructor!
	for(auto dev : m_devices) {
		delete dev.second;
	}
}

void Devices::init(Machine *_machine)
{
	m_machine = _machine;

	// install mandatory devices
	m_sysboard = install<SystemBoard>();
	m_dma = install<DMA>();
	m_pic = install<PIC>();
	m_pit = install<PIT>();
	m_vga = install<VGA>(); // TODO other VGA models?
	m_cmos = install<CMOS>();
	install<Keyboard>();
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

	m_last_io_time = 0;
}

void Devices::config_changed()
{
	// install mandatory devices
	m_sysboard = install<SystemBoard>();

	// install or remove optional devices
	install_only_if<FloppyCtrl>(
		FloppyCtrl::config_drive_type(0)!=FDD_NONE || FloppyCtrl::config_drive_type(1)!=FDD_NONE
	);
	install_only_if<HardDrive>(g_program.config().get_int(DRIVES_SECTION, DRIVES_HDD) > 0);
	install_only_if<PCSpeaker>(g_program.config().get_bool(PCSPEAKER_SECTION, PCSPEAKER_ENABLED));
	bool gameport =
	install_only_if<PS1Audio>(g_program.config().get_bool(PS1AUDIO_SECTION, PS1AUDIO_ENABLED));
	install_only_if<AdLib>(g_program.config().get_bool(ADLIB_SECTION, ADLIB_ENABLED));
	install_only_if<GamePort>(gameport);
	install_only_if<Serial>(g_program.config().get_bool(COM_SECTION, COM_ENABLED));
	install_only_if<Parallel>(g_program.config().get_bool(LPT_SECTION, LPT_ENABLED));

	PINFOF(LOG_V2, LOG_MACHINE, "Installed devices:\n");
	for(auto dev : m_devices) {
		PINFOF(LOG_V2, LOG_MACHINE, "  %s\n", dev.first.c_str());
	}

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
		if(h.name.length() == 0) {
			PERRF(LOG_MACHINE, "unknown device state %u\n", restored);
			throw std::exception();
		}
		if(m_devices.find(h.name) == m_devices.end()) {
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

template<class T>
T* Devices::install()
{
	if(m_devices[T::NAME] != nullptr) {
		PDEBUGF(LOG_V2, LOG_MACHINE, "device '%s' is already installed\n", T::NAME);
		return dynamic_cast<T*>(m_devices[T::NAME]);
	}
	T *device = new T(this);
	m_devices[T::NAME] = device;
	device->install();
	return device;
}

template<class T>
T* Devices::install_only_if(bool _condition)
{
	if(_condition) {
		return install<T>();
	} else {
		remove(T::NAME);
		return nullptr;
	}
}

void Devices::register_read_handler(IODevice *_iodev, uint16_t _port, uint _mask)
{
	assert(_iodev);

	if(m_read_handlers[_port].device != nullptr) {
		PERRF(LOG_MACHINE, "IO device %s address conflict(read) with %s at address 0x%04X\n",
				_iodev->name(), m_read_handlers[_port].device->name(), _port);
		return;
	}
	if(_mask & 2) {
		if(_port == PORT_MAX) {
			PDEBUGF(LOG_V2, LOG_MACHINE,
				"Registering 16-bit IO device %s at address 0x%04Xh\n",
				_iodev->name(),
				_port
			);
		}
		if(m_read_handlers[_port+1].device != nullptr) {
			PDEBUGF(LOG_V2, LOG_MACHINE,
				"IO device %s at address 0x%04Xh is 16-bit but address 0x%04Xh is registered to %s\n",
				_iodev->name(),
				_port,
				_port+1,
				m_read_handlers[_port+1].device->name()
			);
		}
	}

	m_read_handlers[_port].device = _iodev;
	m_read_handlers[_port].mask = _mask;
}

void Devices::register_write_handler(IODevice *_iodev, uint16_t _port, uint _mask)
{
	assert(_iodev);

	if(m_write_handlers[_port].device != nullptr) {
		PERRF(LOG_MACHINE, "IO device %s address conflict(write) with %s at address 0x%04X\n",
				_iodev->name(), m_write_handlers[_port].device->name(), _port);
		return;
	}
	if(_mask & PORT_16BIT) {
		if(_port == PORT_MAX) {
			PDEBUGF(LOG_V2, LOG_MACHINE,
				"Registering 16-bit IO device %s at address 0x%04Xh\n",
				_iodev->name(),
				_port
			);
		}
		if(m_write_handlers[_port+1].device != nullptr) {
			PDEBUGF(LOG_V2, LOG_MACHINE,
				"IO device %s at address 0x%04X is 16-bit but address 0x%04X is registered to %s\n",
				_iodev->name(),
				_port,
				_port+1,
				m_write_handlers[_port+1].device->name()
			);
		}
	}
	m_write_handlers[_port].device = _iodev;
	m_write_handlers[_port].mask = _mask;
}

void Devices::unregister_read_handler(uint16_t _port)
{
	m_read_handlers[_port].device = nullptr;
	m_read_handlers[_port].mask = 0;
}

void Devices::unregister_write_handler(uint16_t _port)
{
	m_write_handlers[_port].device = nullptr;
	m_write_handlers[_port].mask = 0;
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

	m_last_io_time = 0;
	if(!(iohdl.mask & PORT_8BIT)) {
		PDEBUGF(LOG_V2, LOG_MACHINE, "Unhandled read from port 0x%04X\n", _port);
		return 0xFF;
	}
	return uint8_t(iohdl.device->read(_port, 1));
}

uint16_t Devices::read_word(uint16_t _port)
{
	uint16_t value = 0xFFFF;
	io_handler_t &iohdl = m_read_handlers[_port];

	m_last_io_time = 0;
	if((_port & 1) || !(iohdl.mask & PORT_16BIT)) {
		uint16_t b0 = read_byte(_port);
		unsigned io_time = m_last_io_time;
		m_last_io_time = 0;
		uint16_t b1 = read_byte(_port + 1);
		m_last_io_time += io_time;
		value = b0 | (b1<<8);
	} else if(iohdl.mask & PORT_16BIT) {
		value = iohdl.device->read(_port, 2);
	}
	return value;
}

uint32_t Devices::read_dword(uint16_t _port)
{
	uint32_t w0 = read_word(_port);
	uint32_t w1 = read_word(_port + 2);
	return w0 | (w1<<16);
}

void Devices::write_byte(uint16_t _port, uint8_t _value)
{
	io_handler_t &iohdl = m_write_handlers[_port];

	m_last_io_time = 0;
	if(!(iohdl.mask & PORT_8BIT)) {
		PDEBUGF(LOG_V2, LOG_MACHINE, "Unhandled write to port 0x%04X\n", _port);
		return;
	}
	iohdl.device->write(_port, _value, 1);
}

void Devices::write_word(uint16_t _port, uint16_t _value)
{
	io_handler_t &iohdl = m_write_handlers[_port];

	m_last_io_time = 0;
	if((_port & 1) || !(iohdl.mask & PORT_16BIT)) {
		/* If you output a word to an odd-numbered I/O port, it's done in two
		 * operations using A0 and BHE/ as it would if you were writing a word to a
		 * memory address. If you output a word to an 8-bit device, the motherboard
		 * runs it in two cycles.
		 */
		/* Reading/writing a word to an odd address on other CPUs (e.g. Motorola 68K)
		 * is illegal and throws an exception. This behavior is also an option on
		 * the 486 and later chips (cf the AC bit in EFLAGS and the AM bit in CR0).
		 */
		write_byte(_port, uint8_t(_value));
		unsigned io_time = m_last_io_time;
		m_last_io_time = 0;
		write_byte(_port+1, uint8_t(_value>>8));
		m_last_io_time += io_time;
	} else if(iohdl.mask & PORT_16BIT) {
		iohdl.device->write(_port, _value, 2);
	}
}

void Devices::write_dword(uint16_t _port, uint32_t _value)
{
	write_word(_port, _value);
	write_word(_port+2, _value>>16);
}

void Devices::remove(const char *_name)
{
	if(m_devices.find(_name) == m_devices.end()) {
		return;
	}
	m_devices[_name]->remove();
	delete m_devices[_name];
	m_devices.erase(_name);
}
/*
void Devices::remove_all()
{
	for(int i=0; i<=PORT_MAX; i++) {
		m_read_handlers[i].device = nullptr;
		m_read_handlers[i].mask = 0;
		m_write_handlers[i].device = nullptr;
		m_write_handlers[i].mask = 0;
	}
	for(auto dev : m_devices) {
		dev.second->remove();
	}
	for(auto dev : m_devices) {
		delete dev.second;
	}
	m_devices.clear();
}
*/
