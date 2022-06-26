// license:BSD-3-Clause
// copyright-holders:Miodrag Milanovic, Marco Bortolin

// Based on MAME's "lib/formats/td0_dsk.cpp"


#include "ibmulator.h"
#include "floppydisk.h"
#include "floppyfmt_td0.h"
#include "filesys.h"
#include "utils.h"
#include <cstring>


FloppyDisk::Properties FloppyFmt_TD0::identify(std::string _file_path, uint64_t _file_size,
		FloppyDisk::Size _disk_size)
{
	UNUSED(_file_size);
	UNUSED(_disk_size);

	std::ifstream fstream = FileSys::make_ifstream(_file_path.c_str(), std::ios::binary);
	if(!fstream.is_open()){
		PWARNF(LOG_V1, LOG_FDC, "TD0: cannot open: '%s'\n", _file_path.c_str());
		return {0};
	}

	fstream.read(reinterpret_cast<char*>(&m_header), sizeof(Header));
	if(fstream.bad()) {
		PWARNF(LOG_V1, LOG_FDC, "TD0: cannot read: '%s'\n", _file_path.c_str());
		return {0};
	}
	if(::memcmp(&m_header.sig, "TD", 2) == 0) {
		m_adv_comp = false;
	} else if(::memcmp(&m_header.sig, "td", 2) == 0) {
		m_adv_comp = true;
	} else {
		PWARNF(LOG_V1, LOG_FDC, "TD0: not a valid TeleDisk file: '%s'\n", _file_path.c_str());
		return {0};
	}

	switch(m_header.drvtype) {
		// drive type values are ambiguous, the only reliable interpretation is
		// for distinguishing between 3.5/5.25/8 sizes 
		case 0:  // 5.25 96 tpi disk in 48 tpi drive
		case 1:  // 5.25
		case 2:  // 5.25 48-tpi
			m_geom.type = FloppyDisk::SIZE_5_25;
			break;
		case 3:  // 3.5
		case 4:  // 3.5
		case 6:  // 3.5
			m_geom.type = FloppyDisk::SIZE_3_5;
			break;
		case 5:  // 8-inch
			m_geom.type = FloppyDisk::SIZE_8;
			PWARNF(LOG_V1, LOG_FDC, "TD0: 8\" disks not supported: %s\n", _file_path.c_str());
			return {0};
		default:
			PDEBUGF(LOG_V2, LOG_FDC, "TD0: unknown drive type=%u: %s\n", m_header.drvtype, _file_path.c_str());
			break;
	}

	m_geom.sides = m_header.sides > 1 ? 2 : 1;
	// number of tracks is unknown, we need to read and decode the entire file to determine.

	// High bit indicates single-density diskette in early versions
	switch(m_header.drate & 0x7f) {
		case 0:
			// DD read from 3.5 or DD/QD from 5.25 drive
			m_geom.drate = FloppyDisk::DRATE_250;
			m_geom.type |= FloppyDisk::DENS_DD;
			if(m_geom.type & FloppyDisk::SIZE_5_25) {
				// assuming, can change on load for QD disks
				m_geom.tracks = 42;
				m_geom.desc = str_format("5.25\" %sSDD", m_geom.sides == 1 ? "S" : "D");
			} else {
				// single sided 3.5???
				m_geom.desc = str_format("3.5\" %sSDD", m_geom.sides == 1 ? "S" : "D");
				m_geom.tracks = 84;
			}
			break;
		case 1:
			// DD/QD read from 5.25 HD drive
			m_geom.drate = FloppyDisk::DRATE_300;
			m_geom.type |= FloppyDisk::DENS_DD;
			m_geom.tracks = 42;
			m_geom.desc = str_format("5.25\" %sSDD", m_geom.sides == 1 ? "S" : "D");
			break;
		default:
			// HD read from 3.5 or 5.25 drive
			m_geom.drate = FloppyDisk::DRATE_500;
			m_geom.type |= FloppyDisk::DENS_HD;
			m_geom.tracks = 84;
			m_geom.desc = str_format("%s\" %sSHD",
					(m_geom.type & FloppyDisk::SIZE_5_25) ? "5.25" : "3.5",
					m_geom.sides == 1 ? "S" : "D");
			break;
	}

	return m_geom;
}

