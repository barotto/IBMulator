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

#include "ibmulator.h"
#include "fatreader.h"
#include "filesys.h"
#include "utils.h"


void FATReader::read(std::string _filepath)
{
	auto infile = FileSys::make_file(FileSys::to_native(_filepath).c_str(), "rb");
	if(!infile) {
		throw std::runtime_error("Cannot access the image file");
	}

	m_boot.read(infile.get());
	read_root_dir(infile.get());
}

std::string FATReader::get_volume_id() const
{
	for(auto &entry : m_root) {
		if((entry.Attr & ATTR_VOLUME_ID) && !(entry.Attr & ATTR_SYSTEM)) {
			return entry.get_name_str() + entry.get_ext_str();
		}
	}
	return std::string(8+3,' ');
}

bool FATReader::DIREntry::is_file() const
{
	return (!is_empty() && !(Attr & ATTR_VOLUME_ID) && !(Attr & ATTR_DIRECTORY));
}

bool FATReader::DIREntry::is_directory() const
{
	return (!is_empty() && !(Attr & ATTR_VOLUME_ID) && (Attr & ATTR_DIRECTORY));
}

void FATReader::BootSector::read(FILE *_infile)
{
	// Jump instruction.
	if(fread(jump_inst, 3, 1, _infile) != 1) {
		throw std::runtime_error("Cannot read from file");
	}
	
	// OEM Name
	fseek(_infile, 0x03, SEEK_SET);
	if(fread(oem_name, 8, 1, _infile) != 1) {
		throw std::runtime_error("Cannot read from file");
	}
	
	// BPB
	fseek(_infile, 0x0b, SEEK_SET);
	if(fread(&bios_params, sizeof(BPB), 1, _infile) != 1) {
		throw std::runtime_error("Cannot read the BPB");
	}

	// Derived Values
	if(bios_params.tot_sectors != 0) {
		tot_sectors = bios_params.tot_sectors;
	} else {
		tot_sectors = bios_params.tot_sectors_32;
	}
	if(!tot_sectors) {
		throw std::runtime_error("Not a valid FAT volume");
	}
	bytes_per_cluster = bios_params.bps * bios_params.spc;
	tot_bytes = bios_params.bps * tot_sectors;
	if(!bios_params.bps) {
		throw std::runtime_error("Not a valid FAT volume");
	}
	root_dir_sec = ((bios_params.max_entries * 32) + (bios_params.bps - 1)) / bios_params.bps;
	data_sec_cnt = tot_sectors - (bios_params.reserved_sec + (bios_params.num_fats * bios_params.spfat) + root_dir_sec);
	if(!bios_params.spc) {
		throw std::runtime_error("Not a valid FAT volume");
	}
	clusters_cnt = data_sec_cnt / bios_params.spc;
	first_data_sec = bios_params.reserved_sec + (bios_params.num_fats * bios_params.spfat) + root_dir_sec;

	// Some checks to determine if the read data can be trusted:
	// 1. is the jump instruction at the start of the boot block valid?
	bool goodjmp = (jump_inst[0] == 0xEB && jump_inst[2] == 0x90); // short jump + NOP
	goodjmp = goodjmp || (jump_inst[0] == 0xE9); // near jump
	goodjmp = goodjmp || (jump_inst[0] == 0x69); // jump
	// 2. is the high order nibble of the BPB's media descriptor byte 0xF?
	bool goodmedia = (bios_params.media & 0xF0) == 0xF0;
	// 3. is the sector size in the BPB 512?
	bool goodsec = bios_params.bps == 512;
	// 4. is the cluster size in the BPB a power of 2?
	bool goodclust = bios_params.spc <= 128 && is_power_of_2(bios_params.spc);
	
	if(!goodjmp || !goodmedia || !goodsec || !goodclust) {
		throw std::runtime_error("Unknown media type");
	}
	
	// Microsoft and IBM operating systems determine the type of FAT file system
	// used on a volume solely by the number of clusters, not by the used BPB
	// format or the indicated file system type, so we do the same.
	if(clusters_cnt < 4085) {
		fat_type = 12;
	} else if(clusters_cnt < 65525) {
		fat_type = 16;
	} else {
		fat_type = 32;
	}
	
	if(fat_type != 32) {
		fseek(_infile, 0x24, SEEK_SET);
		if(fread(&ext_bios_params, sizeof(EBPB), 1, _infile) != 1)  {
			throw std::runtime_error("Cannot read the EBPB");
		}
	} else {
		throw std::runtime_error("FAT32 not supported");
	}
}

const char * FATReader::BootSector::get_media_str() const
{
	switch(bios_params.media) {
		case 0xF0: if(bios_params.sptrk > 18) {
			return "3.5\" DS 80 tps 36 spt (2.88MB)";
		} else {
			return "3.5\" DS 80 tps 18 spt (1.44MB)";
		}
		case 0xF8: return "Fixed disk";
		case 0xF9: if(bios_params.sptrk > 9) {
			return "5.25\" DS 80 tps 15 spt (1.2MB)";
		} else {
			return "3.5\" DS 80 tps 9 spt (720K)";
		}
		case 0xFA: return "5.25\" SS 80 tps 8 spt (320K)";
		case 0xFB: return "3.5\" DS 80 tps 8 spt (640K)";
		case 0xFC: return "5.25\" SS 40 tps 9 spt (180K)";
		case 0xFD: return "5.25\"/8\" DS 40 tps 9 spt (360K)";
		case 0xFE: return "5.25\"/8\" SS 40 tps 8 spt (160K)";
		case 0xFF: return "5.25\" DS 40 tps 8 spt (320K)";
		default:
			throw std::runtime_error("Unknown media type");
	}
}

