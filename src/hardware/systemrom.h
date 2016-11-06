/*
 * Copyright (C) 2016  Marco Bortolin
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

#ifndef IBMULATOR_HW_SYSTEMROM_H
#define IBMULATOR_HW_SYSTEMROM_H

#include "model.h"
#include "devices/hddparams.h"


/* The System ROM is composed of:
 * 0xFF0000 (mirror at 0xF0000) (64K) BIOS
 * 0xFE0000 (mirror at 0xE0000) (64K) VGA BIOS
 * 0xFC0000 (128K) ROM drive
 * 0xF80000 (256K) ROM drive (non US markets)
 *
 * Addresses are for 24-bit address systems (2011, 2121)
 */
class SystemROM
{
private:
	BIOSType m_bios;
	uint8_t *m_data;

public:
	SystemROM();
	~SystemROM();

	void load(const std::string _romset);
	void inject_custom_hdd_params(int _table_entry_id, HDDParams _params);
	const BIOSType & bios() const { return m_bios; }

	inline uint8_t read(uint32_t _phy) const {
		assert(m_data);
		assert((_phy&0xFFFFF) >= 0x80000);
		// TODO this works only for 24-bit address systems
		return m_data[(_phy&0xFFFFF) - 0x80000];
	}

private:
	int load_file(const std::string &_filename, uint32_t _destaddr);
	void load_dir(const std::string &_dirname);
	void load_archive(const std::string &_filename);
};

#endif
