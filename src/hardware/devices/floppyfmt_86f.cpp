// license:BSD-3-Clause
// copyright-holders:Olivier Galibert, Marco Bortolin

// Portions of code from MAME's "lib/formats/86f_dsk.cpp"

#include "ibmulator.h"
#include "floppydisk.h"
#include "floppyfmt_86f.h"
#include "filesys.h"
#include "utils.h"
#include <cstring>



uint16_t FloppyFmt_86F::TrackInfo::get_flags()
{
	return read_16bit_le(&info_data[0]);
}

int FloppyFmt_86F::TrackInfo::get_rpm()
{
	return (get_flags() & 0xe0) ? 360 : 300;
}

FloppyDisk::Encoding FloppyFmt_86F::TrackInfo::get_encoding()
{
	switch((get_flags() >> 3) & 3) {
		case 0: return FloppyDisk::FM;
		case 1: return FloppyDisk::MFM;
		case 2: return FloppyDisk::M2FM;
		case 3: return FloppyDisk::GCR;
		default: assert(false); return FloppyDisk::FM;
	}
}

FloppyDisk::DataRate FloppyFmt_86F::TrackInfo::get_drate()
{
	switch(get_flags() & 7) {
		case 0: return FloppyDisk::DRATE_500;
		case 1: return FloppyDisk::DRATE_300;
		case 2: return FloppyDisk::DRATE_250;
		case 3: return FloppyDisk::DRATE_1000;
		case 4: return FloppyDisk::DRATE_2000;
		default: return FloppyDisk::DRATE_250;
	}
}

int FloppyFmt_86F::TrackInfo::get_drate_kbps()
{
	int kbps = 0;
	switch(get_drate()) {
		case FloppyDisk::DRATE_500: kbps = 500; break;
		case FloppyDisk::DRATE_300: kbps = 300; break;
		case FloppyDisk::DRATE_250: kbps = 250; break;
		case FloppyDisk::DRATE_1000: kbps = 1000; break;
		case FloppyDisk::DRATE_2000: kbps = 2000; break;
		default: break;
	}
	if(get_encoding() == FloppyDisk::FM) {
		kbps /= 2;
	}
	return kbps;
}

int FloppyFmt_86F::TrackInfo::get_count()
{
	if(extra_bc_present) {
		return read_32bit_le(&info_data[2]);
	}
	return 0;
}

int FloppyFmt_86F::TrackInfo::get_index()
{
	if(extra_bc_present) {
		return read_32bit_le(&info_data[6]);
	}
	return read_32bit_le(&info_data[2]);
}

unsigned FloppyFmt_86F::TrackInfo::get_bit_length()
{
	int bc_count = get_count();
	if(extra_bc_present && bc_is_total) {
		return bc_count;
	}

	// Same as 86Box common_get_raw_size()
	double size = 100'000.0;
	size = (size / 250.0) * double(get_drate_kbps());
	size = (size * 300.0) / double(get_rpm());
	size *= rpm_adjust;

	// Round down to a multiple of 16 and add the extra bit cells (86Box)
	int bitcells = ((((uint32_t) size) >> 4) << 4) + bc_count;

	// Floor and add extra bit cells (FluxFox)
	// int bitcells = int(size) + bc_count;

	if(bitcells < 0) {
		return 0;
	}
	return unsigned(bitcells);
}

unsigned FloppyFmt_86F::TrackInfo::get_byte_length()
{
	unsigned bitcells = get_bit_length();
	return (bitcells >> 3) + (bitcells & 7 ? 1 : 0);
}

void FloppyFmt_86F::TrackInfo::read_track_data(std::ifstream &_fstream, std::vector<uint8_t> &_buffer, bool _desc)
{
	unsigned bytes = get_byte_length();
	unsigned offset = data_offset;
	if(_desc) {
		offset += bytes;
	}

	_fstream.seekg(offset, std::ios::beg);
	if(_fstream.bad()) {
		throw std::runtime_error("seek error");
	}

	_buffer.resize(bytes);

	_fstream.read(reinterpret_cast<char*>(&_buffer[0]), bytes);
	if(_fstream.bad()) {
		throw std::runtime_error("cannot read from file");
	}
}

