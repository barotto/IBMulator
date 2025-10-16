/*
 * Copyright (C) 2021  Marco Bortolin
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

#ifndef IBMULATOR_FATREADER_H
#define IBMULATOR_FATREADER_H


#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>

class FATReader
{
public:
	enum Attributes {
		ATTR_READ_ONLY = 0x01,
		ATTR_HIDDEN    = 0x02,
		ATTR_SYSTEM    = 0x04,
		ATTR_VOLUME_ID = 0x08,
		ATTR_DIRECTORY = 0x10,
		ATTR_ARCHIVE   = 0x20,
		ATTR_LONG_NAME = 0x0f
	};

	struct BootSector {
		uint8_t jump_inst[3] = {0,0,0};
		uint8_t oem_name[8] = {0};

		struct BPB {
			uint16_t bps = 0;            // 0x00B Bytes per logical sector
			uint8_t  spc = 0;            // 0x00D Logical sectors per cluster
			uint16_t reserved_sec = 0;   // 0x00E Reserved sector count
			uint8_t  num_fats = 0;       // 0x010 Number of file allocation tables
			uint16_t max_entries = 0;    // 0x011 Maximum number of root directory entries
			uint16_t tot_sectors = 0;    // 0x013 Total logical sectors
			uint8_t  media = 0;          // 0x015 Media descriptor
			uint16_t spfat = 0;          // 0x016 Sectors per File Allocation Table
			uint16_t sptrk = 0;          // 0x018 Physical Sectors per track
			uint16_t nheads = 0;         // 0x01A Number of heads
			uint32_t hid_sec = 0;        // 0x01C Count of hidden sectors preceding the partition that contains this FAT volume.
			uint32_t tot_sectors_32 = 0; // 0x020 Total logical sectors (if greater than 65535, used if tot_sectors==0)
		} GCC_ATTRIBUTE(packed);

		struct EBPB {
			uint8_t phy_drive = 0;       // 0x024 Physical drive number
			uint8_t rsvd;
			uint8_t boot_sig = 0;        // 0x026 Extended boot signature.
			uint32_t vol_id = 0;         // 0x027 Volume ID (serial number)
			uint8_t vol_label[11] = {0};    // 0x02B Partition Volume Label
			uint8_t fs_type[8] = {0};       // 0x036 File system type
		} GCC_ATTRIBUTE(packed);

		BPB bios_params;
		EBPB ext_bios_params;

		int root_dir_sec = 0;
		int tot_bytes = 0;
		int bytes_per_cluster = 0;
		int fat_type = 0;
		int data_sec_cnt = 0;
		int first_data_sec = 0;
		int clusters_cnt = 0;
		int tot_sectors = 0;

		void read(FILE *);
		std::string get_vol_label_str() const;
		std::string get_fs_type_str() const;
		const char *get_medium_str() const;
		std::string get_oem_str() const;
		int seek_sector(int secnum, FILE *) const;
	};

	struct DIREntry {
		uint8_t  Name[8] = {0};    // Short name
		uint8_t  Ext[3] = {0};     // Extension
		uint8_t  Attr = 0;         // File attributes
		uint8_t  NTRes = 0;        // Reserved for use by Windows NT
		uint8_t  CrtTimeTenth = 0; // Millisecond stamp at file creation time
		uint16_t CrtTime = 0;      // Time file was created
		uint16_t CrtDate = 0;      // Date file was created
		uint16_t LstAccDate = 0;   // Last access date
		uint16_t FstClusHI = 0;    // High word of this entry’s first cluster number
		uint16_t WrtTime = 0;      // Time of last write
		uint16_t WrtDate = 0;      // Date of last write
		uint16_t FstClusLO = 0;    // Low word of this entry’s first cluster number
		uint32_t FileSize = 0;     // File size in bytes

		std::string get_name_str() const;
		std::string get_ext_str() const;
		std::string get_fullname_str(const char *_dot=".") const;
		bool is_empty() const { return Name[0] == 0 || Name[0] == 0xE5; }
		bool is_file() const;
		bool is_directory() const;
		bool is_long_name() const { return (Attr & 0xf) == ATTR_LONG_NAME; }
		static void get_time(uint16_t _time, int &sec_, int &min_, int &hour_);
		static void get_date(uint16_t _date, int &day_, int &month_, int &year_);
		static time_t get_time_t(uint16_t _date, uint16_t _time);
	} GCC_ATTRIBUTE(packed);
	
public:
	void read(std::string _filepath);
	const BootSector &get_boot_sector() const { return m_boot; }
	const std::vector<DIREntry> &get_root_entries() const { return m_root; }
	std::string get_volume_id() const;

protected:
	std::vector<DIREntry> m_root;
	BootSector m_boot;

	void read_dir_entry(DIREntry *_entry);
	void print_dir_entry(DIREntry *_entry);
	void read_root_dir(FILE *infile);
	static std::string get_printable_str(const uint8_t *_data, unsigned _size, const std::string &_replacement);
};

#endif
