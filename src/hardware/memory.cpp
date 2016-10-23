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
#include "memory.h"
#include "filesys.h"
#include "program.h"
#include "machine.h"
#include "hardware/devices/vga.h"
#include <fstream>
#include <cstring>


Memory g_memory;


Memory::Memory()
:
m_buffer(nullptr),
m_mainbuf_size(0),
m_base_size(0),
m_ext_size(0)
{
	/* The 286 and the 386SX both have a 24-bit address bus. The 386DX has a
	 * 32-bit address bus, but the PS/1 was equipped with the SX variant, so the
	 * system supported only 16MB of RAM, and the ROM BIOS was mapped at
	 * 0xFC0000
	 */
	m_s.mask = 0x00FFFFFF;
	m_s.A20_enabled = true;
}

Memory::~Memory()
{
	delete[] m_buffer;
}

void Memory::init()
{
	//register_trap(0x400, 0x4FF, 3, &Memory::s_debug_40h_trap);
}

void Memory::reset()
{
	memset(m_buffer, 0, m_base_size);
	memset(m_buffer+MEBIBYTE, 0, m_ext_size);

	set_A20_line(true);
}

void Memory::config_changed()
{
	int ram = g_program.config().get_int(MEM_SECTION, MEM_RAM_SIZE);
	// the last 512 KiB are reserved for the ROM
	ram = std::min(16384-512-384, ram);
	ram = std::max(128, ram);
	ram -= ram % 128;
	m_base_size = std::min(ram, 640);
	m_ext_size = ram - m_base_size;
	m_base_size *= KEBIBYTE;
	m_ext_size *= KEBIBYTE;
	m_mainbuf_size = MEBIBYTE + m_ext_size;
	delete[] m_buffer;
	m_buffer = new uint8_t[m_mainbuf_size];
	memset(m_buffer, 0, m_mainbuf_size);

	PINFOF(LOG_V0, LOG_MEM, "Installed RAM: %uKB (base: %uKB, extended: %uKB)\n",
			ram, m_base_size/KEBIBYTE, m_ext_size/KEBIBYTE);
}

#define MEM_STATE_NAME "Memory state"
#define MEM_DATA_NAME "Memory data"

void Memory::save_state(StateBuf &_state)
{
	StateHeader h;
	h.name = MEM_STATE_NAME;
	h.data_size = sizeof(m_s);
	_state.write(&m_s, h);

	h.name = MEM_DATA_NAME;
	h.data_size = m_mainbuf_size;
	_state.write(m_buffer, h);
}

void Memory::restore_state(StateBuf &_state)
{
	StateHeader h;
	h.name = MEM_STATE_NAME;
	h.data_size = sizeof(m_s);
	_state.read(&m_s, h);

	h.name = MEM_DATA_NAME;
	h.data_size = m_mainbuf_size;
	_state.read(m_buffer, h);
}

void Memory::set_A20_line(bool _enabled)
{
	if(_enabled) {
		PDEBUGF(LOG_V2, LOG_MEM, "A20 line ENABLED\n");
		m_s.A20_enabled = true;
		m_s.mask = 0x00ffffff; // 24-bit address bus
	} else {
		PDEBUGF(LOG_V2, LOG_MEM, "A20 line DISABLED\n");
		m_s.A20_enabled = false;
		m_s.mask = 0x00efffff; // 24-bit address bus with A20 masked
	}
}

uint8_t Memory::read_byte(uint32_t _address) const noexcept
{
	_address &= m_s.mask;

	//BASE RAM, EXTENDED RAM
	if(_address < m_base_size || (_address > 0xFFFFF && _address < m_mainbuf_size)) {
		return m_buffer[_address];
	}
	//SYSTEM ROM
	//TODO this works only for 24-bit address systems
	else if((_address >= 0xE0000 && _address <= 0xFFFFF) || _address >= SYS_ROM_ADDR) {
		return g_machine.sys_rom().read(_address);
	}
	//VGA MEMORY
	else if(_address >= 0xA0000 && _address <= 0xBFFFF) {
		return g_machine.devices().vga()->mem_read(_address);
	}
	//NO DATA
	return 0xFF;
}

