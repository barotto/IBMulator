// license:BSD-3-Clause
// copyright-holders:Olivier Galibert, Marco Bortolin

// Based on MAME's "lib/formats/imd_dsk.cpp"

#include "ibmulator.h"
#include "floppydisk.h"
#include "floppyfmt_imd.h"
#include "filesys.h"
#include "utils.h"
#include <cstring>

FloppyDisk::Properties FloppyFmt_IMD::identify(std::string _file_path,
		uint64_t _file_size, FloppyDisk::Size _disk_size)
{
	UNUSED(_file_size);
	UNUSED(_disk_size);

	std::ifstream fstream = FileSys::make_ifstream(_file_path.c_str(), std::ios::binary);
	if(!fstream.is_open()){
		PWARNF(LOG_V1, LOG_FDC, "IMD: cannot open: '%s'\n", _file_path.c_str());
		return {0};
	}

	char h[4];
	fstream.read(&h[0], 4);
	if(!fstream.good()) {
		PWARNF(LOG_V1, LOG_FDC, "IMD: cannot read: '%s'\n", _file_path.c_str());
		return {0};
	}

	if(::memcmp(h, "IMD ", 4) != 0) {
		PWARNF(LOG_V1, LOG_FDC, "IMD: invalid format: '%s'\n", _file_path.c_str());
		return {0};
	}

	do {
		m_header.push_back(fstream.get());
		if(!fstream.good()) {
			return {0};
		}
	} while(m_header.back() != 0x1a);
	m_header.pop_back();

	m_load_offset = fstream.tellg();

	// IMD's format authors forgot to add the disk geometry into the file header.
	// We have to walk the whole file to find out the floppy disk variant!
	uint8_t maxtrack = 0;
	uint8_t maxhead = 0;
	unsigned maxdrate = 0;
	unsigned maxdata = 0;

	while(fstream.good())
	{
		TrackInfo t;
		fstream.read(reinterpret_cast<char*>(&t), sizeof(TrackInfo));
		if(fstream.fail()) {
			PWARNF(LOG_V1, LOG_FDC, "IMD: error reading TrackInfo: '%s'\n", _file_path.c_str());
			return {0};
		}
		if(t.has_secsize_table()) {
			PWARNF(LOG_V1, LOG_FDC, "IMD: variable sector size not supported: '%s'\n", _file_path.c_str());
			return {0};
		}
		if(t.cyl > 84) {
			PWARNF(LOG_V1, LOG_FDC, "IMD: number of tracks exceeds maximum: %s\n", _file_path.c_str());
			return {0};
		}
		if(t.cyl > maxtrack) {
			maxtrack = t.cyl;
		}
		auto head = (t.head & 0x3f);
		if(head  > 1) {
			PWARNF(LOG_V1, LOG_FDC, "IMD: number of heads exceeds maximum: %s\n", _file_path.c_str());
			return {0};
		}
		if(head > maxhead) {
			maxhead = head;
		}
		if(t.get_rate_kbps() > maxdrate) {
			maxdrate = t.get_rate_kbps();
		}
		unsigned trkdata = t.spt * t.get_actual_secsize();
		if(trkdata > maxdata) {
			maxdata = trkdata;
		}
		unsigned skip_bytes = t.spt; // skip sector numbering map
		if(t.has_cyl_map()) {
			skip_bytes += t.spt;
		}
		if(t.has_head_map()) {
			skip_bytes += t.spt;
		}

		// move to the sector data records
		fstream.seekg(skip_bytes, std::ifstream::cur);
		if(fstream.fail()) {
			PWARNF(LOG_V1, LOG_FDC, "IMD: error accessing sector data: '%s'\n", _file_path.c_str());
			return {0};
		}

		for(int i=0; i<t.spt; i++) {
			uint8_t stype = fstream.get();
			if(stype == 0 || stype > 8) {
				// sector not available
			} else {
				if(stype == 2  // Compressed: All bytes in sector have same value (xx)
				|| stype == 4  // Compressed  with "Deleted-Data address mark"
				|| stype == 6  // Compressed  read with data error
				|| stype == 8) // Compressed, Deleted read with data error
				{
					fstream.seekg(1, std::ifstream::cur);
				}
				else
				{
					fstream.seekg(t.get_actual_secsize(), std::ifstream::cur);
				}
				if(fstream.fail()) {
					PWARNF(LOG_V1, LOG_FDC, "IMD: error reading sector data: '%s'\n", _file_path.c_str());
					return {0};
				}
			}
		}
		fstream.peek(); // sets the eof bit
	}
	if(!fstream.eof()) {
		// this should be an assert
		PWARNF(LOG_V1, LOG_FDC, "IMD: bigger than expected: '%s'\n", _file_path.c_str());
		return {0};
	}

	m_geom.tracks = maxtrack + 1;
	m_geom.sides = maxhead + 1;

	const char *sides = m_geom.sides == 1 ? "SS" : "DS";
	switch(maxdrate) {
		case 250:
			if(m_geom.tracks <= 42) {
				m_geom.type = FloppyDisk::SIZE_5_25 | FloppyDisk::DENS_DD;
				m_geom.desc = str_format("5.25\" %sDD", sides);
			} else {
				// 5.25 QD cannot be distinguished from 3.5 DD
				// both 80 trk, 9 spt, 250 kbit
				// choose the most popular format
				m_geom.type = FloppyDisk::SIZE_3_5 | FloppyDisk::DENS_DD;
				m_geom.desc = str_format("3.5\" %sDD", sides);
			}
			break;
		case 300:
			// DD disks read with 5.25" HD 360rpm drives
			m_geom.type = FloppyDisk::SIZE_5_25;
			if(m_geom.tracks > 42) {
				m_geom.type |= FloppyDisk::DENS_QD;
				m_geom.desc = str_format("5.25\" %sQD", sides);
			} else {
				m_geom.type |= FloppyDisk::DENS_DD;
				m_geom.desc = str_format("5.25\" %sDD", sides);
			}
			break;
		case 500:
			m_geom.type = FloppyDisk::DENS_HD;
			if(maxdata > 7680) {
				m_geom.type |= FloppyDisk::SIZE_3_5;
				m_geom.desc = str_format("3.5\" %sHD", sides);
			} else {
				m_geom.type |= FloppyDisk::SIZE_5_25;
				m_geom.desc = str_format("5.25\" %sHD", sides);
			}
			break;
		default:
			PWARNF(LOG_V1, LOG_FDC, "IMD: invalid data rate: '%s'\n", _file_path.c_str());
			return {0};
	}

	PDEBUGF(LOG_V2, LOG_FDC, "IMD: t=%u,h=%u,maxrate=%u,maxdata=%u: %s\n",
			m_geom.tracks, m_geom.sides, maxdrate, maxdata, _file_path.c_str() );

	return m_geom;
}