std::string FloppyFmt_TD0::get_preview_string(std::string _filepath)
{
	auto props = identify(_filepath, 0, FloppyDisk::SIZE_8);
	if(!props.type) {
		return "Unknown or unsupported file type";
	}
	std::string info = "Format: TeleDisk TD0 File<br />";
	info += "Media: " + str_to_html(m_geom.desc) + "<br />";
	info += "TeleDisk version: " + str_format("%u.%u", m_header.ver>>4, m_header.ver&0xf) + "<br />";
	if(m_adv_comp) {
		info += "Compression: LZSS-Huffman<br />";
	} else {
		if(m_header.has_comment_block()) {
			std::ifstream fstream = FileSys::make_ifstream(_filepath.c_str(), std::ios::binary);
			fstream.seekg(sizeof(Header), std::ifstream::beg);
			CommentBlock comblk;
			fstream.read(reinterpret_cast<char*>(&comblk), sizeof(CommentBlock));
			if(fstream.bad()) {
				PWARNF(LOG_V1, LOG_FDC, "TD0: cannot read: '%s'\n", _filepath.c_str());
			} else {
				info += "Date: " + str_format("%u-%02u-%02u %02u:%02u:%02u", 
						1900u + comblk.year, comblk.month, comblk.day,
						comblk.hour, comblk.min, comblk.sec) + "<br />";
				if(comblk.datalen) {
					info += "Comments: <br />";
					unsigned len = std::min(comblk.datalen, uint16_t(1_KB));
					std::vector<char> comments(len);
					fstream.read(reinterpret_cast<char*>(&comments[0]), len);
					if(fstream.good() && comments.back() == 0) {
						unsigned off = 0;
						std::string comment;
						while(off < len) {
							comment = &comments[off];
							off += comment.length() + 1;
							info += str_to_html(comment) + "<br />";
						}
					}
				}
			}
		}
	}
	return info;
}

bool FloppyFmt_TD0::load(std::ifstream &_file, FloppyDisk &_disk)
{
	PINFOF(LOG_V1, LOG_FDC, "Reading TD0 file ...\n");

	// identify() must be called before load(), on the same file path
	if(!m_geom.type) {
		PERRF(LOG_FDC, "Call identify() first!\n");
		assert(false);
		return false;
	}

	// format shouldn't exceed disk geometry
	int img_tracks, img_heads;
	_disk.get_maximal_geometry(img_tracks, img_heads);

	if(m_geom.sides > img_heads) {
		PERRF(LOG_FDC, "TD0: Invalid disk geometry\n");
		return false;
	}

	// set the maximum number of tracks to 84, will trim later if necessary
	if(img_tracks < 84) {
		_disk.resize_tracks(84);
	}

	try {
		return load_raw(_file, dynamic_cast<FloppyDisk_Raw&>(_disk));
	} catch(std::bad_cast &) {
		return load_flux(_file, _disk);
	}
}

bool FloppyFmt_TD0::load_raw(std::ifstream &, FloppyDisk_Raw &)
{
	// TODO?
	PERRF(LOG_FDC, "TD0: raw-sector disk emulation is not supported\n");
	return false;
}

