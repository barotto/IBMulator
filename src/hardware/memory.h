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

#ifndef IBMULATOR_HW_MEMORY_H
#define IBMULATOR_HW_MEMORY_H

#include "model.h"
#include "statebuf.h"
#include "interval_tree.h"
#include "devices/hddparams.h"

#define KEBIBYTE           1024u
#define MEBIBYTE           (1024u * KEBIBYTE)
#define MAX_MEM_SIZE       (16u * MEBIBYTE)
#define MAX_BASE_MEM_SIZE  (640u * KEBIBYTE)
#define SYS_ROM_SIZE       (512u * KEBIBYTE)
#define SYS_ROM_ADDR       0xF80000
#define SYS_ROM_LOBASEADDR 0xE0000
#define SYS_ROM_HIBASEADDR 0xFFFFF
#define MAX_EXT_MEM_SIZE   (MAX_MEM_SIZE - MEBIBYTE - SYS_ROM_SIZE)

#define MEM_MAP_SIZE        0x40000
#define MEM_MAP_GRANULARITY 0x4000

#define MEM_MAPPING_EXTERNAL 1  // memory on external bus
#define MEM_MAPPING_INTERNAL 2  // system RAM

#define MEM_READ_MASK       0x0F
#define MEM_READ_DISABLED   0x00
#define MEM_READ_INTERNAL   0x01
#define MEM_READ_EXTERNAL   0x02
#define MEM_READ_ANY        0x03

#define MEM_WRITE_MASK      0xF0
#define MEM_WRITE_DISABLED  0x00
#define MEM_WRITE_INTERNAL  0x10
#define MEM_WRITE_EXTERNAL  0x20
#define MEM_WRITE_ANY       0x30

#define MEM_DISABLED (MEM_READ_DISABLED | MEM_WRITE_DISABLED)
#define MEM_INTERNAL (MEM_READ_INTERNAL | MEM_WRITE_INTERNAL)
#define MEM_EXTERNAL (MEM_READ_EXTERNAL | MEM_WRITE_EXTERNAL)
#define MEM_ANY      (MEM_READ_ANY | MEM_WRITE_ANY)

class Memory;
extern Memory g_memory;

#define MEM_TRAP_READ  0x1
#define MEM_TRAP_WRITE 0x2

typedef std::function<void(
		uint32_t,  // address
		uint8_t,   // read or write
		uint32_t,  // value read or written
		uint8_t    // data lenght (1=byte, 2=word, 4=dword)
	)> memtrap_fun_t;

struct memtrap_t {
	uint mask;
	memtrap_fun_t func;

	memtrap_t() : mask(0) {};
	memtrap_t(uint _mask, memtrap_fun_t _func)
	:  mask(_mask), func(_func) {}
};

typedef Interval<memtrap_t> memtrap_interval_t;
typedef IntervalTree<memtrap_t> memtrap_intervalTree_t;

typedef uint32_t (*mem_read_fn_t)(uint32_t _address, void *_privdata);
typedef void (*mem_write_fn_t)(uint32_t _address, uint32_t _value, void *_privdata);

class Memory
{
	friend class CPUBus;

protected:
	struct {
		uint32_t size;
		int low_mapping;
		int high_mapping;
		uint8_t *buffer;
		uint32_t buffer_size;
		int cycles;
		unsigned exp;
	} m_ram;

	struct {
		bool A20_enabled;
		uint32_t mask;
		unsigned mapstate[MEM_MAP_SIZE];
	} m_s;

	memtrap_intervalTree_t m_traps_tree;
	std::vector<memtrap_interval_t> m_traps_intervals;

	struct MemMapping
	{
		int name;
		bool enabled;
		uint32_t base;
		uint32_t size;
		uint32_t flags;
		struct {
			int byte, word, dword;
		} cycles;
		struct {
			mem_read_fn_t byte, word, dword;
			void *priv;
		} read;
		struct {
			mem_write_fn_t byte, word, dword;
			void *priv;
		} write;

