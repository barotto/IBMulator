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

#ifndef IBMULATOR_HW_MEMORY_H
#define IBMULATOR_HW_MEMORY_H

#include "statebuf.h"
#include "interval_tree.h"

#define KEBIBYTE          1024u
#define MEBIBYTE          1024u * KEBIBYTE
#define MAX_MEM_SIZE      16u * MEBIBYTE
#define MAX_BASE_MEM_SIZE 640u * KEBIBYTE
#define SYS_ROM_SIZE      512u * KEBIBYTE
#define SYS_ROM_ADDR      0xF80000
#define SYS_ROM_LOBASEADDR  0xE0000
#define SYS_ROM_HIBASEADDR  0xFFFFF
#define MAX_EXT_MEM_SIZE  (MAX_MEM_SIZE - MEBIBYTE - SYS_ROM_SIZE)

class Memory;
extern Memory g_memory;

typedef std::function<void(
		uint32_t,  // address
		uint8_t,   // 0=read, 1=write
		uint16_t,  // value read or written
		uint8_t    // data lenght (1=byte, 2=word)
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

class Memory
{
protected:

	//TODO change these C arrays to std::vector
	uint8_t *m_buffer;
	uint m_mainbuf_size;
	uint8_t *m_sysrom;
	uint m_base_size;
	uint m_ext_size;

	struct {
		bool A20_enabled;
		uint32_t mask;
	} m_s;

	memtrap_intervalTree_t m_traps_tree;
	std::vector<memtrap_interval_t> m_traps_intervals;

	uint8_t read(uint32_t _address) const noexcept;
	void write(uint32_t _address, uint8_t value) noexcept;

	void load_rom_set(const std::string &_filename);

public:

	Memory();
	~Memory();

	void init();
	void reset();
	void config_changed();

	uint8_t read_byte(uint32_t _address) const noexcept;
	uint16_t read_word(uint32_t _address) const noexcept;
	uint32_t read_dword(uint32_t _address) const noexcept;
	uint64_t read_qword(uint32_t _address) const noexcept;

	uint8_t read_byte_notraps(uint32_t _address) const noexcept;
	uint16_t read_word_notraps(uint32_t _address) const noexcept;
	uint32_t read_dword_notraps(uint32_t _address) const noexcept;
	uint64_t read_qword_notraps(uint32_t _address) const noexcept;

	void write_byte(uint32_t _address, uint8_t value) noexcept;
	void write_word(uint32_t _address, uint16_t value) noexcept;
	void write_dword(uint32_t _address, uint32_t value) noexcept;

	void set_A20_line(bool _enabled);
	bool get_A20_line() { return m_s.A20_enabled; }

	uint8_t *get_phy_ptr(uint32_t _address);

	void DMA_read(uint32_t addr, uint16_t len, uint8_t *buf);
	void DMA_write(uint32_t addr, uint16_t len, uint8_t *buf);

	void load(uint32_t _address, const std::string &_filename);
	void dump(const std::string &_filename, uint32_t _address, uint _len);

	void register_trap(uint32_t _lo, uint32_t _hi, uint _mask, memtrap_fun_t _fn);

	uint32_t get_ram_size() { return m_mainbuf_size; }

	void save_state(StateBuf &);
	void restore_state(StateBuf &);

	static void s_debug_trap(uint32_t _address, uint8_t _rw, uint16_t _value, uint8_t _len);
	static void s_debug_40h_trap(uint32_t _address, uint8_t _rw, uint16_t _value, uint8_t _len);
};


#endif