void Memory::write_byte(uint32_t _address, uint8_t _value) noexcept
{
	_address &= m_s.mask;

	//BASE and EXTENDED RAM
	if(_address < m_base_size || (_address > 0xFFFFF && _address < m_mainbuf_size)) {
		m_buffer[_address] = _value;
	}
	//VGA MEMORY
	else if(_address >= 0xA0000 && _address <= 0xBFFFF) {
		g_machine.devices().vga()->mem_write(_address, _value);
	}
}

uint64_t Memory::read_qword(uint32_t _address) const noexcept
{
	uint32_t dw0 = read<4>(_address); //lo dword
	uint32_t dw1 = read<4>(_address+4); //hi dword
	uint64_t value = uint64_t(dw1)<<32 | dw0;

	return value;
}

uint64_t Memory::read_qword_notraps(uint32_t _address) const noexcept
{
	uint32_t dw0 = read_notraps<4>(_address); //lo dword
	uint32_t dw1 = read_notraps<4>(_address+4); //hi dword
	return uint64_t(dw1)<<32 | uint64_t(dw0);
}

uint8_t * Memory::get_phy_ptr(uint32_t _address)
{
	_address &= m_s.mask;
	if(_address > m_mainbuf_size) {
		throw std::exception();
	}
	return &m_buffer[_address];
}

void Memory::DMA_read(uint32_t _address, uint16_t _len, uint8_t *_buf)
{
	for(uint16_t i=0; i<_len; i++) {
		_buf[i] = read<1>(_address+i);
	}
}

void Memory::DMA_write(uint32_t _address, uint16_t _len, uint8_t *_buf)
{
	for(uint16_t i=0; i<_len; i++) {
		write<1>(_address+i, _buf[i]);
	}
}

void Memory::dump(const std::string &_filename, uint32_t _address, uint _len)
{
	if(_address+_len > m_mainbuf_size) {
		PERRF(LOG_MEM, "can't read %u bytes from 0x%06X\n", _len, _address);
		throw std::exception();
	}

	std::ofstream file(_filename.c_str(), std::ofstream::binary);
	if(!file.is_open()) {
		PERRF(LOG_FS,"unable to open %s to write\n",_filename.c_str());
		throw std::exception();
	}

	file.write((char*)(m_buffer + _address), _len);
	file.close();
}

void Memory::check_trap(uint32_t _address, uint8_t _mask, uint32_t _value, unsigned _len)
const noexcept
{
	std::vector<memtrap_interval_t> results;
	m_traps_tree.findOverlapping(_address, _address, results);
	for(auto t : results) {
		if(t.value.mask & _mask) {
			t.value.func(_address, _mask, _value, _len);
			if(STOP_AT_MEM_TRAPS) {
				g_machine.set_single_step(true);
			}
		}
	}
}

void Memory::register_trap(uint32_t _lo, uint32_t _hi, uint _mask, memtrap_fun_t _fn)
{
	m_traps_intervals.push_back(memtrap_interval_t(_lo, _hi, memtrap_t(_mask, _fn)));
	m_traps_tree = memtrap_intervalTree_t(m_traps_intervals);
}

void Memory::s_debug_trap(uint32_t _address,  // address
		uint8_t  _rw,    // read or write
		uint32_t _value, // value read or written
		uint8_t  _len    // data length (1=byte, 2=word, 4=dword)
		)
{
	const char *assign="<-", *read="->";
	const char *op;
	uint len = 20;
	char buf[len+1];
	buf[0] = 0;
	buf[len] = 0;
	uint32_t addr = _address;
	if(_rw == MEM_TRAP_READ) {
		op = read;
		char * byte0 = buf;
		while(len--) {
			uint8_t byte = g_memory.read_notraps<1>(addr++);
			if(byte>=32 && byte<=126) {
				*byte0 = byte;
			} else {
				*byte0 = '.';
			}
			byte0++;
		}
	} else {
		if(_len==1) {
			uint8_t byte = g_memory.read_notraps<1>(addr);
			if(byte>=32 && byte<=126) {
				buf[0] = byte;
			} else {
				buf[0] = '.';
			}
			buf[1] = 0;
		}
		op = assign;
	}
	const char *format;
	switch(_len) {
		case 1:
			format = "%d[%04X] %s %02X %s\n";
			break;
		case 2:
			format = "%d[%04X] %s %04X %s\n";
			break;
		case 4:
		default:
			format = "%d[%04X] %s %08X %s\n";
			break;
	}
	PDEBUGF(LOG_V1, LOG_MEM, format, _len, _address, op, _value, buf);
}