std::string FloppyFmt_86F::TrackInfo::get_info_str()
{
	auto enc = get_encoding();
	int kbps = get_drate_kbps();
	int count = get_count();
	return str_format("Flags: 0x%04X, RPM: %d, Enc:%c%c%c%c, Rate: %d kbps%s, Index: %d, Bitcells: %u, Bytes: %u",
		get_flags(),
		get_rpm(),
		char(enc >> 24), char(enc >> 16), char(enc >> 8), char(enc),
		kbps,
		extra_bc_present ? str_format(", BC count: %s%d", (!bc_is_total && (count > 0)) ? "+" : "", count).c_str() : "",
		get_index(),
		get_bit_length(),
		get_byte_length()
	);
}



FloppyDisk::Properties FloppyFmt_86F::identify(std::string _file_path,
		uint64_t _file_size, FloppyDisk::Size _disk_size)
{
	UNUSED(_file_size);
	UNUSED(_disk_size);

	std::ifstream fstream = FileSys::make_ifstream(_file_path.c_str(), std::ios::binary);
	if(!fstream.is_open()){
		PWARNF(LOG_V1, LOG_FDC, "86F: cannot open: '%s'\n", _file_path.c_str());
		return {0};
	}

	fstream.read(reinterpret_cast<char*>(&m_header), sizeof(m_header));
	if(fstream.bad()) {
		PWARNF(LOG_V1, LOG_FDC, "86F: cannot read: '%s'\n", _file_path.c_str());
		return {0};
	}

	if(::memcmp(m_header.headername, _86F_FORMAT_HEADER, 4) != 0) {
		PWARNF(LOG_V1, LOG_FDC, "86F: invalid format: '%s'\n", _file_path.c_str());
		return {0};
	}

	if(m_header.major_version != _86F_MAJOR_VERSION && m_header.minor_version != _86F_MINOR_VERSION) {
		PWARNF(LOG_V1, LOG_FDC, "86F: unsupported version (v%u.%u): '%s'\n",
				m_header.major_version, m_header.minor_version, _file_path.c_str());
		return {0};
	}

	PDEBUGF(LOG_V1, LOG_FDC, "86F: '%s':\n", _file_path.c_str());

	PDEBUGF(LOG_V2, LOG_FDC, "86F:   Header:\n");
	decode_header();

	m_geom.sides = m_settings.sides;
	if(m_geom.sides != 1 && m_geom.sides != 2) {
		PWARNF(LOG_V1, LOG_FDC, "86F: invalid number of sides: '%s'\n", _file_path.c_str());
		return {0};
	}

	if(m_settings.disk_type == 1) {
		PWARNF(LOG_V1, LOG_FDC, "86F: unsupported zoned RPM: '%s'\n", _file_path.c_str());
		return {0};
	}
	if(m_settings.big_endian) {
		PWARNF(LOG_V1, LOG_FDC, "86F: unsupported big endian: '%s'\n", _file_path.c_str());
		return {0};
	}

	if(read_track_offsets(fstream)) {
		PWARNF(LOG_V1, LOG_FDC, "86F: cannot load tracks data: '%s'\n", _file_path.c_str());
		return {0};
	}

	PDEBUGF(LOG_V2, LOG_FDC, "86F:   Tracks:\n");
	PDEBUGF(LOG_V2, LOG_FDC, "86F:    Track length: %u words\n", get_track_words());

	TrackInfo ti;

	int tracks = 0;
	for(auto toff : m_tracklist) {
		if(!toff) {
			break;
		}
		tracks++;

		try {
			ti = read_track_info(fstream, tracks-1);
		} catch(std::runtime_error &e) {
			PWARNF(LOG_V1, LOG_FDC, "86F: %s: '%s'\n", e.what(), _file_path.c_str());
			break;
		}
		PDEBUGF(LOG_V3, LOG_FDC, "86F:    %d: Offset: 0x%04X (%d), %s\n",
			tracks-1,
			toff, toff,
			ti.get_info_str().c_str()
		);
	}
	PDEBUGF(LOG_V2, LOG_FDC, "86F:    Total tracks: %d\n", tracks);

	m_settings.tracks_count = tracks;

	if(tracks < 1) {
		PWARNF(LOG_V1, LOG_FDC, "86F: unformatted disk: '%s'\n", _file_path.c_str());
		return {0};
	}
	if(tracks == 1) {
		m_settings.image_cylinders = 1;
	} else {
		m_settings.image_cylinders = tracks >> (m_settings.sides-1);
	}
	if(m_settings.hole == 0 && m_settings.image_cylinders > 50 &&
	  (m_settings.sides == 1 || detect_duplicate_odd_tracks(fstream)))
	{
		m_settings.double_step = true;
	}
	PDEBUGF(LOG_V2, LOG_FDC, "86F:    Total cylinders: %d (%d)\n",
		m_settings.image_cylinders,
		m_settings.image_cylinders >> m_settings.double_step
	);
	if(m_settings.image_cylinders < 1) {
		PWARNF(LOG_V1, LOG_FDC, "86F: unformatted disk: '%s'\n", _file_path.c_str());
		return {0};
	}

	try {
		ti = read_track_info(fstream, 2, 0);
	} catch(std::runtime_error &e) {
		PWARNF(LOG_V1, LOG_FDC, "86F: %s: '%s'\n", e.what(), _file_path.c_str());
		return {0};
	}

	m_geom.tracks = (m_settings.image_cylinders >> m_settings.double_step);
	m_geom.sides = m_settings.sides;

	switch(m_header.flags & TYPE_MASK) {
		case TYPE_DD:
			m_geom.type |= FloppyDisk::DENS_DD;
			if(m_geom.tracks <= 50) {
				m_geom.type |= FloppyDisk::SIZE_5_25;
				m_geom.desc = str_format("5.25\" %sDD", m_geom.sides == 1 ? "SS" : "DS");
			} else {
				m_geom.type |= FloppyDisk::SIZE_3_5;
				m_geom.desc = "3.5\" DSDD";
			}
			break;
		case TYPE_HD:
			m_geom.type |= FloppyDisk::DENS_HD;
			if(ti.get_rpm() == 360) {
				m_geom.type |= FloppyDisk::SIZE_5_25;
				m_geom.desc = "5.25\" DSHD";
			} else {
				m_geom.type |= FloppyDisk::SIZE_3_5;
				m_geom.desc = "3.5\" DSHD";
			}
			break;
		case TYPE_ED:
		case TYPE_ED2000:
			m_geom.type |= FloppyDisk::SIZE_3_5 | FloppyDisk::DENS_ED;
			m_geom.desc = "3.5\" DSED";
			break;
		default:
			assert(false);
			return {0};
	}

	PDEBUGF(LOG_V1, LOG_FDC, "86F:   Medium: %s\n", m_geom.desc.c_str());

	m_geom.wprot = m_settings.write_prot;

	return m_geom;
}

