/*
 * Copyright (C) 2015-2021  Marco Bortolin
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
#include "hardware/cpu.h"
#include "hardware/cpu/mmu.h"
#include "hardware/devices/vga.h"
#include <fstream>
#include <cstring>
#include <algorithm>
#include <cmath>

#define DRAM_TIME_NS 120.0

Memory g_memory;


Memory::Memory()
:
m_mappings_namecnt(0)
{
	/* The 286 and the 386SX both have a 24-bit address bus. The 386DX has a
	 * 32-bit address bus, but the PS/1 was equipped with the SX variant, so the
	 * system supported only 16MB of RAM, and the ROM BIOS was mapped at
	 * 0xFC0000
	 */
	m_s.mask = 0x00FFFFFF;
	m_s.A20_enabled = true;
	memset(m_s.mapstate, MEM_ANY, sizeof(m_s.mapstate));

	m_ram.buffer = nullptr;
	m_ram.buffer_size = 0;

	remap(0, 0xFFFFFFFF);
}

Memory::~Memory()
{
	delete[] m_ram.buffer;
}

void Memory::init()
{
	//register_trap(0x400, 0x4FF, MEM_TRAP_READ|MEM_TRAP_WRITE, &Memory::s_debug_40h_trap);
	//register_trap(0x500, 0x9ffff, MEM_TRAP_READ|MEM_TRAP_WRITE, &Memory::s_debug_trap);
	/*
	for(int i=0; i<0x600000; i+=0x10000) {
		register_trap(i, i+2, MEM_TRAP_READ|MEM_TRAP_WRITE, &Memory::s_debug_trap);
	}
	*/

	// sizes and cycles are finalized in config_changed()
	m_ram.low_mapping = add_mapping(0x000000, 0xA0000, MEM_MAPPING_INTERNAL,
			Memory::s_read<uint8_t>, Memory::s_read<uint16_t>, Memory::s_read<uint32_t>, this,
			Memory::s_write<uint8_t>, Memory::s_write<uint16_t>, Memory::s_write<uint32_t>, this);
	m_ram.high_mapping = add_mapping(0x100000, 0x00000, MEM_MAPPING_INTERNAL,
			Memory::s_read<uint8_t>, Memory::s_read<uint16_t>, Memory::s_read<uint32_t>, this,
			Memory::s_write<uint8_t>, Memory::s_write<uint16_t>, Memory::s_write<uint32_t>, this);
}

void Memory::reset(unsigned _signal)
{
	if(_signal == MACHINE_POWER_ON || _signal == MACHINE_HARD_RESET) {
		memset(m_ram.buffer, 0, m_ram.buffer_size);
	}
}

void Memory::config_changed()
{
	static std::map<std::string, unsigned> ram_str_size = {
		{ "none", 0   },
		{ "512K", 512 },
		{ "2M",   2  * KEBIBYTE },
		{ "4M",   4  * KEBIBYTE },
		{ "6M",   6  * KEBIBYTE },
		{ "8M",   8  * KEBIBYTE },
		{ "16M",  16 * KEBIBYTE },
	};
	static std::map<unsigned, std::string> ram_size_str = {
		{ 0            , "none" },
		{ 512          , "512K" },
		{ 2  * KEBIBYTE, "2M"   },
		{ 4  * KEBIBYTE, "4M"   },
		{ 6  * KEBIBYTE, "6M"   },
		{ 8  * KEBIBYTE, "8M"   },
		{ 16 * KEBIBYTE, "16M"  },
	};
	unsigned exp_ram = g_program.config().get_enum(MEM_SECTION, MEM_RAM_EXP, ram_str_size, g_machine.model().exp_ram);
	g_program.config().set_string(MEM_SECTION, MEM_RAM_EXP, ram_size_str[exp_ram]);

	m_ram.size = g_machine.model().board_ram + exp_ram;
	// the last 512 KiB are reserved for the ROM
	m_ram.size = std::min(16384u-512u-384u, m_ram.size);
	m_ram.size = std::max(128u, m_ram.size);
	m_ram.size -= m_ram.size % 128;
	m_ram.size *= KEBIBYTE;

	uint32_t low_mapping_size = std::min(m_ram.size, 0xA0000u);
	uint32_t high_mapping_size = m_ram.size - low_mapping_size;

	m_ram.buffer_size = MEBIBYTE + high_mapping_size;
	delete[] m_ram.buffer;
	// add 3 extra bytes for unaligned accesses at the end of buffer
	m_ram.buffer = new uint8_t[m_ram.buffer_size+3];

	resize_mapping(m_ram.low_mapping, 0x000000, low_mapping_size);
	resize_mapping(m_ram.high_mapping, 0x100000, high_mapping_size);

	unsigned speed_ns = g_program.config().get_int(MEM_SECTION, MEM_RAM_SPEED, g_machine.model().ram_speed);
	g_program.config().set_int(MEM_SECTION, MEM_RAM_SPEED, speed_ns);

	m_ram.cycles = 1 + std::ceil(double(speed_ns) / g_cpu.cycle_time_ns()); // address + data

	set_mapping_cycles(m_ram.low_mapping, m_ram.cycles, m_ram.cycles, m_ram.cycles);
	set_mapping_cycles(m_ram.high_mapping, m_ram.cycles, m_ram.cycles, m_ram.cycles);

	PINFOF(LOG_V0, LOG_MEM, "Installed RAM: %uKB (base: %uKB, extended: %uKB)\n",
		m_ram.size/KEBIBYTE, low_mapping_size/KEBIBYTE, high_mapping_size/KEBIBYTE);
	PINFOF(LOG_V2, LOG_MEM, "RAM speed: %u ns, %d/%d/%d cycles\n", speed_ns,
		m_ram.cycles, m_ram.cycles, (g_cpubus.width()==16)?m_ram.cycles*2:m_ram.cycles);

	memset(m_s.mapstate, MEM_ANY, sizeof(m_s.mapstate));
}