std::string FloppyFmt_IMD::get_preview_string(std::string _filepath)
{
	auto props = identify(_filepath, 0, FloppyDisk::SIZE_8);
	if(!props.type) {
		return "Unknown or unsupported file type";
	}
	std::string info = "Format: ImageDisk IMD File<br />";
	info += "Media: " + str_to_html(m_geom.desc) + str_format(" %u tracks", m_geom.tracks) + "<br />";
	info += "ImageDisk version: " + str_format("%.*s", 4, &m_header[0]) + "<br />";
	info += "Date: " + str_format("%.*s", 19, &m_header[6]) + "<br />";
	if(m_header.size() > 25) {
		info += "Comments: <br />";
		info += str_to_html(str_format("%.*s", m_header.size()-25, &m_header[25]));
	}
	return info;
}

bool FloppyFmt_IMD::load(std::ifstream &_file, FloppyDisk &_disk)
{
	PINFOF(LOG_V1, LOG_FDC, "Reading IMD file ...\n");

	// identify() must be called before load(), on the same file path
	if(!m_geom.type) {
		PERRF(LOG_FDC, "Call identify() first!\n");
		assert(false);
		return false;
	}

	// format shouldn't exceed disk geometry
	int img_tracks, img_heads;
	_disk.get_maximal_geometry(img_tracks, img_heads);

	if(m_geom.sides > img_heads || m_geom.tracks > img_tracks) {
		PERRF(LOG_FDC, "IMD: Invalid disk geometry\n");
		return false;
	}

	_file.seekg(m_load_offset, std::ifstream::beg);
	if(!_file.good()) {
		PWARNF(LOG_V1, LOG_FDC, "IMD: error accessing the file!\n");
		return {0};
	}

	try {
		return load_raw(_file, dynamic_cast<FloppyDisk_Raw&>(_disk));
	} catch(std::bad_cast &) {
		return load_flux(_file, _disk);
	}
}