bool FloppyFmt_86F::load(std::ifstream &_file, FloppyDisk &_disk)
{
	PINFOF(LOG_V1, LOG_FDC, "Reading 86F file ...\n");

	// identify() must be called before load(), on the same file path
	if(!m_geom.type) {
		PERRF(LOG_FDC, "Call identify() first!\n");
		assert(false);
		return false;
	}

	// format shouldn't exceed disk geometry
	int disk_tracks, disk_heads;
	_disk.get_maximal_geometry(disk_tracks, disk_heads);

	if(m_geom.sides > disk_heads) {
		PERRF(LOG_FDC, "86F: Invalid disk geometry\n");
		return false;
	}

	if(disk_tracks < m_geom.tracks) {
		if(m_geom.tracks - disk_tracks > FloppyFmt::DUMP_THRESHOLD) {
			PERRF(LOG_FDC, "86F: Invalid disk geometry\n");
			return false;
		} else {
			// Some dumps might have excess tracks to be safe,
			// lets be nice and just skip those tracks
			PWARNF(LOG_V0, LOG_FDC,
				"86F: the image has a slight excess of tracks for this disk that will be discarded"
				"(disk tracks=%d, image tracks=%d).\n",
				disk_tracks, m_geom.tracks);
			m_geom.tracks = disk_tracks;
		}
	}

	try {
		return load_raw(_file, dynamic_cast<FloppyDisk_Raw&>(_disk));
	} catch(std::bad_cast &) {
		return load_flux(_file, _disk);
	}
}


MediumInfoData FloppyFmt_86F::get_preview_string(std::string _filepath)
{
	identify(_filepath, 0, FloppyDisk::SIZE_8);
	if(!m_header.headername[0]) {
		std::string err("Not a valid 86F file");
		return { err, err };
	}

	std::string info = str_format("Format: 86Box 86F File v%u.%u\n", m_header.major_version, m_header.minor_version);
	info += "Medium: " + m_geom.desc + "\n";
	info += str_format("Cylinders: %u\n", m_geom.tracks);
	info += str_format("Sides: %u\n", m_geom.sides);
	if(m_geom.wprot) {
		info += "Write protected.\n";
	}
	if(m_settings.surf_desc) {
		info += "Has surface description.\n";
	}

	return { info, str_to_html(info) };
}