std::string FATReader::BootSector::get_vol_label_str() const
{
	if(ext_bios_params.boot_sig == 0x29) {
		return FATReader::get_printable_str(ext_bios_params.vol_label,11,u8"▯");
	}
	return std::string(11,' ');
}

std::string FATReader::BootSector::get_fs_type_str() const
{
	if(ext_bios_params.boot_sig == 0x29) {
		return FATReader::get_printable_str(ext_bios_params.fs_type,8,u8"▯");
	}
	return std::string(8,' ');
}

std::string FATReader::BootSector::get_oem_str() const
{
	return FATReader::get_printable_str(oem_name,8,u8"▯");
}

void FATReader::BootSector::seek_sector(int secnum, FILE *infile) const
{
	int byte_offset = secnum * bios_params.bps;
	fseek(infile, byte_offset, SEEK_SET);
}

void FATReader::DIREntry::get_time(uint16_t _time, int &sec_, int &min_, int &hour_)
{
	// Bits 0-4: 2-second count, valid value range 0-29 inclusive (0 - 58 seconds).
	// Bits 5-10: Minutes, valid value range 0-59 inclusive.
	// Bits 11-15: Hours, valid value range 0-23 inclusive.
	sec_  = (_time & 0x1f)*2;
	min_  = (_time & 0x7e0) >> 5;
	hour_ = (_time & 0xf800) >> 11;
}

void FATReader::DIREntry::get_date(uint16_t _date, int &day_, int &month_, int &year_)
{
	// Bits 0-4: Day of month, valid value range 1-31 inclusive.
	// Bits 5-8: Month of year, 1 = January, valid value range 1-12 inclusive.
	// Bits 9-15: Count of years from 1980, valid value range 0-127 inclusive (1980–2107).
	day_   = _date & 0x1f;
	month_ = (_date & 0x1e0) >> 5;
	year_  = ((_date & 0xfe00) >> 9) + 1980;
}

time_t FATReader::DIREntry::get_time_t(uint16_t _date, uint16_t _time)
{
	struct tm timeinfo;
	get_date(_date, timeinfo.tm_mday, timeinfo.tm_mon, timeinfo.tm_year);
	get_time(_time, timeinfo.tm_sec, timeinfo.tm_min, timeinfo.tm_hour);
	timeinfo.tm_year -= 1900;
	timeinfo.tm_mon -= 1;
	return mktime(&timeinfo);
}

void FATReader::read_root_dir(FILE *infile)
{
	assert(m_boot.fat_type != 32);

	int first_root_dir_secnum = m_boot.bios_params.reserved_sec +
			(m_boot.bios_params.num_fats * m_boot.bios_params.spfat);
	m_boot.seek_sector(first_root_dir_secnum, infile);
	m_root.resize(m_boot.bios_params.max_entries);

	int entrynum = 0;
	do {
		if(fread(&m_root[entrynum], sizeof(DIREntry), 1, infile) != 1) {
			throw std::runtime_error("Cannot read directory entry");
		}
	} while(m_root[entrynum++].Name[0] != 0x00 && 
	        entrynum < m_boot.bios_params.max_entries);
	
	m_root.resize(entrynum);
}

std::string FATReader::get_printable_str(const uint8_t *_data, unsigned _size, const std::string &_replacement)
{
	std::string printable;
	for(unsigned i=0; i<_size; i++) {
		if(_data[i] >= 0x20 && _data[i] <= 0x7E) {
			printable += char(_data[i]);
		} else {
			printable += _replacement;
		}
	}
	return printable;
}

std::string FATReader::DIREntry::get_name_str() const
{
	if(is_empty()) {
		return std::string();
	}
	// DOS file names are encoded in the currently active OEM charset.
	// cannot convert to UTF8 without knowing the loaded code page.
	return (FATReader::get_printable_str(Name, sizeof(Name), u8"▯"));
}

std::string FATReader::DIREntry::get_ext_str() const
{
	if(is_empty()) {
		return std::string();
	}
	// DOS file names are encoded in the currently active OEM charset.
	// cannot convert to UTF8 without knowing the loaded code page.
	return (FATReader::get_printable_str(Ext, sizeof(Ext), u8"▯"));
}

std::string FATReader::DIREntry::get_fullname_str(const char *_dot) const
{
	if(is_empty()) {
		return std::string();
	}
	// DOS file names are encoded in the currently active OEM charset.
	// cannot convert to UTF8 without knowing the loaded code page.
	std::string name = str_trim(get_name_str());
	std::string ext = str_trim(get_ext_str());
	if(ext.empty()) {
		return name;
	}
	return name + _dot + ext;
}


