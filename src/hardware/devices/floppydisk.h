/*
 * Copyright (C) 2015-2022  Marco Bortolin
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

#ifndef IBMULATOR_HW_FLOPPYDISK_H
#define IBMULATOR_HW_FLOPPYDISK_H

#include <string>

enum FloppyDiskType {
	FLOPPY_NONE, // media not present
	FLOPPY_160K, // 160K  5.25"
	FLOPPY_180K, // 180K  5.25"
	FLOPPY_320K, // 320K  5.25"
	FLOPPY_360K, // 360K  5.25"
	FLOPPY_720K, // 720K  3.5"
	FLOPPY_1_2,  // 1.2M  5.25"
	FLOPPY_1_44, // 1.44M 3.5"
	FLOPPY_2_88, // 2.88M 3.5"
	FLOPPY_TYPE_CNT
};

enum FloppyDiskSize {
	FLOPPY_160K_BYTES = 160*1024,  // 160K  5.25"
	FLOPPY_180K_BYTES = 180*1024,  // 180K  5.25"
	FLOPPY_320K_BYTES = 320*1024,  // 320K  5.25"
	FLOPPY_360K_BYTES = 360*1024,  // 360K  5.25"
	FLOPPY_720K_BYTES = 720*1024,  // 720K  3.5"
	FLOPPY_1_2_BYTES  = 1200*1024, // 1.2M  5.25"
	FLOPPY_1_44_BYTES = 1440*1024, // 1.44M 3.5"
	FLOPPY_2_88_BYTES = 2880*1024  // 2.88M 3.5"
};

class FloppyDisk
{
public:
	std::string path;      // the image file
	int      fd      = -1; // file descriptor of the image file
	unsigned spt     = 0;  // number of sectors per track
	unsigned sectors = 0;  // number of formatted sectors on diskette
	unsigned tracks  = 0;  // number of tracks
	unsigned heads   = 0;  // number of heads
	unsigned type    = FLOPPY_NONE;
	bool     wprot   = false; // write protected?

	struct TypeDef {
		unsigned id;
		uint8_t  trk;
		uint8_t  hd;
		uint8_t  spt;
		unsigned sectors;
		uint8_t  drive_mask;
		const char *str;
	};

	static constexpr TypeDef std_types[FLOPPY_TYPE_CNT] = {
		{ FLOPPY_NONE,  0, 0,  0,    0, 0x00, "none"  },
		{ FLOPPY_160K, 40, 1,  8,  320, 0x03, "160K"  },
		{ FLOPPY_180K, 40, 1,  9,  360, 0x03, "180K"  },
		{ FLOPPY_320K, 40, 2,  8,  640, 0x03, "320K"  },
		{ FLOPPY_360K, 40, 2,  9,  720, 0x03, "360K"  },
		{ FLOPPY_720K, 80, 2,  9, 1440, 0x1f, "720K"  },
		{ FLOPPY_1_2,  80, 2, 15, 2400, 0x02, "1.2M"  },
		{ FLOPPY_1_44, 80, 2, 18, 2880, 0x18, "1.44M" },
		{ FLOPPY_2_88, 80, 2, 36, 5760, 0x10, "2.88M" }
	};

	static const std::map<std::string, unsigned> disk_names_350;
	static const std::map<std::string, unsigned> disk_names_525;

	FloppyDisk() {}

	bool open(unsigned _type, const char *_path, bool _write_prot);
	void close();

	void read_sector(uint32_t _from_offset, uint8_t *_to_buffer, uint32_t _bytes);
	void write_sector(uint32_t _to_offset, const uint8_t *_from_buffer, uint32_t _bytes);
};


#endif