void FloppyFmt_86F::decode_header()
{
	const char *hole_str[4] = {
		"DD", "HD", "ED", "ED + 2000"
	};
	const float rpm_val[4] = {
		0.0, -1.0, -1.5, -2.0
	};
	const char *bitcell_str[2] = {
		"No extra bitcells", "Extra bitcells count"
	};
	const char *disk_type_str[2] = {
		"Fixed RPM", "Zoned"
	};
	const char *zone_type_str[4] = {
		"Pre-Apple zoned #1",
		"Pre-Apple zoned #2",
		"Apple zoned",
		"Commodore 64 zoned"
	};
	m_settings.surf_desc = bool(m_header.flags & SURFACE_DESC);
	m_settings.hole = (m_header.flags & TYPE_MASK) >> 1;
	m_settings.sides = bool(m_header.flags & TWO_SIDES) + 1;
	m_settings.write_prot = bool(m_header.flags & WRITE_PROTECT);
	m_settings.disk_type = bool(m_header.flags & ZONED_RPM);
	m_settings.zone_type = (m_header.flags & ZONE_MASK) >> 9;
	m_settings.big_endian = bool(m_header.flags & ENDIAN_BIG);
	m_settings.total_bc_bit = bool(m_header.flags & TOTAL_BC);
	m_settings.bitcell_mode = bool(m_header.flags & EXTRA_BC);
	m_settings.extra_count_is_total = false;
	double rpm_variation = rpm_val[(m_header.flags & RPM_MASK) >> 5];
	if(m_settings.total_bc_bit) {
		if(rpm_variation < .0) {
			rpm_variation = abs(rpm_variation); // positive
		} else if(m_settings.bitcell_mode) {
			m_settings.extra_count_is_total = true;
		}
	}
	m_settings.rpm_adjust = 1.0 + abs(rpm_variation) / 100.0;
	if(rpm_variation > .0) {
		m_settings.rpm_adjust = 1.0 / m_settings.rpm_adjust;
	}

	PDEBUGF(LOG_V2, LOG_FDC, "86F:    Version: %u.%u\n", m_header.major_version, m_header.minor_version);
	PDEBUGF(LOG_V2, LOG_FDC, "86F:    Surface description data: %s\n", m_settings.surf_desc ? "yes" : "no");
	PDEBUGF(LOG_V2, LOG_FDC, "86F:    Hole: %s\n", hole_str[m_settings.hole]);
	PDEBUGF(LOG_V2, LOG_FDC, "86F:    Sides: %d\n", m_settings.sides);
	PDEBUGF(LOG_V2, LOG_FDC, "86F:    Write protect: %s\n", m_settings.write_prot ? "yes" : "no");
	PDEBUGF(LOG_V2, LOG_FDC, "86F:    RPM %s: %.1f%% (adj. %.3f)\n", rpm_variation > .0 ? "speedup" : "slowdown", rpm_variation, m_settings.rpm_adjust);
	PDEBUGF(LOG_V2, LOG_FDC, "86F:    Bitcell mode: %s\n", bitcell_str[m_settings.bitcell_mode]);
	PDEBUGF(LOG_V2, LOG_FDC, "86F:    Disk type: %s\n", disk_type_str[m_settings.disk_type]);
	PDEBUGF(LOG_V2, LOG_FDC, "86F:    Zone type: %s\n", m_settings.disk_type ? zone_type_str[m_settings.zone_type] : "not zoned");
	PDEBUGF(LOG_V2, LOG_FDC, "86F:    Endianness: %s\n", m_settings.big_endian ? "big" : "little");
	PDEBUGF(LOG_V2, LOG_FDC, "86F:    Extra BC count is total: %s\n", m_settings.extra_count_is_total ? "yes" : "no");
}

