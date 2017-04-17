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
	m_s.xga_reg[1] = 0x1; // aperture 64KB at address 0xA0000
	m_s.mem_offset = 0xA0000;
	m_s.mem_aperture = 0x10000;

	VGA::reset(_type);
}

void CF62011BPC::save_state(StateBuf &_state)
{
	PINFOF(LOG_V1, LOG_VGA, "%s: saving state\n", name());

	_state.write(&m_s, {sizeof(m_s), name()});

	VGA::save_state(_state);
}

void CF62011BPC::restore_state(StateBuf &_state)
{
	PINFOF(LOG_V1, LOG_VGA, "%s: restoring state\n", name());

	_state.read(&m_s, {sizeof(m_s), name()});

	VGA::restore_state(_state);
}

void CF62011BPC::update_mem_mapping()
{
	int mode = m_s.xga_reg[0] & 0x7;
	switch(mode) { // Display Mode field (bits 2-0)
		case 0: // VGA Mode (address decode disabled)
			PWARNF(LOG_VGA, "VGA Mode 0 (address decode disabled) not supported\n");
			break;
		case 1: // VGA Mode (address decode enabled)
			PDEBUGF(LOG_V1, LOG_VGA, "VGA Mode 1 (address decode enabled)\n");
			VGA::update_mem_mapping();
			break;
		case 2: // 132-Column Text Mode (address decode disabled)
		case 3: // 132-Column Text Mode (address decode enabled)
			PWARNF(LOG_VGA, "132-Column text video mode (%d) not supported\n", mode);
			break;
		default:
			// Extended Graphics mode
			g_memory.resize_mapping(m_mapping, m_s.mem_offset, m_s.mem_aperture);
			g_memory.set_mapping_funcs(m_mapping,
				CF62011BPC::s_mem_read<uint8_t>,
				CF62011BPC::s_mem_read<uint16_t>,
				CF62011BPC::s_mem_read<uint32_t>,
				this,
				CF62011BPC::s_mem_write<uint8_t>,
				CF62011BPC::s_mem_write<uint16_t>,
				CF62011BPC::s_mem_write<uint32_t>,
				this);
			PDEBUGF(LOG_V1, LOG_VGA, "Extended Graphics mode %d\n", mode);
			PDEBUGF(LOG_V1, LOG_VGA, "memory mapping: 0x%X .. 0x%X\n",
					m_s.mem_offset, m_s.mem_offset+m_s.mem_aperture-1);
			break;
	}
}

template<class T>
uint32_t CF62011BPC::s_mem_read(uint32_t _addr, void *_priv)
{
	CF62011BPC *me = (CF62011BPC*)_priv;

	// check for out of window read
	if(_addr+sizeof(T) > me->m_s.mem_offset+0x10000) {
		return 0xFF;
	}

	int aperture_index = me->m_s.xga_reg[8] & 0x3F;
	uint32_t addr = aperture_index*0x10000 + _addr - me->m_s.mem_offset;

	// check for out of memory read
	if(addr+sizeof(T) > me->m_memsize) {
		return 0xFF;
	}

	return *(T*)(&me->m_memory[addr]);
}

template<class T>
void CF62011BPC::s_mem_write(uint32_t _addr, uint32_t _value, void *_priv)
{
	CF62011BPC *me = (CF62011BPC*)_priv;

	// check for out of window write
	if(_addr+sizeof(T) > me->m_s.mem_offset+0x10000) {
		return;
	}

	int aperture_index = me->m_s.xga_reg[8] & 0x3F;
	uint32_t addr = aperture_index*0x10000 + _addr - me->m_s.mem_offset;

	// check for out of memory write
	if(addr+sizeof(T) > me->m_memsize) {
		return;
	}

	*(T*)(&me->m_memory[addr]) = _value;
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

	PDEBUGF(LOG_V2, LOG_VGA, "read  0x%03X -> 0x%04X\n", _address, value);

	return value;
}

void CF62011BPC::write(uint16_t _address, uint16_t _value, unsigned _io_len)
{
	if(_address < 0x2100) {
		return VGA::write(_address, _value, _io_len);
	}

	PDEBUGF(LOG_V2, LOG_VGA, "write 0x%03X <- 0x%04X\n", _address, _value);

	_address &= 0xF;

	switch(_address) {
		case 0: { // Operating Mode Register (Address 21x0)
			if(_value != m_s.xga_reg[_address]) {
				m_s.xga_reg[_address] = _value;
				update_mem_mapping();
			}
			break;
		}
		case 1: { // Aperture Control Register (Address 21x1)
			int aperture = _value & 0x3;
			if(aperture != 0) {
				// aperture=0 no 64KB aperture (1MB or 4MB) (not emulated)
				// aperture=1 64KB at address 0xA0000
				// aperture=2 64KB at address 0xB0000
				uint32_t new_offset = 0xA0000 + (aperture-1)*0x10000;
				uint32_t new_aperture = 0x10000;  // fixed 64KB for now
				if(new_offset != m_s.mem_offset || new_aperture != m_s.mem_aperture) {
					m_s.mem_offset = new_offset;
					m_s.mem_aperture = new_aperture;
					update_mem_mapping();
				}
			}
			m_s.xga_reg[_address] = _value;
			break;
		}
		default:
			m_s.xga_reg[_address] = _value;
			break;
	}
}
