/*
 * Copyright (C) 2017-2026  Marco Bortolin
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

#ifndef IBMULATOR_HW_CF62011BPC_H
#define IBMULATOR_HW_CF62011BPC_H

#include "vga.h"
#include "xga_dispctrl.h"


enum IBMVGA256c_ExtGfxRegisters {
	// 0x30 to 0x36 are also used by the VESA driver for unknown purposes
	IBM_DISP_CTRL = 0x50, // Display Control 1 ?
	IBM_CLOCK_SEL = 0x72, // Clock Frequency Select ?
};

struct IBMVGA256c_ExtGfx
{
	uint8_t index;       // 21xA - Index register
	uint8_t regs[0x7F];

	void    set_register(uint8_t _value) { regs[index & 0x7F] = _value; }
	uint8_t get_register() const { return regs[index & 0x7F]; }

	// DEBUGGING
	operator const char*() { return register_to_string(index); }
	const char * register_to_string(uint8_t _index);
	const char * register_to_string() { return register_to_string(index); }
	const char * register_name(uint8_t _index);
	const char * register_name() { return register_name(index); }
	const std::string & registers_to_string();
};


class CF62011BPC : public VGA
{
	IODEVICE(CF62011BPC, "TI CF62011BPC")
	IODEVICE_ID(CF62011BPC, 0xE8FE)

protected:
	struct {
		XGA_DispCtrl disp_ctrl;
		// Analyzing the behaviour of the IBM VESA TSR drivers, it looks
		// like the IBM VGA256C has its own variant of XGA's Extended Graphics registers,
		// eg. the VESA driver uses index 72h which is not described in IBM's XGA docs,
		IBMVGA256c_ExtGfx ext_gfx;
	} m_s;

public:
	CF62011BPC(Devices *_dev);
	~CF62011BPC();

	void install();
	void remove();
	void reset(unsigned _type);
	uint16_t read(uint16_t _address, unsigned _io_len);
	void write(uint16_t _address, uint16_t _value, unsigned _io_len);

	void save_state(StateBuf &_state);
	void restore_state(StateBuf &_state);

	void state_to_textfile(std::string _filepath);

protected:
	uint32_t default_memsize_kb() const { return 256u; }
	double clock_freq_hz() const override;
	void update_mem_mapping() override;
	void update_rendering_mode() override;

	unsigned draw_gfx_svga8(unsigned _scanline, uint32_t _row_addr_cnt, std::vector<uint8_t> &line_data_);

	template<class T>
	static uint32_t s_mem_read(uint32_t _addr, void *_priv);
	template<class T>
	static void s_mem_write(uint32_t _addr, uint32_t _value, void *_priv);
};


#endif