bool FloppyFmt_86F::detect_duplicate_odd_tracks(std::ifstream &_fstream)
{
	// Unfortunately it's possible for duplicate tracks to exist without all odd tracks being duplicates.
	// But we try detecting the case by reading a couple of pairs hoping for the best, because
	// reading the whole disk would be overkill.

	assert(m_settings.sides == 2);

	TrackInfo track[4];

	try {
		track[0] = read_track_info(_fstream, 0);
		track[1] = read_track_info(_fstream, 2);
		track[2] = read_track_info(_fstream, 20);
		track[3] = read_track_info(_fstream, 22);
	} catch(std::runtime_error &e) {
		PWARNF(LOG_V1, LOG_FDC, "86F: %s\n", e.what());
		return false;
	}
	unsigned len = track[0].get_byte_length();
	if(len != track[1].get_byte_length() || len != track[2].get_byte_length() || len != track[3].get_byte_length()) {
		return false;
	}

	std::vector<uint8_t> databuf1, databuf2;

	try {
		track[0].read_track_data(_fstream, databuf1);
		track[1].read_track_data(_fstream, databuf2);
	} catch(std::runtime_error &e) {
		PWARNF(LOG_V1, LOG_FDC, "86F: %s\n", e.what());
		return false;
	}
	assert(len == databuf1.size() && len == databuf2.size());
	if(std::memcmp(&databuf1[0], &databuf2[0], len) != 0) {
		return false;
	}

	try {
		track[2].read_track_data(_fstream, databuf1);
		track[3].read_track_data(_fstream, databuf2);
	} catch(std::runtime_error &e) {
		PWARNF(LOG_V1, LOG_FDC, "86F: %s\n", e.what());
		return false;
	}
	assert(len == databuf1.size() && len == databuf2.size());
	if(std::memcmp(&databuf1[0], &databuf2[0], len) != 0) {
		return false;
	}

	return true;
}

bool FloppyFmt_86F::load_raw(std::ifstream &, FloppyDisk_Raw &)
{
	PERRF(LOG_FDC, "86F: raw-sector disk emulation is not supported.\n");
	return false;
}

bool FloppyFmt_86F::load_flux(std::ifstream &_file, FloppyDisk &_disk)
{
	// terminology is fckd up:
	// 86F format: track is the data track, so tracks = cylinders * sides
	// FloppyDisk: track is the geometric cylinder

	int disk_cyls, disk_heads;
	_disk.get_maximal_geometry(disk_cyls, disk_heads);

	if(m_geom.sides > disk_heads) {
		PERRF(LOG_FDC, "86F: Invalid disk geometry\n");
		return false;
	}

	// if the interface asks for a full image let's give it that
	int cylstep = 1;
	if(disk_cyls < m_settings.image_cylinders) {
		if(m_settings.double_step && disk_cyls >= m_geom.tracks) {
			cylstep = 2;
		} else {
			PERRF(LOG_FDC, "86F: Invalid disk geometry\n");
			return false;
		}
	}

	std::vector<uint8_t> trackbuf;
	std::vector<uint8_t> weakbuf;

	for(int cylinder = 0; cylinder < m_settings.image_cylinders; cylinder += cylstep) {
		for(int head = 0; head < m_geom.sides; head++) {
			TrackInfo ti;
			try {
				ti = read_track_info(_file, cylinder, head);
			} catch(std::runtime_error &e) {
				PWARNF(LOG_V0, LOG_FDC, "86F: %s\n", e.what());
				return false;
			}

			uint8_t *weak = nullptr;
			try {
				ti.read_track_data(_file, trackbuf);
				if(m_settings.surf_desc) {
					ti.read_track_data(_file, weakbuf);
					weak = &weakbuf[0];
				}
			} catch(std::runtime_error &e) {
				PWARNF(LOG_V0, LOG_FDC, "86F: %s\n", e.what());
				return false;
			}

			generate_track_from_bitstream_with_weak(
				cylinder / cylstep, head,
				&trackbuf[0], weak,
				ti.get_index(), ti.get_bit_length(),
				_disk
			);
		}
	}

	return true;
}

