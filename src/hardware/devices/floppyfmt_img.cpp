// license:BSD-3-Clause
// copyright-holders:Olivier Galibert, Marco Bortolin

// Based on MAME's lib/formats/upd765_dsk.cpp, lib/formats/pc_dsk.cpp

#include "ibmulator.h"
#include "floppydisk_raw.h"
#include "floppyfmt_img.h"
#include "utils.h"
#include "fatreader.h"
#include "filesys.h"

#include <cstring>

const std::map<unsigned, FloppyFmt_IMG::encoding> FloppyFmt_IMG::ms_encodings = {
//  stdtype               encoding         cs    g4a g1  g2  g3
  { FloppyDisk::DD_160K, {FloppyDisk::MFM, 2000, 80, 50, 22, 80  } }, // 160K 5 1/4 inch double density single sided
  { FloppyDisk::DD_180K, {FloppyDisk::MFM, 2000, 80, 50, 22, 80  } }, // 180K 5 1/4 inch double density single sided
  { FloppyDisk::DD_320K, {FloppyDisk::MFM, 2000, 80, 50, 22, 80  } }, // 320K 5 1/4 inch double density
  { FloppyDisk::DD_360K, {FloppyDisk::MFM, 2000, 80, 50, 22, 80  } }, // 360K 5 1/4 inch double density
//{ FloppyDisk::QD_720K, {FloppyDisk::MFM, 2000, 80, 50, 22, 80  } }, // 720K 5 1/4 inch quad density - gaps unverified
  { FloppyDisk::DD_720K, {FloppyDisk::MFM, 2000, 80, 50, 22, 80  } }, // 720K 3 1/2 inch double density
  { FloppyDisk::HD_1_20, {FloppyDisk::MFM, 1200, 80, 50, 22, 84  } }, // 1200K 5 1/4 inch high density
  { FloppyDisk::HD_1_44, {FloppyDisk::MFM, 1000, 80, 50, 22, 108 } }, // 1440K 3 1/2 inch high density
  { FloppyDisk::HD_1_68, {FloppyDisk::MFM, 1000, 80, 50, 22, 0xc } }, // Microsoft DMF 1680K 3 1/2 inch high density - gaps unverified
  { FloppyDisk::HD_1_72, {FloppyDisk::MFM, 1000, 80, 50, 22, 0xc } }, // Microsoft DMF 1720K 3 1/2 inch high density - gaps unverified
  { FloppyDisk::ED_2_88, {FloppyDisk::MFM,  500, 80, 50, 41, 80  } }, // 2880K 3 1/2 inch extended density - gaps unverified
};


FloppyDisk::Properties FloppyFmt_IMG::identify(std::string file_path,
		uint64_t file_size, FloppyDisk::Size _disk_size)
{
	for(auto const &fmt : FloppyDisk::std_types) {
		if((fmt.first & FloppyDisk::SIZE_MASK) == _disk_size && fmt.second.cap == file_size) {
			m_enc = ms_encodings.at(fmt.first);
			assert(m_enc.type);
			m_geom = FloppyDisk::std_types.at(fmt.first);
			assert(m_geom.type == fmt.first);
			m_imgfile = file_path;
			return m_geom;
		}
	}
	return {FloppyDisk::FD_NONE};
}