bool FloppyFmt_TD0::load_flux(std::ifstream &_file, FloppyDisk &_disk)
{
	// format shouldn't exceed disk geometry
	int img_tracks, img_heads;
	_disk.get_maximal_geometry(img_tracks, img_heads);

	if(img_tracks != 84) {
		PERRF(LOG_FDC, "TD0: Invalid disk geometry\n");
		return false;
	}

	int track_count = 0;
	int track_spt;
	int offset = 0;
	const int max_size = 4_MB; // 4MB ought to be large enough for any floppy
	std::vector<uint8_t> imagebuf(max_size);

	if(m_adv_comp) {
		td0dsk_t disk_decode(&_file);
		disk_decode.init_Decode();
		disk_decode.set_floppy_file_offset(12);
		disk_decode.Decode(&imagebuf[0], max_size);
	} else {
		_file.seekg(0, std::ifstream::end);
		unsigned size = _file.tellg();
		if(size-12 > max_size) {
			PERRF(LOG_FDC, "TD0: file's too big: %u bytes\n", size);
			return false;
		}
		_file.seekg(12, std::ifstream::beg);
		_file.read(reinterpret_cast<char*>(&imagebuf[0]), size-12);
		if(_file.bad()) {
			PERRF(LOG_FDC, "TD0: cannot read file\n");
			return false;
		}
	}

	if(m_header.has_comment_block()) {
		offset = 10 + imagebuf[2] + (imagebuf[3] << 8);
	}

	track_spt = imagebuf[offset];

	static const int rates[3] = { 500000, 300000, 250000 };
	int rate = rates[m_geom.drate];
	int rpm = ((m_geom.type & FloppyDisk::SIZE_5_25) && rate >= 300000) ? 360 : 300;
	int base_cell_count = rate * 60/rpm;

	while(track_spt != 255)
	{
		// Track Header
		// 0 Number of sectors       (1 byte)
		// 1 Cylinder number         (1 byte)
		// 2 Side/Head number        (1 byte)
		// 3 Cyclic Redundancy Check (1 byte)
		desc_pc_sector sects[256];
		uint8_t sect_data[65536];
		int sdatapos = 0;
		int track = imagebuf[offset + 1];
		if(track >= 84) {
			PERRF(LOG_FDC, "TD0: excessive number of cylinders\n");
			return false;
		}
		int head = imagebuf[offset + 2] & 1;
		bool fm = (m_header.is_single_density()) || (imagebuf[offset + 2] & 0x80);
		offset += 4;
		PDEBUGF(LOG_V2, LOG_FDC, "TD0: cyl=%u, head=%u, spt=%u\n", track, head, track_spt);
		for(int i = 0; i < track_spt; i++)
		{
			// Sector Header
			// 0 Cylinder number         (1 byte)
			// 1 Side/Head               (1 byte)
			// 2 Sector number           (1 byte)
			// 3 Sector size             (1 byte)
			// 4 Flags                   (1 byte)
			// 5 Cyclic Redundancy Check (1 byte)
			// 6 Data block size         (2 bytes) (opt)
			// 8 Encoding method         (1 byte) (opt)
			// Flags:
			//	01 = Sector was duplicated within a track
			//	02 = Sector was read with a CRC error
			//	04 = Sector has a "deleted-data" address mark
			//	10 = Sector data was skipped based on DOS allocation [note]
			//	20 = Sector had an ID field but not data [note]
			//	40 = Sector had data but no ID field (bogus header)
			uint8_t *hs = &imagebuf[offset];
			uint16_t size;
			offset += 6;

			sects[i].track   = hs[0];
			sects[i].head    = hs[1];
			sects[i].sector  = hs[2];
			sects[i].size    = hs[3];
			sects[i].deleted = (hs[4] & 4) == 4;
			sects[i].bad_crc = (hs[4] & 2) == 2;

			if(hs[4] & 0x30)
				size = 0;
			else
			{
				offset += 3;
				size = 128 << hs[3];
				int j, k;
				switch(hs[8])
				{
					default:
						return false;
					case 0: // Raw sector data
						memcpy(&sect_data[sdatapos], &imagebuf[offset], size);
						offset += size;
						break;
					case 1: // Repeated 2-byte pattern
						offset += 4;
						k = (hs[9] + (hs[10] << 8)) * 2;
						k = (k <= size) ? k : size;
						for(j = 0; j < k; j += 2)
						{
							sect_data[sdatapos + j] = hs[11];
							sect_data[sdatapos + j + 1] = hs[12];
						}
						if(k < size)
							memset(&sect_data[sdatapos + k], '\0', size - k);
						break;
					case 2: // Run Length Encoded data
						k = 0;
						while(k < size)
						{
							uint16_t len = imagebuf[offset];
							uint16_t rep = imagebuf[offset + 1];
							offset += 2;
							if(!len)
							{
								memcpy(&sect_data[sdatapos + k], &imagebuf[offset], rep);
								offset += rep;
								k += rep;
							}
							else
							{
								len = (1 << len);
								rep = len * rep;
								rep = ((rep + k) <= size) ? rep : (size - k);
								for(j = 0; j < rep; j += len)
									memcpy(&sect_data[sdatapos + j + k], &imagebuf[offset], len);
								k += rep;
								offset += len;
							}
						}
						break;
				}
			}

			sects[i].actual_size = size;

			PDEBUGF(LOG_V2, LOG_FDC, "TD0:   %u: CHS=%u/%u/%u, size=%u(%u), del=%u, bad=%u\n", i,
					sects[i].track, sects[i].head, sects[i].sector, sects[i].size, sects[i].actual_size,
					sects[i].deleted, sects[i].bad_crc);

			if(size)
			{
				sects[i].data = &sect_data[sdatapos];
				sdatapos += size;
			}
			else
				sects[i].data = nullptr;
		}

		try {
			if(fm) {
				build_pc_track_fm(track, head, _disk, base_cell_count, track_spt, sects,
						calc_default_pc_gap3_size(m_geom.type & FloppyDisk::SIZE_MASK, sects[0].actual_size));
			} else {
				build_pc_track_mfm(track, head, _disk, base_cell_count*2, track_spt, sects,
						calc_default_pc_gap3_size(m_geom.type & FloppyDisk::SIZE_MASK, sects[0].actual_size));
			}
		} catch(std::runtime_error &e) {
			PERRF(LOG_FDC, "TD0: %s\n", e.what());
			return false;
		}
		track_spt = imagebuf[offset];

		track_count = track + 1;
	}

	assert(track_count <= 84);

	m_geom.tracks = track_count;

	if(m_geom.tracks >= 40 && m_geom.tracks <= 42) {
		_disk.resize_tracks(track_count);
	} else if(m_geom.tracks > 42 && (m_geom.type & FloppyDisk::SIZE_5_25) && (m_geom.type & FloppyDisk::DENS_DD)) {
		// Quad Density?
		m_geom.type &= ~FloppyDisk::DENS_DD;
		m_geom.type |= FloppyDisk::DENS_QD;
		_disk.set_type(m_geom.type);
	}

	return true;
}