void Memory::save_state(StateBuf &_state)
{
	_state.write(&m_s, {sizeof(m_s), "Memory state"});
	_state.write(m_ram.buffer, {m_ram.buffer_size, "Memory buffer"});
}

void Memory::restore_state(StateBuf &_state)
{
	_state.read(&m_s, {sizeof(m_s), "Memory state"});
	_state.read(m_ram.buffer, {m_ram.buffer_size, "Memory buffer"});

	// every device that modify mappings during execution (eg. SVGA) must
	// restore its mappings state
	remap(0, 0xFFFFFFFF);
}

int Memory::add_mapping(uint32_t _base, uint32_t _size, unsigned _flags,
		mem_read_fn_t _read_byte, mem_read_fn_t _read_word, mem_read_fn_t _read_dword,
		void *_read_priv,
		mem_write_fn_t _write_byte, mem_write_fn_t _write_word, mem_write_fn_t _write_dword,
		void *_write_priv)
{
	uint64_t end = _base + _size;

	assert(_size % MEM_MAP_GRANULARITY == 0);
	assert(end / MEM_MAP_GRANULARITY < MEM_MAP_SIZE);

	m_mappings.push_back({
		++m_mappings_namecnt,
		true,
		_base, _size, _flags,
		{2, 2, 2},
		{_read_byte, _read_word, _read_dword, _read_priv},
		{_write_byte, _write_word, _write_dword, _write_priv}
	});

	remap(_base, end);

	return m_mappings_namecnt;
}

void Memory::resize_mapping(int _mapping, uint32_t _newbase, uint32_t _newsize)
{
	assert(_newsize % MEM_MAP_GRANULARITY == 0);
	auto it = std::find(m_mappings.begin(), m_mappings.end(), _mapping);
	if(it != m_mappings.end()) {
		if(it->base != _newbase && (it->enabled && it->size)) {
			it->enabled = false;
			remap(it->start(), it->end());
			it->enabled = true;
		}
		it->base = _newbase;
		it->size = _newsize;
		assert(it->end() / MEM_MAP_GRANULARITY < MEM_MAP_SIZE);
		remap(it->start(), it->end());
	} else {
		PERRF(LOG_MEM, "Cannot find mapping %d\n", _mapping);
	}
}

void Memory::remove_mapping(int _mapping)
{
	auto it = std::find(m_mappings.begin(), m_mappings.end(), _mapping);
	if(it != m_mappings.end()) {
		if(it->enabled && it->size) {
			it->enabled = false;
			remap(it->start(), it->end());
		}
		m_mappings.erase(it);
	} else {
		PERRF(LOG_MEM, "Cannot find mapping %d\n", _mapping);
	}
}

void Memory::enable_mapping(int _mapping, bool _enabled)
{
	auto it = std::find(m_mappings.begin(), m_mappings.end(), _mapping);
	if(it != m_mappings.end()) {
		if(it->enabled != _enabled) {
			it->enabled = _enabled;
			remap(it->start(), it->end());
		}
	} else {
		PERRF(LOG_MEM, "Cannot find mapping %d\n", _mapping);
	}
}

