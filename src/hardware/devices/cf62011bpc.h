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

#ifndef IBMULATOR_HW_CF62011BPC_H
#define IBMULATOR_HW_CF62011BPC_H

#include "vga.h"

class CF62011BPC : public VGA
{
	IODEVICE(CF62011BPC, "TI CF62011BPC")

protected:
	struct {
		uint8_t xga_reg[0xF];
	} m_s;

public:
	CF62011BPC(Devices *_dev);
	~CF62011BPC();

	void install();
	void remove();
	void reset(unsigned _type);
	uint16_t read(uint16_t _address, unsigned _io_len);
	void write(uint16_t _address, uint16_t _value, unsigned _io_len);

protected:
	static uint32_t s_read_byte(uint32_t _addr, void *_priv);
	static void s_write_byte(uint32_t _addr, uint32_t _value, void *_priv);
};

#endif