FloppyFmt_TD0::td0dsk_t::td0dsk_t(std::ifstream *f) :
floppy_file(f)
{
	f->seekg(0, std::ifstream::end);
	floppy_file_size = f->tellg();
}

int FloppyFmt_TD0::td0dsk_t::data_read(uint8_t *buf, uint16_t size)
{
	if(size > floppy_file_size - floppy_file_offset) {
		size = floppy_file_size - floppy_file_offset;
	}
	floppy_file->seekg(floppy_file_offset, std::ifstream::beg);
	floppy_file->read(reinterpret_cast<char*>(buf), size);
	floppy_file_offset += size;
	return size;
}


/*
 * Tables for encoding/decoding upper 6 bits of
 * sliding dictionary pointer
 */

/* decoder table */
static const uint8_t d_code[256] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
	0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
	0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
	0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
	0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
	0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
	0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
	0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
	0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
	0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
	0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
	0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09,
	0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A,
	0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B,
	0x0C, 0x0C, 0x0C, 0x0C, 0x0D, 0x0D, 0x0D, 0x0D,
	0x0E, 0x0E, 0x0E, 0x0E, 0x0F, 0x0F, 0x0F, 0x0F,
	0x10, 0x10, 0x10, 0x10, 0x11, 0x11, 0x11, 0x11,
	0x12, 0x12, 0x12, 0x12, 0x13, 0x13, 0x13, 0x13,
	0x14, 0x14, 0x14, 0x14, 0x15, 0x15, 0x15, 0x15,
	0x16, 0x16, 0x16, 0x16, 0x17, 0x17, 0x17, 0x17,
	0x18, 0x18, 0x19, 0x19, 0x1A, 0x1A, 0x1B, 0x1B,
	0x1C, 0x1C, 0x1D, 0x1D, 0x1E, 0x1E, 0x1F, 0x1F,
	0x20, 0x20, 0x21, 0x21, 0x22, 0x22, 0x23, 0x23,
	0x24, 0x24, 0x25, 0x25, 0x26, 0x26, 0x27, 0x27,
	0x28, 0x28, 0x29, 0x29, 0x2A, 0x2A, 0x2B, 0x2B,
	0x2C, 0x2C, 0x2D, 0x2D, 0x2E, 0x2E, 0x2F, 0x2F,
	0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
	0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
};