void Memory::set_mapping_funcs(int _mapping,
		mem_read_fn_t _read_byte, mem_read_fn_t _read_word, mem_read_fn_t _read_dword,
		void *_read_priv,
		mem_write_fn_t _write_byte, mem_write_fn_t _write_word, mem_write_fn_t _write_dword,
		void *_write_priv)
{
	auto it = std::find(m_mappings.begin(), m_mappings.end(), _mapping);
	if(it != m_mappings.end()) {
		it->read.byte  = _read_byte;
		it->read.word  = _read_word;
		it->read.dword = _read_dword;
		it->read.priv  = _read_priv;
		it->write.byte  = _write_byte;
		it->write.word  = _write_word;
		it->write.dword = _write_dword;
		it->write.priv  = _write_priv;
		remap(it->start(), it->end());
	} else {
		PERRF(LOG_MEM, "Cannot find mapping %d\n", _mapping);
	}
}

void Memory::set_mapping_cycles(int _mapping, int _byte, int _word, int _dword)
{
	auto it = std::find(m_mappings.begin(), m_mappings.end(), _mapping);
	if(it != m_mappings.end()) {
		it->cycles.byte  = _byte;
		it->cycles.word  = _word;
		it->cycles.dword = _dword;
		// no remap needed
	} else {
		PERRF(LOG_MEM, "Cannot find mapping %d\n", _mapping);
	}
}

void Memory::set_state(uint32_t _base, uint32_t _size, unsigned _state)
{
	assert(_size % MEM_MAP_GRANULARITY == 0);
	assert((_base+_size) / MEM_MAP_GRANULARITY < MEM_MAP_SIZE);

	for(uint32_t block=0; block<_size; block+=MEM_MAP_GRANULARITY) {
		m_s.mapstate[(_base + block) / MEM_MAP_GRANULARITY] = _state;
	}

	PDEBUGF(LOG_V2, LOG_MEM, "state 0x%05X .. 0x%05X : %02X\n",
			_base, _base+_size-1, _state);

	remap(_base, _base+_size);
}

void Memory::set_A20_line(bool _enabled)
{
	if(_enabled && !m_s.A20_enabled) {
		PDEBUGF(LOG_V2, LOG_MEM, "A20 line ENABLED\n");
		m_s.A20_enabled = true;
		m_s.mask = 0x00ffffff; // 24-bit address bus
		g_cpummu.TLB_flush();
	} else if(!_enabled && m_s.A20_enabled) {
		PDEBUGF(LOG_V2, LOG_MEM, "A20 line DISABLED\n");
		m_s.A20_enabled = false;
		m_s.mask = 0x00efffff; // 24-bit address bus with A20 masked
		g_cpummu.TLB_flush();
	}
}

template<>
uint32_t Memory::read_mapped<1>(uint32_t _addr, int &_cycles) const noexcept
{
	_addr &= m_s.mask;
	MemMapping *map = m_map[_addr / MEM_MAP_GRANULARITY].read;
	if(map->read.byte) {
		_cycles += map->cycles.byte;
		return map->read.byte(_addr, map->read.priv);
	}
	return 0xFF;
}

template<>
uint32_t Memory::read_mapped<2>(uint32_t _addr, int &_cycles) const noexcept
{
	_addr &= m_s.mask;
	MemMapping *map = m_map[_addr / MEM_MAP_GRANULARITY].read;
	if(map->read.word) {
		if((_addr&0x1) && (map->flags&MEM_MAPPING_EXTERNAL)) {
			/* 16bit external bus
			 * this is the case for 32-bit bus CPU for odd aligned words inside
			 * dword boundaries
			 */
			return (
				read_mapped<1>(_addr,   _cycles) |
				read_mapped<1>(_addr+1, _cycles) << 8
			);
		}
		// if odd address then it must be 32-bit internal bus
		_cycles += map->cycles.word;
		return map->read.word(_addr, map->read.priv);
	}
	return (
		read_mapped<1>(_addr,   _cycles) |
		read_mapped<1>(_addr+1, _cycles) << 8
	);
}

