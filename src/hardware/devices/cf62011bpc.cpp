/*
 * Copyright (C) 2017  Marco Bortolin
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

/* Bare bones emulation of the integrated TI CF62011BPC video adapter of the
 * IBM PS/1 2121. It only allows the POST procedure to succeed without errors.
 *
 * About the TI CF62011BPC:
 * there's no official documentation for this chip but it appears to be an XGA
 * on a 16bit bus. It probably lacks any coprocessor functionality but it
 * implements the Extended Graphics mode and has a VESA DOS driver which allows
 * video modes like 640x480 256-colors and 132-column x 25-row text when at
 * least 512KB of video memory is installed. No Windows 3 drivers are known to
 * exist.
 */

#include "ibmulator.h"
#include "cf62011bpc.h"
#include "hardware/memory.h"
#include "hardware/devices.h"
#include <cstring>

IODEVICE_PORTS(CF62011BPC) = {
	{ 0x2100, 0x210F, PORT_8BIT|PORT_RW }
};


CF62011BPC::CF62011BPC(Devices *_dev)
: VGA(_dev)
{
}

CF62011BPC::~CF62011BPC()
{
}

void CF62011BPC::install()
{
	VGA::install();
	IODevice::install(&VGA::ioports()->at(0), VGA::ioports()->size());

	g_memory.set_mapping_rfuncs(m_mapping,
			CF62011BPC::s_read_byte, nullptr, nullptr, this);
	g_memory.set_mapping_wfuncs(m_mapping,
			CF62011BPC::s_write_byte, nullptr, nullptr, this);
}

void CF62011BPC::remove()
{
	VGA::remove();
	IODevice::remove(&VGA::ioports()->at(0), VGA::ioports()->size());
}

void CF62011BPC::reset(unsigned _type)
{
	memset(m_s.xga_reg, 0, 0xF);
	m_s.xga_reg[0] = 0x1; // VGA Mode (address decode enabled)

	VGA::reset(_type);
}

uint32_t CF62011BPC::s_read_byte(uint32_t _addr, void *_priv)
{
	CF62011BPC *me = (CF62011BPC*)_priv;
	int mode = me->m_s.xga_reg[0] & 0x7;
	if(mode <= 0x1) {
		// VGA modes
		return VGA::s_read_byte(_addr, dynamic_cast<VGA*>(me));
	} else if(mode == 0x2 || mode == 0x3) {
		// 132-Column Text Mode
		PDEBUGF(LOG_V0, LOG_VGA, "Unsupported video mode\n");
		return 0;
	}
	// Extended Graphics modes
	int aperture = me->m_s.xga_reg[1] & 0x3;
	if(aperture == 0) {
		PDEBUGF(LOG_V0, LOG_VGA, "Memory aperture != 64KB not supported\n");
		return 0;
	}
	// aperture=0 no 64KB aperture (1MB or 4MB)
	// aperture=1 64KB at address 0xA0000
	// aperture=2 64KB at address 0xB0000
	uint32_t offset = 0xA0000 + (aperture-1)*0x10000;

	// check for out of window read
	if(_addr<offset || _addr>=offset+0x10000) {
		return 0;
	}

	_addr = (me->m_s.xga_reg[8]&0x3F)*0x10000 + _addr - offset;

	// check for out of memory read
	if(_addr >= me->m_memsize) {
		return 0;
	}

	return me->m_memory[_addr];
}

void CF62011BPC::s_write_byte(uint32_t _addr, uint32_t _value, void *_priv)
{
	CF62011BPC *me = (CF62011BPC*)_priv;
	int mode = me->m_s.xga_reg[0] & 0x7;
	if(mode <= 0x1) {
		// VGA modes
		return VGA::s_write_byte(_addr, _value, dynamic_cast<VGA*>(me));
	} else if(mode == 0x2 || mode == 0x3) {
		// 132-Column Text Mode
		PDEBUGF(LOG_V0, LOG_VGA, "Unsupported video mode\n");
		return;
	}
	// Extended Graphics modes
	int aperture = me->m_s.xga_reg[1] & 0x3;
	if(aperture == 0) {
		PDEBUGF(LOG_V0, LOG_VGA, "Memory aperture != 64KB not supported\n");
		return;
	}
	// aperture=0 no 64KB aperture (1MB or 4MB)
	// aperture=1 64KB at address 0xA0000
	// aperture=2 64KB at address 0xB0000
	uint32_t offset = 0xA0000 + (aperture-1)*0x10000;

	// check for out of window read
	if(_addr<offset || _addr>=offset+0x10000) {
		return;
	}

	_addr = (me->m_s.xga_reg[8]&0x3F)*0x10000 + _addr - offset;

	// check for out of memory read
	if(_addr >= me->m_memsize) {
		return;
	}

	me->m_memory[_addr] = _value;
}

uint16_t CF62011BPC::read(uint16_t _address, unsigned _io_len)
{
	switch(_address) {
		case 0x100:
			// Adapter ID value taken from the PCem project.
			// TODO verify on a real machine?
			return 0xFE;
		case 0x101:
			return 0xE8;
	}
	if(_address < 0x2100) {
		return VGA::read(_address, _io_len);
	}

	uint16_t value = m_s.xga_reg[_address&0xF];

	PDEBUGF(LOG_V2, LOG_MACHINE, "read  0x%03X -> 0x%04X\n", _address, value);

	return value;
}

void CF62011BPC::write(uint16_t _address, uint16_t _value, unsigned _io_len)
{
	if(_address < 0x2100) {
		return VGA::write(_address, _value, _io_len);
	}

	PDEBUGF(LOG_V2, LOG_MACHINE, "write 0x%03X <- 0x%04X\n", _address, _value);

	m_s.xga_reg[_address&0xF] = _value;
}