MediumInfoData FloppyFmt_IMG::get_preview_string(std::string _filepath)
{
	std::string info_plain = "Format: RAW sector image file\n";

	FATReader fat;
	try {
		fat.read(_filepath);
	} catch(std::runtime_error & err) {
		info_plain += err.what();
		return { info_plain, str_to_html(info_plain) };
	}

	auto &boot_sec = fat.get_boot_sector();

	std::string medium_desc;
	try {
		medium_desc = boot_sec.get_medium_str();
	} catch(std::runtime_error & err) {
		info_plain += err.what();
		return { info_plain, str_to_html(info_plain) };
	}

	auto to_value = [=](std::string s){
		return std::string("<span class=\"value\">") + str_to_html(s,true) + "</span>";
	};

	std::string info_html = str_to_html(info_plain);

	info_plain += "Medium: " + medium_desc + "\n";
	info_html += "Medium: " + str_to_html(medium_desc) + "<br />";

	info_plain += "OEM name: " + boot_sec.get_oem_str();
	info_html += "OEM name: " + to_value(boot_sec.get_oem_str());
	if(boot_sec.oem_name[5]=='I' && boot_sec.oem_name[6]=='H' && boot_sec.oem_name[7]=='C') {
		info_plain += " (mod. by Win95+)";
		info_html += " (mod. by Win95+)";
	}
	info_plain += "\n";
	info_html += "<br />";

	info_plain += "Disk label: " + boot_sec.get_vol_label_str() + "\n";
	info_html += "Disk label: " + to_value(boot_sec.get_vol_label_str()) + "<br />";

	auto root = fat.get_root_entries();
	if(root.empty() || root[0].is_empty()) {
		info_plain += "\nEmpty disk";
		info_html += "<br />Empty disk";
	} else {
		info_plain += "Volume label: " + fat.get_volume_id() + "\n";
		info_html += "Volume label: " + to_value(fat.get_volume_id()) + "<br />";

		info_plain += "Directory\n\n";
		info_html += "Directory<br /><br />";

		info_html += "<table class=\"directory_listing\">";
		for(auto &entry : root) {
			if(entry.is_file() || entry.is_directory()) {
				auto ext = str_to_upper(entry.get_ext_str());
				bool exe = (ext == "BAT" || ext == "COM" || ext == "EXE");

				info_html += std::string("<tr class=\"") + 
						(entry.is_file()?"file":"dir") +
						(exe?" executable":"") +
						"\">";

				info_plain += entry.get_name_str();
				info_html += "<td class=\"name\">" + str_to_html(entry.get_name_str()) + "</td>";

				if(entry.is_file()) {
					info_plain += "." + entry.get_ext_str();
					info_html += "<td class=\"extension\">" + str_to_html(entry.get_ext_str()) + "</td>";

					info_plain += ", " + str_format("%u", entry.FileSize);
					info_html += "<td class=\"size\">" + str_format("%u", entry.FileSize) + "</td>";

					time_t wrtime = entry.get_time_t(entry.WrtDate, entry.WrtTime);
					info_plain += ", " + str_format_time(wrtime, "%x");
					info_html += "<td class=\"date\">" + str_format_time(wrtime, "%x") + "</td>";
				} else {
					info_plain += " <DIR>";
					info_html += "<td class=\"extension\"></td>";
					info_html += "<td class=\"size\">" + str_to_html("<DIR>") + "</td>";
					info_html += "<td class=\"date\"></td>";
				}
				info_plain += "\n";
				info_html += "</tr>";
			}
		}
		info_html += "</table>";
	}

	return { info_plain, info_html };
}

bool FloppyFmt_IMG::load(std::ifstream &_file, FloppyDisk &_disk)
{
	PINFOF(LOG_V1, LOG_FDC, "Reading IMG file (%s) ...\n", m_geom.desc.c_str());

	// identify() must be called before load(), on the same file path
	if(!m_enc.type || !m_geom.type) {
		PERRF(LOG_FDC, "Call identify() first!\n");
		assert(false);
		return false;
	}

	// format shouldn't exceed disk geometry
	int img_tracks, img_heads;
	_disk.get_maximal_geometry(img_tracks, img_heads);
	if(m_geom.tracks > img_tracks || m_geom.sides > img_heads) {
		PERRF(LOG_FDC, "Invalid disk geometry\n");
		return false;
	}

	try {
		return load_raw(_file, dynamic_cast<FloppyDisk_Raw&>(_disk));
	} catch(std::bad_cast &) {
		return load_flux(_file, _disk);
	}
}