bool FloppyFmt_IMD::load_raw(std::ifstream &, FloppyDisk_Raw &)
{
	// TODO?
	PERRF(LOG_FDC, "IMD: raw-sector disk emulation is not supported\n");
	return false;
}

bool FloppyFmt_IMD::load_flux(std::ifstream &_file, FloppyDisk &_disk)
{
	std::vector<uint8_t> snum;
	std::vector<uint8_t> tnum;
	std::vector<uint8_t> hnum;
	std::vector<std::vector<uint8_t>> data;

	// this is the second file walk.
	// stream's good bit won't be checked, checks alredy done in the identify()
	for(unsigned j=0; j<m_geom.tracks*m_geom.sides; j++) {
		TrackInfo t;
		_file.read(reinterpret_cast<char*>(&t), sizeof(TrackInfo));

		bool fm = t.mode < 3;
		int rate = t.get_rate_kbps() * 1000;
		int rpm = ((m_geom.type & FloppyDisk::SIZE_5_25) && rate >= 300000) ? 360 : 300;
		int cell_count = (fm ? 1 : 2) * rate * 60/rpm;

		snum.resize(t.spt);
		_file.read(reinterpret_cast<char*>(&snum[0]), t.spt);

		if(t.has_cyl_map()) {
			tnum.resize(t.spt);
			_file.read(reinterpret_cast<char*>(&tnum[0]), t.spt);
		}

		if(t.has_head_map()) {
			hnum.resize(t.spt);
			_file.read(reinterpret_cast<char*>(&hnum[0]), t.spt);
		}

		uint8_t chead = t.head & 0x3f;

		int gap_3 = calc_default_pc_gap3_size(m_geom.type & FloppyDisk::SIZE_MASK,
				t.get_actual_secsize());

		desc_pc_sector sects[256];

		data.resize(t.spt);

		PDEBUGF(LOG_V2, LOG_FDC, "IMD: %u: cyl=%u, head=%u, spt=%u, ssize=%u\n",
				j, t.cyl, t.head, t.spt, t.get_actual_secsize());

		for(unsigned i=0; i<t.spt; i++) {
			uint8_t stype = _file.get();

			sects[i].track       = t.has_cyl_map() ? tnum[i] : t.cyl;
			sects[i].head        = t.has_head_map() ? hnum[i] : chead;
			sects[i].sector      = snum[i];
			sects[i].size        = t.secsize;
			sects[i].actual_size = t.get_actual_secsize();

			// sector types:
			// 00      Sector data unavailable - could not be read
			// 01 .... Normal data: (Sector Size) bytes follow
			// 02 xx   Compressed: All bytes in sector have same value (xx)
			// 03 .... Normal data with "Deleted-Data address mark"
			// 04 xx   Compressed  with "Deleted-Data address mark"
			// 05 .... Normal data read with data error
			// 06 xx   Compressed  read with data error
			// 07 .... Deleted data read with data error
			// 08 xx   Compressed, Deleted read with data error
			if(stype == 0 || stype > 8) {
				sects[i].data = nullptr;
			} else {
				sects[i].deleted = stype == 3 || stype == 4 || stype == 7 || stype == 8;
				sects[i].bad_crc = stype == 5 || stype == 6 || stype == 7 || stype == 8;

				data[i].resize(t.get_actual_secsize());
				sects[i].data = &data[i][0];

				if(stype == 2 || stype == 4 || stype == 6 || stype == 8) {
					int val = _file.get();
					memset(sects[i].data, val, t.get_actual_secsize());
				} else {
					_file.read(reinterpret_cast<char*>(sects[i].data), t.get_actual_secsize());
				}
			}
			PDEBUGF(LOG_V2, LOG_FDC, "IMD:   %u: CHS=%u/%u/%u, size=%u(%u), del=%u, bad=%u\n",
					i,
					sects[i].track, sects[i].head, sects[i].sector, sects[i].size,
					sects[i].actual_size, sects[i].deleted, sects[i].bad_crc);
		}

		if(t.spt) {
			if(fm) {
				build_pc_track_fm(t.cyl, chead, _disk, cell_count, t.spt, sects, gap_3);
			} else {
				build_pc_track_mfm(t.cyl, chead, _disk, cell_count, t.spt, sects, gap_3);
			}
		}
	}

	return true;
}