static const uint8_t d_len[256] = {
	0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
	0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
	0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
	0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
	0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
	0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
	0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
	0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
	0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
	0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
	0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
	0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
	0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
	0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
	0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
	0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
	0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
	0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
	0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
	0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
	0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
	0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
	0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
	0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
	0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
	0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
	0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
	0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
	0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
	0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
	0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
	0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
};

int FloppyFmt_TD0::td0dsk_t::next_word()
{
	if(tdctl.ibufndx >= tdctl.ibufcnt)
	{
		tdctl.ibufndx = 0;
		tdctl.ibufcnt = data_read(tdctl.inbuf, TD0_BUFSZ);
		if(tdctl.ibufcnt <= 0)
			return(-1);
	}
	while (getlen <= 8) { // typically reads a word at a time
		getbuf |= tdctl.inbuf[tdctl.ibufndx++] << (8 - getlen);
		getlen += 8;
	}
	return(0);
}

int FloppyFmt_TD0::td0dsk_t::GetBit()    /* get one bit */
{
	int16_t i;
	if(next_word() < 0)
		return(-1);
	i = getbuf;
	getbuf <<= 1;
	getlen--;
		if(i < 0)
		return(1);
	else
		return(0);
}

int FloppyFmt_TD0::td0dsk_t::GetByte()    /* get a byte */
{
	uint16_t i;
	if(next_word() != 0)
		return(-1);
	i = getbuf;
	getbuf <<= 8;
	getlen -= 8;
	i = i >> 8;
	return((int) i);
}



/* initialize freq tree */

void FloppyFmt_TD0::td0dsk_t::StartHuff()
{
	int i, j;

	for (i = 0; i < TD0_N_CHAR; i++) {
		freq[i] = 1;
		son[i] = i + TD0_T;
		prnt[i + TD0_T] = i;
	}
	i = 0; j = TD0_N_CHAR;
	while (j <= TD0_R) {
		freq[j] = freq[i] + freq[i + 1];
		son[j] = i;
		prnt[i] = prnt[i + 1] = j;
		i += 2; j++;
	}
	freq[TD0_T] = 0xffff;
	prnt[TD0_R] = 0;
}


/* reconstruct freq tree */

void FloppyFmt_TD0::td0dsk_t::reconst()
{
	int16_t i, j, k;
	uint16_t f, l;

	/* halven cumulative freq for leaf nodes */
	j = 0;
	for (i = 0; i < TD0_T; i++) {
		if (son[i] >= TD0_T) {
			freq[j] = (freq[i] + 1) / 2;
			son[j] = son[i];
			j++;
		}
	}
	/* make a tree : first, connect children nodes */
	for (i = 0, j = TD0_N_CHAR; j < TD0_T; i += 2, j++) {
		k = i + 1;
		f = freq[j] = freq[i] + freq[k];
		for (k = j - 1; f < freq[k]; k--) {};
		k++;
		l = (j - k) * 2;

		/* movmem() is Turbo-C dependent
		   rewritten to memmove() by Kenji */

		/* movmem(&freq[k], &freq[k + 1], l); */
		(void)memmove(&freq[k + 1], &freq[k], l);
		freq[k] = f;
		/* movmem(&son[k], &son[k + 1], l); */
		(void)memmove(&son[k + 1], &son[k], l);
		son[k] = i;
	}
	/* connect parent nodes */
	for (i = 0; i < TD0_T; i++) {
		if ((k = son[i]) >= TD0_T) {
			prnt[k] = i;
		} else {
			prnt[k] = prnt[k + 1] = i;
		}
	}
}