bool FloppyFmt_IMG::load_raw(std::ifstream &_file, FloppyDisk_Raw &_disk)
{
	_file.seekg(0, std::ios::beg);

	unsigned track_size = m_geom.spt * m_geom.secsize;
	
	for(int track=0; track < m_geom.tracks; track++) {
		for(int head=0; head < m_geom.sides; head++) {
			_disk.get_buffer(track, head).resize(track_size);
			for(unsigned b=0; b<track_size; b++) {
				_file.read((char*)&_disk.get_buffer(track, head)[b], 1);
				if(_file.fail()) {
					return false;
				}
			}
		}
	}
	return true;
}

bool FloppyFmt_IMG::load_flux(std::ifstream &_file, FloppyDisk &_disk)
{
	int current_size;
	int end_gap_index;

	desc_e *desc = nullptr;

	switch(m_enc.type) {
		case FloppyDisk::FM:
			desc = get_desc_fm(current_size, end_gap_index);
			break;
		case FloppyDisk::MFM:
			desc = get_desc_mfm(current_size, end_gap_index);
			break;
		default:
			PERRF(LOG_FDC, "Invalid disk encoding type\n");
			return false;
	}

	int total_size = 200'000'000 / m_enc.cell_size;
	int remaining_size = total_size - current_size;
	if(remaining_size < 0) {
		PERRF(LOG_FDC, "Incorrect track layout, max_size=%d, current_size=%d\n", total_size, current_size);
		return false;
	}

	// Fixup the end gap
	desc[end_gap_index].p2 = remaining_size / 16;
	desc[end_gap_index + 1].p2 = remaining_size & 15;
	desc[end_gap_index + 1].p1 >>= 16 - (remaining_size & 15);

	assert(m_geom.spt < 40);

	uint8_t sectdata[40*512];
	desc_s sectors[40];
	std::streampos track_size = m_geom.spt * m_geom.secsize;

	assert(unsigned(track_size) < sizeof(sectdata));

	_file.seekg(0, std::ios::beg);

	for(int track=0; track < m_geom.tracks; track++) {
		for(int head=0; head < m_geom.sides; head++) {
			_file.read((char*)sectdata, track_size);
			if(_file.bad()) {
				return false;
			}
			build_sector_description(m_geom, sectdata, sectors);
			generate_track(desc, track, head, sectors, m_geom.spt, total_size, _disk);
		}
	}

	return true;
}

void FloppyFmt_IMG::build_sector_description(const FloppyDisk::Properties &f,
		const uint8_t *sectdata, desc_s *sectors) const
{
	int cur_offset = 0;
	for(int i=0; i<f.spt; i++) {
		sectors[i].data = sectdata + cur_offset;
		sectors[i].size = f.secsize;
		cur_offset += sectors[i].size;
		sectors[i].sector_id = i + SECTOR_BASE_ID;
	}
}