		bool read_is_allowed(unsigned _state);
		bool write_is_allowed(unsigned _state);
		uint32_t start() { return base; }
		uint32_t end() { return base+size; }

		bool operator==(const MemMapping &_mm) const { return name == _mm.name; }
		bool operator==(int _mm) const { return name == _mm; }
	};

	std::list<MemMapping> m_mappings;
	int m_mappings_namecnt;

	struct MapEntry {
		MemMapping *read;
		MemMapping *write;
	};
	MapEntry m_map[MEM_MAP_SIZE];

public:
	Memory();
	~Memory();

	void init();
	void reset(unsigned _signal);
	void config_changed();
	void check_trap(uint32_t _address, uint8_t _mask, uint32_t _value, unsigned _len) const noexcept;

	int add_mapping(uint32_t _base, uint32_t _size, unsigned _flags,
			mem_read_fn_t _read_byte=nullptr,
			mem_read_fn_t _read_word=nullptr,
			mem_read_fn_t _read_dword=nullptr,
			void *_r_priv=nullptr,
			mem_write_fn_t _write_byte=nullptr,
			mem_write_fn_t _write_word=nullptr,
			mem_write_fn_t _write_dword=nullptr,
			void *_w_priv=nullptr);
	void resize_mapping(int _mapping, uint32_t _newbase, uint32_t _newsize);
	void enable_mapping(int _mapping, bool _enabled);
	void remove_mapping(int _mapping);
	void set_mapping_funcs(int _mapping,
			mem_read_fn_t _read_byte, mem_read_fn_t _read_word, mem_read_fn_t _read_dword,
			void *_read_priv,
			mem_write_fn_t _write_byte, mem_write_fn_t _write_word, mem_write_fn_t _write_dword,
			void *_write_priv);
	void set_mapping_cycles(int _mapping, int _byte, int _word, int _dword);
	void set_state(uint32_t _base, uint32_t _size, unsigned _state);

	void set_A20_line(bool _enabled);
	inline bool get_A20_line() const { return m_s.A20_enabled; }

	uint8_t *get_buffer_ptr(uint32_t _address);
	uint32_t get_buffer_size() { return m_ram.buffer_size; }

	inline int dram_cycles() const { return m_ram.cycles; }
	inline uint32_t dram_size() const { return m_ram.size; }
	inline unsigned dram_exp() const { return m_ram.exp; }

	void DMA_read(uint32_t addr, uint16_t len, uint8_t *buf);
	void DMA_write(uint32_t addr, uint16_t len, uint8_t *buf);

	void save_state(StateBuf &);
	void restore_state(StateBuf &);

	uint8_t  dbg_read_byte (uint32_t _addr) const noexcept;
	uint16_t dbg_read_word (uint32_t _addr) const noexcept;
	uint32_t dbg_read_dword(uint32_t _addr) const noexcept;
	uint64_t dbg_read_qword(uint32_t _addr) const noexcept;
	void dump(const std::string &_filename, uint32_t _address, uint _len);
	void register_trap(uint32_t _lo, uint32_t _hi, uint _mask, memtrap_fun_t _fn);
	static void s_debug_trap(uint32_t _address, uint8_t _rw, uint32_t _value, uint8_t _len);
	static void s_debug_trap_ASCII(uint32_t _address, uint8_t _rw, uint32_t _value, uint8_t _len);
	static void s_debug_40h_trap(uint32_t _address, uint8_t _rw, uint32_t _value, uint8_t _len);

private:
	void remap(uint32_t _start, uint32_t _end);

	// read functions for CPUBus
	template<unsigned LEN> inline
	uint32_t read(uint32_t _address, int &_cycles) const noexcept
	{
		return read_mapped<LEN>(_address, _cycles);
	}
	template<unsigned LEN> inline
	uint32_t read_t(uint32_t _address, unsigned _trap_len, int &_cycles) const noexcept
	{
		#if MEMORY_TRAPS
		uint32_t value = read_mapped<LEN>(_address, _cycles);
		check_trap(_address, MEM_TRAP_READ, value, _trap_len);
		return value;
		#else
		UNUSED(_trap_len);
		return read_mapped<LEN>(_address, _cycles);
		#endif
	}
	template<unsigned LEN>
	uint32_t read_mapped(uint32_t _address, int &_cycles) const noexcept { assert(false); return ~0; }