template<>
uint32_t Memory::read_mapped<4>(uint32_t _addr, int &_cycles) const noexcept
{
	_addr &= m_s.mask;
	MemMapping *map = m_map[_addr / MEM_MAP_GRANULARITY].read;
	if(map->read.dword) {
		_cycles += map->cycles.dword;
		return map->read.dword(_addr, map->read.priv);
	}
	return (
		read_mapped<2>(_addr,   _cycles) |
		read_mapped<2>(_addr+2, _cycles) << 16
	);
}

template<>
void Memory::write_mapped<1>(uint32_t _addr, uint32_t _data, int &_cycles) noexcept
{
	_addr &= m_s.mask;
	MemMapping *map = m_map[_addr / MEM_MAP_GRANULARITY].write;
	if(map->write.byte) {
		_cycles += map->cycles.byte;
		map->write.byte(_addr, _data, map->write.priv);
	}
}

template<>
void Memory::write_mapped<2>(uint32_t _addr, uint32_t _data, int &_cycles) noexcept
{
	_addr &= m_s.mask;
	MemMapping *map = m_map[_addr / MEM_MAP_GRANULARITY].write;
	if(map->write.word) {
		if((_addr&0x1) && (map->flags&MEM_MAPPING_EXTERNAL)) {
			/* 16bit external bus
			 * this is the case for 32-bit bus CPU for odd aligned words inside
			 * dword boundaries
			 */
			write_mapped<1>(_addr,   _data,    _cycles);
			write_mapped<1>(_addr+1, _data>>8, _cycles);
			return;
		}
		// if odd address then it must be 32-bit internal bus
		_cycles += map->cycles.word;
		map->write.word(_addr, _data, map->write.priv);
		return;
	}
	write_mapped<1>(_addr,   _data,    _cycles);
	write_mapped<1>(_addr+1, _data>>8, _cycles);
}

template<>
void Memory::write_mapped<4>(uint32_t _addr, uint32_t _data, int &_cycles) noexcept
{
	_addr &= m_s.mask;
	MemMapping *map = m_map[_addr / MEM_MAP_GRANULARITY].write;
	if(map->write.dword) {
		_cycles += map->cycles.dword;
		map->write.dword(_addr, _data, map->write.priv);
		return;
	}
	write_mapped<2>(_addr,    _data,      _cycles);
	write_mapped<2>(_addr+2, (_data>>16), _cycles);
}

uint8_t * Memory::get_buffer_ptr(uint32_t _addr)
{
	_addr &= m_s.mask;
	if(_addr > m_ram.buffer_size) {
		throw std::exception();
	}
	return &m_ram.buffer[_addr];
}

void Memory::DMA_read(uint32_t _addr, uint16_t _len, uint8_t *_buf)
{
	int c;
	for(uint16_t i=0; i<_len; i++) {
		_buf[i] = read<1>(_addr+i, c);
	}
}

void Memory::DMA_write(uint32_t _addr, uint16_t _len, uint8_t *_buf)
{
	int c;
	for(uint16_t i=0; i<_len; i++) {
		write<1>(_addr+i, _buf[i], c);
	}
}

void Memory::remap(uint32_t _start, uint32_t _end)
{
	if(_start == _end) {
		return;
	}
	static MemMapping nullmap = {
		0,         // name
		false,     // enabled
		0,         // base
		0,         // size
		0,         // flags
		{0, 0, 0}, // cycles
		{nullptr,nullptr,nullptr, nullptr}, // read
		{nullptr,nullptr,nullptr, nullptr}  // write
	};
	for(uint64_t i=_start; i<_end; i+=MEM_MAP_GRANULARITY) {
		int index = i / MEM_MAP_GRANULARITY;
		assert(index < MEM_MAP_SIZE);
		m_map[index].read = &nullmap;
		m_map[index].write = &nullmap;
	}
	for(auto mapping=m_mappings.begin(); mapping != m_mappings.end(); mapping++) {
		if(!mapping->enabled || mapping->size == 0) {
			continue;
		}
		if(mapping->start() < _end && mapping->end() > _start) {
			uint32_t start = mapping->start();
			uint32_t end = std::min(_end, mapping->end());
			for(uint64_t i=start; i<end; i+=MEM_MAP_GRANULARITY) {
				int index = i / MEM_MAP_GRANULARITY;
				assert(index < MEM_MAP_SIZE);
				if(mapping->read_is_allowed(m_s.mapstate[index])) {
					m_map[index].read = &*mapping;
				}
				if(mapping->write_is_allowed(m_s.mapstate[index])) {
					m_map[index].write = &*mapping;
				}
			}
		}
	}
	g_cpummu.TLB_flush();
}