FloppyFmt::desc_e* FloppyFmt_IMG::get_desc_fm(int &current_size, int &end_gap_index)
{
	static FloppyFmt::desc_e desc[26] = {
		/* 00 */ { FM, 0xff, m_enc.gap_4a },
		/* 01 */ { FM, 0x00, 6 },
		/* 02 */ { RAW, 0xf77a, 1 },
		/* 03 */ { FM, 0xff, m_enc.gap_1 },
		/* 04 */ { SECTOR_LOOP_START, 0, m_geom.spt-1 },
		/* 05 */ {   FM, 0x00, 12 },
		/* 06 */ {   CRC_CCITT_FM_START, 1 },
		/* 07 */ {     RAW, 0xf57e, 1 },
		/* 08 */ {     TRACK_ID_FM },
		/* 09 */ {     HEAD_ID_FM },
		/* 10 */ {     SECTOR_ID_FM },
		/* 11 */ {     SIZE_ID_FM },
		/* 12 */ {   CRC_END, 1 },
		/* 13 */ {   CRC, 1 },
		/* 14 */ {   FM, 0xff, m_enc.gap_2 },
		/* 15 */ {   FM, 0x00, 6 },
		/* 16 */ {   CRC_CCITT_FM_START, 2 },
		/* 17 */ {     RAW, 0xf56f, 1 },
		/* 18 */ {     SECTOR_DATA_FM, -1 },
		/* 19 */ {   CRC_END, 2 },
		/* 20 */ {   CRC, 2 },
		/* 21 */ {   FM, 0xff, m_enc.gap_3 },
		/* 22 */ { SECTOR_LOOP_END },
		/* 23 */ { FM, 0xff, 0 },
		/* 24 */ { RAWBITS, 0xffff, 0 },
		/* 25 */ { END }
	};

	current_size = (m_enc.gap_4a + 6 + 1 + m_enc.gap_1) * 16;
	current_size += m_geom.secsize * m_geom.spt * 16;
	current_size += (12 + 1 + 4 + 2 + m_enc.gap_2 + 6 + 1 + 2 + m_enc.gap_3) * m_geom.spt * 16;

	end_gap_index = 23;

	return desc;
}

FloppyFmt::desc_e* FloppyFmt_IMG::get_desc_mfm(int &current_size, int &end_gap_index)
{
	static FloppyFmt::desc_e desc[29] = {
		/* 00 */ { MFM, 0x4e, 0 },
		/* 01 */ { MFM, 0x00, 12 },
		/* 02 */ { RAW, 0x5224, 3 },
		/* 03 */ { MFM, 0xfc, 1 },
		/* 04 */ { MFM, 0x4e, 0 },
		/* 05 */ { SECTOR_LOOP_START, 0, 0 },
		/* 06 */ {   MFM, 0x00, 12 },
		/* 07 */ {   CRC_CCITT_START, 1 },
		/* 08 */ {     RAW, 0x4489, 3 },
		/* 09 */ {     MFM, 0xfe, 1 },
		/* 10 */ {     TRACK_ID },
		/* 11 */ {     HEAD_ID },
		/* 12 */ {     SECTOR_ID },
		/* 13 */ {     SIZE_ID },
		/* 14 */ {   CRC_END, 1 },
		/* 15 */ {   CRC, 1 },
		/* 16 */ {   MFM, 0x4e, 0 },
		/* 17 */ {   MFM, 0x00, 12 },
		/* 18 */ {   CRC_CCITT_START, 2 },
		/* 19 */ {     RAW, 0x4489, 3 },
		/* 20 */ {     MFM, 0xfb, 1 },
		/* 21 */ {     SECTOR_DATA, -1 },
		/* 22 */ {   CRC_END, 2 },
		/* 23 */ {   CRC, 2 },
		/* 24 */ {   MFM, 0x4e, 0 },
		/* 25 */ { SECTOR_LOOP_END },
		/* 26 */ { MFM, 0x4e, 0 },
		/* 27 */ { RAWBITS, 0x9254, 0 },
		/* 28 */ { END }
	};

	desc[0].p2  = m_enc.gap_4a;
	desc[4].p2  = m_enc.gap_1;
	desc[5].p2  = m_geom.spt - 1;
	desc[16].p2 = m_enc.gap_2;
	desc[24].p2 = m_enc.gap_3;

	current_size = (m_enc.gap_4a + 12 + 3 + 1 + m_enc.gap_1) * 16;
	current_size += m_geom.secsize * m_geom.spt * 16;
	current_size += (12 + 3 + 1 + 4 + 2 + m_enc.gap_2 + 12 + 3 + 1 + 2 + m_enc.gap_3) * m_geom.spt * 16;

	end_gap_index = 26;

	return desc;
}

bool FloppyFmt_IMG::save(std::ofstream &_file, const FloppyDisk &_disk)
{
	try {
		return save_raw(_file, dynamic_cast<const FloppyDisk_Raw&>(_disk));
	} catch(std::bad_cast &) {
		// is this how proper C++ code should look like?
		return save_flux(_file, _disk);
	}
}