/* update freq tree */

void FloppyFmt_TD0::td0dsk_t::update(int c)
{
	int i, j, k, l;

	if (freq[TD0_R] == TD0_MAX_FREQ) {
		reconst();
	}
	c = prnt[c + TD0_T];
	do {
		k = ++freq[c];

		/* swap nodes to keep the tree freq-ordered */
		if (k > freq[l = c + 1]) {
			while (k > freq[++l]) {};
			l--;
			freq[c] = freq[l];
			freq[l] = k;

			i = son[c];
			prnt[i] = l;
			if (i < TD0_T) prnt[i + 1] = l;

			j = son[l];
			son[l] = i;

			prnt[j] = c;
			if (j < TD0_T) prnt[j + 1] = c;
			son[c] = j;

			c = l;
		}
	} while ((c = prnt[c]) != 0);    /* do it until reaching the root */
}


int16_t FloppyFmt_TD0::td0dsk_t::DecodeChar()
{
	int ret;
	uint16_t c;

	c = son[TD0_R];

	/*
	 * start searching tree from the root to leaves.
	 * choose node #(son[]) if input bit == 0
	 * else choose #(son[]+1) (input bit == 1)
	 */
	while (c < TD0_T) {
		if((ret = GetBit()) < 0)
			return(-1);
		c += (unsigned) ret;
		c = son[c];
	}
	c -= TD0_T;
	update(c);
	return c;
}

int16_t FloppyFmt_TD0::td0dsk_t::DecodePosition()
{
	int16_t bit;
	uint16_t i, j, c;

	/* decode upper 6 bits from given table */
	if((bit=GetByte()) < 0)
		return(-1);
	i = (uint16_t) bit;
	c = (uint16_t)d_code[i] << 6;
	j = d_len[i];

	/* input lower 6 bits directly */
	j -= 2;
	while (j--) {
		if((bit = GetBit()) < 0)
			return(-1);
		i = (i << 1) + bit;
	}
	return(c | (i & 0x3f));
}

/* DeCompression

split out initialization code to init_Decode()

*/

void FloppyFmt_TD0::td0dsk_t::init_Decode()
{
	int i;
	getbuf = 0;
	getlen = 0;
	tdctl.ibufcnt= tdctl.ibufndx = 0; // input buffer is empty
	tdctl.bufcnt = 0;
	StartHuff();
	for (i = 0; i < TD0_N - TD0_F; i++)
		text_buf[i] = ' ';
	tdctl.r = TD0_N - TD0_F;
}


int FloppyFmt_TD0::td0dsk_t::Decode(uint8_t *buf, int len)  /* Decoding/Uncompressing */
{
	int16_t c,pos;
	int  count;  // was an unsigned long, seems unnecessary
	for (count = 0; count < len; ) {
		if(tdctl.bufcnt == 0) {
			if((c = DecodeChar()) < 0)
				return(count); // fatal error
			if (c < 256) {
				*(buf++) = c;
				text_buf[tdctl.r++] = c;
				tdctl.r &= (TD0_N - 1);
				count++;
			}
			else {
				if((pos = DecodePosition()) < 0)
						return(count); // fatal error
				tdctl.bufpos = (tdctl.r - pos - 1) & (TD0_N - 1);
				tdctl.bufcnt = c - 255 + TD0_THRESHOLD;
				tdctl.bufndx = 0;
			}
		}
		else { // still chars from last string
			while( tdctl.bufndx < tdctl.bufcnt && count < len ) {
				c = text_buf[(tdctl.bufpos + tdctl.bufndx) & (TD0_N - 1)];
				*(buf++) = c;
				tdctl.bufndx++;
				text_buf[tdctl.r++] = c;
				tdctl.r &= (TD0_N - 1);
				count++;
			}
			// reset bufcnt after copy string from text_buf[]
			if(tdctl.bufndx >= tdctl.bufcnt)
				tdctl.bufndx = tdctl.bufcnt = 0;
		}
	}
	return(count); // count == len, success
}