bool Memory::MemMapping::read_is_allowed(unsigned _state)
{
	if(!read.byte && !read.word && !read.dword) {
		return false;
	}
	switch(_state & MEM_READ_MASK) {
		case MEM_READ_ANY:
			return true;
		case MEM_READ_DISABLED:
			return false;
		case MEM_READ_EXTERNAL:
			return !(flags & MEM_MAPPING_INTERNAL);
		case MEM_READ_INTERNAL:
			return !(flags & MEM_MAPPING_EXTERNAL);
		default:
			break;
	}
	return false;
}

bool Memory::MemMapping::write_is_allowed(unsigned _state)
{
	if(!write.byte && !write.word && !write.dword) {
		return false;
	}
	switch(_state & MEM_WRITE_MASK) {
		case MEM_WRITE_ANY:
			return true;
		case MEM_WRITE_DISABLED:
			return false;
		case MEM_WRITE_EXTERNAL:
			return !(flags & MEM_MAPPING_INTERNAL);
		case MEM_WRITE_INTERNAL:
			return !(flags & MEM_MAPPING_EXTERNAL);
		default:
			break;
	}
	return false;
}


/*******************************************************************************
 * DEBUGGING
 */

uint8_t  Memory::dbg_read_byte(uint32_t _addr) const noexcept
{
	_addr &= m_s.mask;
	int index = _addr / MEM_MAP_GRANULARITY;
	assert(index < MEM_MAP_SIZE);
	MemMapping *map = m_map[index].read;
	if(map->read.byte) {
		return map->read.byte(_addr, map->read.priv);
	}
	return 0xFF;
}

uint16_t Memory::dbg_read_word(uint32_t _addr) const noexcept
{
	_addr &= m_s.mask;
	int index = _addr / MEM_MAP_GRANULARITY;
	assert(index < MEM_MAP_SIZE);
	MemMapping *map = m_map[index].read;
	if(map->read.word) {
		return map->read.word(_addr, map->read.priv);
	}
	return (
		dbg_read_byte(_addr) | dbg_read_byte(_addr+1) << 8
	);
}

uint32_t Memory::dbg_read_dword(uint32_t _addr) const noexcept
{
	_addr &= m_s.mask;
	int index = _addr / MEM_MAP_GRANULARITY;
	assert(index < MEM_MAP_SIZE);
	MemMapping *map = m_map[index].read;
	if(map->read.dword) {
		return map->read.dword(_addr, map->read.priv);
	}
	return (
		dbg_read_word(_addr) | dbg_read_word(_addr+2) << 16
	);
}

uint64_t Memory::dbg_read_qword(uint32_t _addr) const noexcept
{
	return (
		uint64_t(dbg_read_dword(_addr)) | uint64_t(dbg_read_dword(_addr+4)) << 32
	);
}

void Memory::dump(const std::string &_filename, uint32_t _address, uint _len)
{
	if(_address+_len > m_ram.buffer_size) {
		PERRF(LOG_MEM, "can't read %u bytes from 0x%06X\n", _len, _address);
		throw std::exception();
	}

	std::ofstream file(_filename.c_str(), std::ofstream::binary);
	if(!file.is_open()) {
		PERRF(LOG_FS,"unable to open %s to write\n",_filename.c_str());
		throw std::exception();
	}

	file.write((char*)(m_ram.buffer + _address), _len);
	file.close();
}

void Memory::check_trap(uint32_t _address, uint8_t _mask, uint32_t _value, unsigned _len)
const noexcept
{
	if(MEMORY_TRAPS) {
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
	if(_rw == MEM_TRAP_READ) {
		op = read;
	} else {
		op = assign;
	}
	const char *format;
	switch(_len) {
		case 1:
			format = "%d[%04X] %s %02X\n";
			break;
		case 2:
			format = "%d[%04X] %s %04X\n";
			break;
		case 4:
		default:
			format = "%d[%04X] %s %08X\n";
			break;
	}
	PDEBUGF(LOG_V1, LOG_MEM, format, _len, _address, op, _value);
}

void Memory::s_debug_trap_ASCII(uint32_t _address,  // address
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
			uint8_t byte = g_memory.dbg_read_byte(addr++);
			if(byte>=32 && byte<=126) {
				*byte0 = byte;
			} else {
				*byte0 = '.';
			}
			byte0++;
		}
	} else {
		if(_len==1) {
			uint8_t byte = g_memory.dbg_read_byte(addr);
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