bool FloppyFmt_IMG::save_raw(std::ofstream &_file, const FloppyDisk_Raw &_disk)
{
	_file.seekp(0, std::ios::beg);

	auto geom = _disk.props();
	for(unsigned track=0; track<geom.tracks; track++) {
		for(unsigned head=0; head<geom.sides; head++) {
			for(unsigned b=0; b<_disk.get_buffer(track, head).size(); b++) {
				_file.write((char*)&_disk.get_buffer(track, head)[b], 1);
				if(_file.fail()) {
					return false;
				}
			}
		}
	}
	return true;
}

bool FloppyFmt_IMG::save_flux(std::ofstream &_file, const FloppyDisk &_disk)
{
	// Allocate the storage for the list of testable formats for a
	// given cell size
	std::vector<unsigned> candidates;

	// Format we're finally choosing
	unsigned chosen_candidate = FloppyDisk::FD_NONE;

	// Previously tested cell size
	int min_cell_size = 0;
	for(;;) {
		// Build the list of all formats for the immediately superior cell size
		int cur_cell_size = 0;
		candidates.clear();
		for(auto const &enc : ms_encodings) {
			if((_disk.props().type & FloppyDisk::SIZE_MASK) == (enc.first & FloppyDisk::SIZE_MASK)) {
				if(enc.second.cell_size == cur_cell_size)
				{
					candidates.push_back(enc.first);
				}
				else if((!cur_cell_size || enc.second.cell_size < cur_cell_size) &&
						enc.second.cell_size > min_cell_size)
				{
					candidates.clear();
					candidates.push_back(enc.first);
					cur_cell_size = enc.second.cell_size;
				}
			}
		}

		min_cell_size = cur_cell_size;

		// No candidates with a cell size bigger than the previously
		// tested one, we're done
		if(candidates.empty()) {
			break;
		}

		// Filter with track 0 head 0
		check_compatibility(_disk, candidates);

		// Nobody matches, try with the next cell size
		if(candidates.empty()) {
			continue;
		}

		// We have a match at that cell size, we just need to find the
		// best one given the geometry

		// If there's only one, we're done
		if(candidates.size() == 1) {
			chosen_candidate = candidates[0];
			break;
		}

		// Otherwise, find the best
		int tracks, heads;
		_disk.get_actual_geometry(tracks, heads);
		chosen_candidate = candidates[0];
		for(unsigned int i=1; i != candidates.size(); i++) {
			auto cc = FloppyDisk::std_types.at(FloppyDisk::StdType(chosen_candidate));
			auto cn = FloppyDisk::std_types.at(FloppyDisk::StdType(candidates[i]));

			// Handling enough sides is better than not
			if(cn.sides >= heads && cc.sides < heads) {
				goto change;
			} else if(cc.sides >= heads && cn.sides < heads) {
				goto dont_change;
			}

			// Since we're limited to two heads, at that point head
			// count is identical for both formats.

			// Handling enough tracks is better than not
			if(cn.tracks >= tracks && cc.tracks < tracks) {
				goto change;
			} else if(cc.tracks >= tracks && cn.tracks < tracks) {
				goto dont_change;
			}

			// Both are on the same side of the track count, so closest is best
			if(cc.tracks < tracks && cn.tracks > cc.tracks) {
				goto change;
			}
			if(cc.tracks >= tracks && cn.tracks < cc.tracks) {
				goto change;
			}
			goto dont_change;

		change:
			chosen_candidate = candidates[i];
		dont_change:
			;
		}
		// We have a winner, bail out
		break;
	}

	if(chosen_candidate == FloppyDisk::FD_NONE) {
		PERRF(LOG_FDC, "Error saving floppy disk: cannot find a valid format.\n");
		return false;
	}

	auto f = FloppyDisk::std_types.at(FloppyDisk::StdType(chosen_candidate));
	auto e = ms_encodings.at(chosen_candidate);
	std::streampos track_size = f.spt * f.secsize;

	int t,h;
	_disk.get_actual_geometry(t, h);
	if(f.tracks > t || f.sides > h) {
		PERRF(LOG_FDC, "Error saving floppy disk: invalid format\n");
		return false;
	}

	uint8_t sectdata[40*512];
	desc_s sectors[40];
	assert(unsigned(track_size) < sizeof(sectdata));
	assert(f.spt < sizeof(sectors));

	_file.seekp(0, std::ios::beg);

	for(int track=0; track < f.tracks; track++) {
		for(int head=0; head < f.sides; head++) {
			build_sector_description(f, sectdata, sectors);
			extract_sectors(_disk, f, e, sectors, track, head);
			_file.write((char*)sectdata, track_size);
			if(_file.bad()) {
				return false;
			}
		}
	}
	return true;
}