void Memory::s_debug_40h_trap(uint32_t _address,  // address
		uint8_t  _rw,    // read or write
		uint32_t _value, // value read or written
		uint8_t  _len    // data lenght (1=byte, 2=word, 4=dword)
		)
{
	const char *assign=":=", *read="=";
	const char *op;
	if(_rw == MEM_TRAP_READ) {
		op = read;
	} else {
		op = assign;
	}

	uint32_t offset = _address-0x400;

	PDEBUGF(LOG_V2, LOG_MEM, "%d[40:%04X] %s %04X (", _len, offset, op, _value);

	switch(offset) {
	case 0x0000:
	case 0x0001:
		PDEBUGF(LOG_V2, LOG_MEM, "BASE I/O ADDRESS OF FIRST SERIAL I/O PORT");
		break;

	case 0x0002:
	case 0x0003:
		PDEBUGF(LOG_V2, LOG_MEM, "BASE I/O ADDRESS OF SECOND SERIAL I/O PORT");
		break;

	case 0x0004:
	case 0x0005:
		PDEBUGF(LOG_V2, LOG_MEM, "BASE I/O ADDRESS OF THIRD SERIAL I/O PORT");
		break;

	case 0x0006:
	case 0x0007:
		PDEBUGF(LOG_V2, LOG_MEM, "BASE I/O ADDRESS OF FOURTH SERIAL I/O PORT");
		break;

	case 0x0008:
	case 0x0009:
		PDEBUGF(LOG_V2, LOG_MEM, "BASE I/O ADDRESS OF FIRST PARALLEL I/O PORT");
		break;

	case 0x000A:
	case 0x000B:
		PDEBUGF(LOG_V2, LOG_MEM, "BASE I/O ADDRESS OF SECOND PARALLEL I/O PORT");
		break;

	case 0x000C:
	case 0x000D:
		PDEBUGF(LOG_V2, LOG_MEM, "BASE I/O ADDRESS OF THIRD PARALLEL I/O PORT");
		break;

	case 0x000E:
	case 0x000F:
		PDEBUGF(LOG_V2, LOG_MEM, "BASE I/O ADDRESS OF LPT4 - SEGMENT OF EXTENDED BIOS DATA SEGMENT");
		break;

	case 0x0010:
	case 0x0011:
		PDEBUGF(LOG_V2, LOG_MEM, "INSTALLED HARDWARE");
		break;

	case 0x0012:
		PDEBUGF(LOG_V2, LOG_MEM, "MANUFACTURING TEST / POST SYSTEM FLAG");
		break;

	case 0x0013:
	case 0x0014:
		PDEBUGF(LOG_V2, LOG_MEM, "BASE MEMORY SIZE IN KBYTES");
		break;

	case 0x0015:
		PDEBUGF(LOG_V2, LOG_MEM, "ADAPTER MEMORY SIZE IN KBYTES / MANUFACTURING TEST SCRATCH PAD");
		break;

	case 0x0016:
		PDEBUGF(LOG_V2, LOG_MEM, "BIOS CONTROL FLAGS / MANUFACTURING TEST SCRATCH PAD");
		break;

	case 0x0017:
		PDEBUGF(LOG_V2, LOG_MEM, "KEYBOARD - STATUS FLAGS 1");
		break;

	case 0x0018:
		PDEBUGF(LOG_V2, LOG_MEM, "KEYBOARD - STATUS FLAGS 2");
		break;

	case 0x0019:
		PDEBUGF(LOG_V2, LOG_MEM, "KEYBOARD - ALT-nnn KEYPAD WORKSPACE");
		break;

	case 0x001A:
		PDEBUGF(LOG_V2, LOG_MEM, "KEYBOARD - POINTER TO NEXT CHARACTER IN KEYBOARD BUFFER");
		break;

	case 0x001C:
	case 0x001D:
		PDEBUGF(LOG_V2, LOG_MEM, "KEYBOARD - POINTER TO FIRST FREE SLOT IN KEYBOARD BUFFER");
		break;

	case 0x001E:
	case 0x001F:
	case 0x0020:
	case 0x0021:
	case 0x0022:
	case 0x0023:
	case 0x0024:
	case 0x0025:
	case 0x0026:
	case 0x0027:
	case 0x0028:
	case 0x0029:
	case 0x002A:
	case 0x002B:
	case 0x002C:
	case 0x002D:
	case 0x002E:
	case 0x002F:
	case 0x0030:
	case 0x0031:
	case 0x0032:
	case 0x0033:
	case 0x0034:
	case 0x0035:
	case 0x0036:
	case 0x0037:
	case 0x0038:
	case 0x0039:
	case 0x003A:
	case 0x003B:
	case 0x003C:
	case 0x003D:
		PDEBUGF(LOG_V2, LOG_MEM, "KEYBOARD - DEFAULT KEYBOARD CIRCULAR BUFFER");
		break;

	case 0x003E:
		PDEBUGF(LOG_V2, LOG_MEM, "DISKETTE - RECALIBRATE STATUS");
		break;

	case 0x003F:
		PDEBUGF(LOG_V2, LOG_MEM, "DISKETTE - MOTOR STATUS");
		break;

	case 0x0040:
		PDEBUGF(LOG_V2, LOG_MEM, "DISKETTE - MOTOR TURN-OFF TIMEOUT COUNT");
		break;

	case 0x0041:
		PDEBUGF(LOG_V2, LOG_MEM, "DISKETTE - LAST OPERATION STATUS");
		break;

	case 0x0042:
		PDEBUGF(LOG_V2, LOG_MEM, "DISK CONTROLLER STATUS REGISTER 0. ");
		break;

	case 0x0043:
		PDEBUGF(LOG_V2, LOG_MEM, "DISK CONTROLLER STATUS REGISTER 1. ");
		break;

	case 0x0044:
		PDEBUGF(LOG_V2, LOG_MEM, "DISK CONTROLLER STATUS REGISTER 2. ");
		break;

	case 0x0045:
	case 0x0046:
	case 0x0047:
	case 0x0048:
		PDEBUGF(LOG_V2, LOG_MEM, "DISK - FLOPPY/HARD DRIVE STATUS/COMMAND BYTES");
		break;

	case 0x0049:
		PDEBUGF(LOG_V2, LOG_MEM, "VIDEO - CURRENT VIDEO MODE");
		break;

	case 0x004A:
	case 0x004B:
		PDEBUGF(LOG_V2, LOG_MEM, "VIDEO - COLUMNS ON SCREEN");
		break;

	case 0x004C:
	case 0x004D:
		PDEBUGF(LOG_V2, LOG_MEM, "VIDEO - PAGE (REGEN BUFFER) SIZE IN BYTES");
		break;

	case 0x004E:
	case 0x004F:
		PDEBUGF(LOG_V2, LOG_MEM, "VIDEO - CURRENT PAGE START ADDRESS IN REGEN BUFFER");
		break;

	case 0x0050:
	case 0x0051:
	case 0x0052:
	case 0x0053:
	case 0x0054:
	case 0x0055:
	case 0x0056:
	case 0x0057:
	case 0x0058:
	case 0x0059:
	case 0x005A:
	case 0x005B:
	case 0x005C:
	case 0x005D:
	case 0x005E:
	case 0x005F:
		PDEBUGF(LOG_V2, LOG_MEM, "VIDEO - CURSOR POSITIONS");
		break;

	case 0x0060:
	case 0x0061:
		PDEBUGF(LOG_V2, LOG_MEM, "VIDEO - CURSOR TYPE");
		break;

	case 0x0062:
		PDEBUGF(LOG_V2, LOG_MEM, "VIDEO - CURRENT PAGE NUMBER");
		break;

	case 0x0063:
	case 0x0064:
		PDEBUGF(LOG_V2, LOG_MEM, "VIDEO - CRT CONTROLLER BASE I/O PORT ADDRESS");
		break;

	case 0x0065:
		PDEBUGF(LOG_V2, LOG_MEM, "VIDEO - CURRENT MODE SELECT REGISTER");
		break;

	case 0x0066:
		PDEBUGF(LOG_V2, LOG_MEM, "VIDEO - CURRENT SETTING OF CGA PALETTE REGISTER");
		break;

	case 0x0067:
	case 0x0068:
	case 0x0069:
	case 0x006A:
		PDEBUGF(LOG_V2, LOG_MEM, "RESET RESTART ADDRESS");
		break;

	case 0x006B:
		PDEBUGF(LOG_V2, LOG_MEM, "POST LAST UNEXPECTED INTERRUPT");
		break;

	case 0x006C:
	case 0x006D:
	case 0x006E:
	case 0x006F:
		PDEBUGF(LOG_V2, LOG_MEM, "TIMER TICKS SINCE MIDNIGHT");
		break;

	case 0x0070:
		PDEBUGF(LOG_V2, LOG_MEM, "TIMER OVERFLOW");
		break;

	case 0x0071:
		PDEBUGF(LOG_V2, LOG_MEM, "Ctrl-Break FLAG");
		break;

	case 0x0072:
	case 0x0073:
		PDEBUGF(LOG_V2, LOG_MEM, "POST RESET FLAG");
		break;

	case 0x0074:
		PDEBUGF(LOG_V2, LOG_MEM, "FIXED DISK LAST OPERATION STATUS (except ESDI drives)");
		break;

	case 0x0075:
		PDEBUGF(LOG_V2, LOG_MEM, "FIXED DISK - NUMBER OF FIXED DISK DRIVES");
		break;

	case 0x0076:
		PDEBUGF(LOG_V2, LOG_MEM, "FIXED DISK - CONTROL BYTE, IBM documented only for XT");
		break;

	case 0x0077:
		PDEBUGF(LOG_V2, LOG_MEM, "FIXED DISK - I/O port offset, IBM documented only for XT");
		break;

	case 0x0078:
		PDEBUGF(LOG_V2, LOG_MEM, "PARALLEL DEVICE 1 TIME-OUT COUNTER");
		break;

	case 0x0079:
		PDEBUGF(LOG_V2, LOG_MEM, "PARALLEL DEVICE 2 TIME-OUT COUNTER");
		break;

	case 0x007A:
		PDEBUGF(LOG_V2, LOG_MEM, "PARALLEL DEVICE 3 TIME-OUT COUNTER");
		break;

	case 0x007B:
		PDEBUGF(LOG_V2, LOG_MEM, "LPT4 TIME-OUT COUNTER / INT 4Bh FLAGS");
		break;

	case 0x007C:
		PDEBUGF(LOG_V2, LOG_MEM, "SERIAL DEVICE 1 TIMEOUT COUNTER");
		break;

	case 0x007D:
		PDEBUGF(LOG_V2, LOG_MEM, "SERIAL DEVICE 2 TIMEOUT COUNTER");
		break;

	case 0x007E:
		PDEBUGF(LOG_V2, LOG_MEM, "SERIAL DEVICE 3 TIMEOUT COUNTER");
		break;

	case 0x007F:
		PDEBUGF(LOG_V2, LOG_MEM, "SERIAL DEVICE 4 TIMEOUT COUNTER");
		break;

	case 0x0080:
	case 0x0081:
		PDEBUGF(LOG_V2, LOG_MEM, "KEYBOARD BUFFER START OFFSET FROM SEGMENT 40h (normally 1Eh)");
		break;

	case 0x0082:
	case 0x0083:
		PDEBUGF(LOG_V2, LOG_MEM, "KEYBOARD BUFFER END+1 OFFSET FROM SEGMENT 40h (normally 3Eh)");
		break;

	case 0x0084:
		PDEBUGF(LOG_V2, LOG_MEM, "VIDEO (EGA/MCGA/VGA) - ROWS ON SCREEN MINUS ONE");
		break;

	case 0x0085:
	case 0x0086:
		PDEBUGF(LOG_V2, LOG_MEM, "VIDEO (EGA/MCGA/VGA) - CHARACTER HEIGHT IN SCAN-LINES");
		break;

	case 0x0087:
		PDEBUGF(LOG_V2, LOG_MEM, "VIDEO (EGA/VGA) CONTROL: [MCGA: =00h]");
		break;

	case 0x0088:
		PDEBUGF(LOG_V2, LOG_MEM, "VIDEO (EGA/VGA) SWITCHES: [MCGA: reserved]");
		break;

	case 0x0089:
		PDEBUGF(LOG_V2, LOG_MEM, "VIDEO (MCGA/VGA) - MODE-SET OPTION CONTROL");
		break;

	case 0x008A:
		PDEBUGF(LOG_V2, LOG_MEM, "VIDEO (MCGA/VGA) - INDEX INTO DISPLAY COMBINATION CODE TBL");
		break;

	case 0x008B:
		PDEBUGF(LOG_V2, LOG_MEM, "DISKETTE MEDIA CONTROL");
		break;

	case 0x008C:
		PDEBUGF(LOG_V2, LOG_MEM, "FIXED DISK - CONTROLLER STATUS [not XT]");
		break;

	case 0x008D:
		PDEBUGF(LOG_V2, LOG_MEM, "FIXED DISK - CONTROLLER ERROR STATUS [not XT]");
		break;

	case 0x008E:
		PDEBUGF(LOG_V2, LOG_MEM, "FIXED DISK - INTERRUPT CONTROL [not XT]");
		break;

	case 0x008F:
		PDEBUGF(LOG_V2, LOG_MEM, "DISKETTE CONTROLLER INFORMATION [not XT]");
		break;

	case 0x0090:
		PDEBUGF(LOG_V2, LOG_MEM, "DISKETTE DRIVE 0 MEDIA STATE");
		break;

	case 0x0091:
		PDEBUGF(LOG_V2, LOG_MEM, "DISKETTE DRIVE 1 MEDIA STATE");
		break;

	case 0x0092:
		PDEBUGF(LOG_V2, LOG_MEM, "DISKETTE DRIVE 0 MEDIA STATE AT START OF OPERATION");
		break;

	case 0x0093:
		PDEBUGF(LOG_V2, LOG_MEM, "DISKETTE DRIVE 1 MEDIA STATE AT START OF OPERATION");
		break;

	case 0x0094:
		PDEBUGF(LOG_V2, LOG_MEM, "DISKETTE DRIVE 0 CURRENT TRACK NUMBER");
		break;

	case 0x0095:
		PDEBUGF(LOG_V2, LOG_MEM, "DISKETTE DRIVE 1 CURRENT TRACK NUMBER");
		break;

	case 0x0096:
		PDEBUGF(LOG_V2, LOG_MEM, "KEYBOARD STATUS BYTE 1");
		break;

	case 0x0097:
		PDEBUGF(LOG_V2, LOG_MEM, "KEYBOARD STATUS BYTE 2");
		break;

	case 0x0098:
	case 0x0099:
	case 0x009A:
	case 0x009B:
		PDEBUGF(LOG_V2, LOG_MEM, "TIMER2 (AT, PS exc Mod 30) - PTR TO USER WAIT-COMPLETE FLAG");
		break;

	case 0x009C:
	case 0x009D:
	case 0x009E:
	case 0x009F:
		PDEBUGF(LOG_V2, LOG_MEM, "TIMER2 (AT, PS exc Mod 30) - USER WAIT COUNT IN MICROSECONDS");
		break;

	case 0x00A0:
		PDEBUGF(LOG_V2, LOG_MEM, "TIMER2 (AT, PS exc Mod 30) - WAIT ACTIVE FLAG");
		break;

	case 0x00A1:
		PDEBUGF(LOG_V2, LOG_MEM, "BIT 5 SET IF LAN SUPPORT PROGRAM INTERRUPT ARBITRATOR PRESENT");
		break;

	case 0x00A4:
	case 0x00A5:
	case 0x00A6:
	case 0x00A7:
		PDEBUGF(LOG_V2, LOG_MEM, "PS/2 Mod 30 - SAVED FIXED DISK INTERRUPT VECTOR");
		break;

	case 0x00A8:
	case 0x00A9:
	case 0x00AA:
	case 0x00AB:
		PDEBUGF(LOG_V2, LOG_MEM, "VIDEO (EGA/MCGA/VGA) - POINTER TO VIDEO SAVE POINTER TABLE");
		break;

	case 0x00B0:
	case 0x00B1:
	case 0x00B2:
	case 0x00B3:
		PDEBUGF(LOG_V2, LOG_MEM, "BIOS ENTRY POINT");
		break;

	case 0x00CE:
	case 0x00CF:
		PDEBUGF(LOG_V2, LOG_MEM, "COUNT OF DAYS SINCE LAST BOOT");
		break;
	default:
		PDEBUGF(LOG_V2, LOG_MEM, "unknown");
		break;
	}
	PDEBUGF(LOG_V2, LOG_MEM, ")\n");
}