uint32_t FloppyFmt_86F::get_track_words()
{
	uint32_t tracklen = 0;
	if((m_header.flags & (TOTAL_BC | EXTRA_BC | RPM_MASK)) != (TOTAL_BC | EXTRA_BC)) {
		switch(m_header.flags & (RPM_MASK | RPM_FAST | TYPE_MASK)) {
			case TYPE_DD | RPM_2:
			case TYPE_HD | RPM_2:
				tracklen = 12750;
				break;
			case TYPE_DD | RPM_15:
			case TYPE_HD | RPM_15:
				tracklen = 12687;
				break;
			case TYPE_DD | RPM_1:
			case TYPE_HD | RPM_1:
				tracklen = 12625;
				break;
			case TYPE_DD | RPM_0:
			case TYPE_HD | RPM_0:
				tracklen = 12500;
				break;
			case TYPE_DD | RPM_1 | RPM_FAST:
			case TYPE_HD | RPM_1 | RPM_FAST:
				tracklen = 12376;
				break;
			case TYPE_DD | RPM_15 | RPM_FAST:
			case TYPE_HD | RPM_15 | RPM_FAST:
				tracklen = 12315;
				break;
			case TYPE_DD | RPM_2 | RPM_FAST:
			case TYPE_HD | RPM_2 | RPM_FAST:
				tracklen = 12254;
				break;

			case TYPE_ED | RPM_2:
				tracklen = 25500;
				break;
			case TYPE_ED | RPM_15:
				tracklen = 25375;
				break;
			case TYPE_ED | RPM_1:
				tracklen = 25250;
				break;
			case TYPE_ED | RPM_0:
				tracklen = 25000;
				break;
			case TYPE_ED | RPM_1 | RPM_FAST:
				tracklen = 25752;
				break;
			case TYPE_ED | RPM_15 | RPM_FAST:
				tracklen = 24630;
				break;
			case TYPE_ED | RPM_2 | RPM_FAST:
				tracklen = 24509;
				break;
			default:
				break;
		}
	}
	return tracklen;
}

bool FloppyFmt_86F::read_track_offsets(std::ifstream &_fstream)
{
	int tracklistsize = m_header.firsttrackoffs - 8;
	m_tracklist.resize(tracklistsize / 4);

	_fstream.seekg(8, std::ios::beg);
	if(_fstream.bad()) {
		throw std::runtime_error("seek error");
	}

	_fstream.read(reinterpret_cast<char*>(&m_tracklist[0]), tracklistsize);
	return _fstream.bad();
}

FloppyFmt_86F::TrackInfo FloppyFmt_86F::read_track_info(std::ifstream &_fstream, int _track_idx)
{
	uint32_t trackoff = m_tracklist[_track_idx];

	_fstream.seekg(trackoff, std::ios::beg);
	if(_fstream.bad()) {
		throw std::runtime_error(str_format("invalid offset access (byte=%d)", trackoff));
	}

	int info_len = m_settings.bitcell_mode ? 10 : 6;

	TrackInfo ti(
		trackoff + info_len,
		m_settings.bitcell_mode,
		m_settings.extra_count_is_total,
		m_settings.rpm_adjust
	);

	_fstream.read(reinterpret_cast<char*>(&ti.info_data[0]), info_len);
	if(_fstream.bad()) {
		throw std::runtime_error("cannot read track information");
	}

	return ti;
}

FloppyFmt_86F::TrackInfo FloppyFmt_86F::read_track_info(std::ifstream &_fstream, uint8_t _cyl, uint8_t _head)
{
	return read_track_info(_fstream, (_cyl * m_geom.sides) + _head);
}

void FloppyFmt_86F::generate_track_from_bitstream_with_weak(int cyl, int head,
		const uint8_t *trackbuf, const uint8_t *weak, int index_cell, int track_size,
		FloppyDisk &_disk) const
{
	std::vector<uint32_t> &dest = _disk.get_buffer(cyl, head);
	dest.clear();

	int j = 0;

	auto generate = [&](int i) {
		int databit = trackbuf[i >> 3] & (0x80 >> (i & 7));
		int weakbit = weak ? weak[i >> 3] & (0x80 >> (i & 7)) : 0;
		if(weakbit && databit) {
			dest.push_back(FloppyDisk::MG_D | (j*2+1));
		} else if(weakbit && !databit) {
			dest.push_back(FloppyDisk::MG_N | (j*2+1));
		} else if(databit) {
			dest.push_back(FloppyDisk::MG_F | (j*2+1));
		}
	};

	for(int i = index_cell; i != track_size; i++, j++) {
		generate(i);
	}
	for(int i = 0; i != index_cell; i++, j++) {
		generate(i);
	}

	normalize_times(dest, track_size*2);
	_disk.set_write_splice_position(cyl, head, 0);
}