void FloppyFmt_IMG::check_compatibility(const FloppyDisk &_disk, std::vector<unsigned> &candidates)
{
	// Extract the sectors
	auto bitstream = generate_bitstream_from_track(0, 0, ms_encodings.at(candidates[0]).cell_size, _disk);
	std::vector<std::vector<uint8_t>> sectors;

	switch(ms_encodings.at(candidates[0]).type) {
		case FloppyDisk::FM:
			sectors = extract_sectors_from_bitstream_fm_pc(bitstream);
			break;
		case FloppyDisk::MFM:
			sectors = extract_sectors_from_bitstream_mfm_pc(bitstream);
			break;
		default:
			assert(false); break;
	}

	// Check compatibility with every candidate, copy in-place
	unsigned *ok_cands = &candidates[0];
	for(unsigned int i=0; i != candidates.size(); i++) {
		auto it = FloppyDisk::std_types.find(FloppyDisk::StdType(candidates[i]));
		if(it == FloppyDisk::std_types.end()) {
			continue;
		}
		auto format = it->second;
		int ns = 0;
		for(unsigned int j=0; j != sectors.size(); j++) {
			if(!sectors[j].empty()) {
				int sid;
				sid = j - SECTOR_BASE_ID;
				if(sid < 0 || sid > format.spt) {
					goto fail;
				}
				if(sectors[j].size() != format.secsize) {
					goto fail;
				}
				ns++;
			}
		}
		if(ns == format.spt) {
			*ok_cands++ = candidates[i];
		}
	fail:
		;
	}
	candidates.resize(ok_cands - &candidates[0]);
}


void FloppyFmt_IMG::extract_sectors(const FloppyDisk &_disk,
		const FloppyDisk::Properties &f, const encoding &e, desc_s *sdesc, int track, int head)
{
	// Extract the sectors
	auto bitstream = generate_bitstream_from_track(track, head, e.cell_size, _disk);
	std::vector<std::vector<uint8_t>> sectors;

	switch(e.type) {
		case FloppyDisk::FM:
			sectors = extract_sectors_from_bitstream_fm_pc(bitstream);
			break;
		case FloppyDisk::MFM:
			sectors = extract_sectors_from_bitstream_mfm_pc(bitstream);
			break;
		default:
			assert(false); break;
	}

	for(int i=0; i<f.spt; i++) {
		desc_s &ds = sdesc[i];
		if(ds.sector_id >= sectors.size() || sectors[ds.sector_id].empty()) {
			std::memset((void *)ds.data, 0, ds.size);
		} else if(sectors[ds.sector_id].size() < ds.size) {
			std::memcpy((void *)ds.data, sectors[ds.sector_id].data(), sectors[ds.sector_id].size());
			std::memset((uint8_t *)ds.data + sectors[ds.sector_id].size(), 0,
					sectors[ds.sector_id].size() - ds.size);
		} else {
			std::memcpy((void *)ds.data, sectors[ds.sector_id].data(), ds.size);
		}
	}
}