	// write functions for CPUBus
	template<unsigned LEN>
	void write(uint32_t _addr, uint32_t _data, int &_cycles) noexcept
	{
		write_mapped<LEN>(_addr, _data, _cycles);
	}
	template<unsigned LEN> inline
	void write_t(uint32_t _addr, uint32_t _data, unsigned _trap_len, int &_cycles) noexcept
	{
		write_mapped<LEN>(_addr, _data, _cycles);
		#if MEMORY_TRAPS
		check_trap(_addr, MEM_TRAP_WRITE, _data, _trap_len);
		#else
		UNUSED(_trap_len);
		#endif
	}
	template<unsigned LEN>
	void write_mapped(uint32_t _addr, uint32_t _data, int &_cycles) noexcept { assert(false); }


	// static RAM buffer read/write for mem mapping
	
	template<typename T>
	static uint32_t s_read(uint32_t _addr, void *_priv)
	{ assert(false); return ~0; }
	
	template<typename T>
	static void s_write(uint32_t _addr, uint32_t _value, void *_priv)
	{ assert(false); }
};

template<> inline uint32_t Memory::s_read<uint8_t>(uint32_t _addr, void *_priv)
{
	return ((Memory*)_priv)->m_ram.buffer[_addr];
}
template<> inline uint32_t Memory::s_read<uint16_t>(uint32_t _addr, void *_priv)
{
	return ((Memory*)_priv)->m_ram.buffer[_addr] |
	       ((Memory*)_priv)->m_ram.buffer[_addr + 1] << 8;
}
template<> inline uint32_t Memory::s_read<uint32_t>(uint32_t _addr, void *_priv)
{
	return ((Memory*)_priv)->m_ram.buffer[_addr] | 
	       ((Memory*)_priv)->m_ram.buffer[_addr + 1] << 8  |
	       ((Memory*)_priv)->m_ram.buffer[_addr + 2] << 16 |
	       ((Memory*)_priv)->m_ram.buffer[_addr + 3] << 24;
}

template<> inline void Memory::s_write<uint8_t>(uint32_t _addr, uint32_t _value, void *_priv)
{
	((Memory*)_priv)->m_ram.buffer[_addr] = _value;
}
template<> inline void Memory::s_write<uint16_t>(uint32_t _addr, uint32_t _value, void *_priv)
{
	((Memory*)_priv)->m_ram.buffer[_addr]     = _value;
	((Memory*)_priv)->m_ram.buffer[_addr + 1] = _value >> 8;
}
template<> inline void Memory::s_write<uint32_t>(uint32_t _addr, uint32_t _value, void *_priv)
{
	((Memory*)_priv)->m_ram.buffer[_addr]     = _value;
	((Memory*)_priv)->m_ram.buffer[_addr + 1] = _value >> 8;
	((Memory*)_priv)->m_ram.buffer[_addr + 2] = _value >> 16;
	((Memory*)_priv)->m_ram.buffer[_addr + 3] = _value >> 24;
}

template<> uint32_t Memory::read_mapped<1>(uint32_t _addr, int &_cycles) const noexcept;
template<> uint32_t Memory::read_mapped<2>(uint32_t _addr, int &_cycles) const noexcept;
template<> uint32_t Memory::read_mapped<4>(uint32_t _addr, int &_cycles) const noexcept;

template<> void Memory::write_mapped<1>(uint32_t _addr, uint32_t _data, int &_cycles) noexcept;
template<> void Memory::write_mapped<2>(uint32_t _addr, uint32_t _data, int &_cycles) noexcept;
template<> void Memory::write_mapped<4>(uint32_t _addr, uint32_t _data, int &_cycles) noexcept;

#endif
