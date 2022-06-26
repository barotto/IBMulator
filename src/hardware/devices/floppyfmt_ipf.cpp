// license:BSD-3-Clause
// copyright-holders:Olivier Galibert

// Based on MAME's "lib/formats/ipf_dsk.cpp"

#include "ibmulator.h"
#include "floppydisk.h"
#include "floppyfmt_ipf.h"
#include "filesys.h"
#include "utils.h"
#include <cstring>

FloppyDisk::Properties FloppyFmt_IPF::identify(std::string _file_path,
		uint64_t _file_size, FloppyDisk::Size _disk_size)
{
	UNUSED(_file_size);
	UNUSED(_disk_size);

	std::ifstream fstream = FileSys::make_ifstream(_file_path.c_str(), std::ios::binary);
	if(!fstream.is_open()){
		PWARNF(LOG_V1, LOG_FDC, "IPF: cannot open: '%s'\n", _file_path.c_str());
		return {0};
	}

	static const uint8_t refh[12] = { 0x43, 0x41, 0x50, 0x53, 0x00, 0x00, 0x00, 0x0c, 0x1c, 0xd5, 0x73, 0xba };
	uint8_t h[12];

	fstream.read(reinterpret_cast<char*>(h), 12);
	if(fstream.bad()) {
		PWARNF(LOG_V1, LOG_FDC, "IPF: cannot read: '%s'\n", _file_path.c_str());
		return {0};
	}

	if(memcmp(h, refh, 12) != 0) {
		PDEBUGF(LOG_V1, LOG_FDC, "IPF: invalid CAPS header: '%s'\n", _file_path.c_str());
		return {0};
	}

	uint8_t info[96];

	fstream.read(reinterpret_cast<char*>(info), 96);
	if(fstream.bad()) {
		PWARNF(LOG_V1, LOG_FDC, "IPF: cannot read INFO record: '%s'\n", _file_path.c_str());
		return {0};
	}
	static const uint8_t refinfo[4] = { 0x49, 0x4e, 0x46, 0x4f };
	if(memcmp(info, refinfo, 4) != 0) {
		PDEBUGF(LOG_V1, LOG_FDC, "IPF: invalid INFO header: '%s'\n", _file_path.c_str());
		return {0};
	}

	if(!m_ipf.parse_info(info)) {
		PDEBUGF(LOG_V1, LOG_FDC, "IPF: invalid INFO data: '%s'\n", _file_path.c_str());
		return {0};
	}

	m_geom.tracks = m_ipf.info.max_cylinder + 1;
	m_geom.sides = m_ipf.info.max_head + 1;

	IPFDecode::TrackInfo t;
	uint8_t imge[80];
	fstream.seekg(80*2, std::ifstream::cur);
	fstream.read(reinterpret_cast<char*>(imge), 80);
	if(fstream.bad()) {
		PWARNF(LOG_V1, LOG_FDC, "IPF: cannot read track: '%s'\n", _file_path.c_str());
		return {0};
	}
	if(!m_ipf.parse_imge(imge, t)) {
		PWARNF(LOG_V1, LOG_FDC, "IPF: invalid track IMGE: '%s'\n", _file_path.c_str());
		return {0};
	}

	PDEBUGF(LOG_V1, LOG_FDC, "IPF: cellcount=%u, cyl=%u, hds=%u, : %s\n",
			t.size_cells, m_geom.tracks, m_geom.sides, _file_path.c_str());

	if(t.size_cells > 150'000) {
		PDEBUGF(LOG_V1, LOG_FDC, "IPF: HD not supported\n");
		return {0};
	}

	m_geom.type |= FloppyDisk::DENS_DD;
	if(m_geom.tracks < 45) {
		m_geom.type |= FloppyDisk::SIZE_5_25;
		m_geom.desc = str_format("5.25\" %sDD", m_geom.sides==1?"SS":"DS");
	} else {
		m_geom.type |= FloppyDisk::SIZE_3_5;
		m_geom.desc = "3.5\" DSDD";
	}
	return m_geom;
}

std::string FloppyFmt_IPF::get_preview_string(std::string _filepath)
{
	auto props = identify(_filepath, 0, FloppyDisk::SIZE_8);
	if(!props.type) {
		return "Unknown or unsupported file type";
	}
	std::string info = "Format: SPS IPF File<br />";
	info += "Media: " + str_to_html(str_format("%s %u tracks", m_geom.desc.c_str(), m_geom.tracks)) + "<br />";
	const std::string encs[4] = {
		"Unknown", "CAPS", "SPS", "(invalid)"
	};
	info += "Encoder: " + encs[m_ipf.info.encoder_type & 0x3] +
			str_format(" rev.%u", m_ipf.info.encoder_revision) + "<br />";
	info += "File: " + str_format("%u rev.%u", m_ipf.info.release, m_ipf.info.revision) + "<br />";
	info += "Origin: " + str_format("0x%08x", m_ipf.info.origin) + "<br />";
	std::string day = str_format("%08u", m_ipf.info.credit_day);
	day.insert(4, "-");
	day.insert(7, "-");
	std::string time = str_format("%09u", m_ipf.info.credit_time);
	time.insert(2, ":");
	time.insert(5, ":");
	time.insert(8, ".");
	info += "Creation: " + day + " " + time + "<br />";
	info += "Disk: " + str_format("%u", m_ipf.info.disk_num) + "<br />";
	info += "Creator ID: " + str_format("%u", m_ipf.info.creator) + "<br />";
	return info;
}

bool FloppyFmt_IPF::load(std::ifstream &_file, FloppyDisk &_disk)
{
	PINFOF(LOG_V1, LOG_FDC, "Reading IPF file ...\n");

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
		PERRF(LOG_FDC, "IPF: Invalid disk geometry\n");
		return false;
	}

	try {
		return load_raw(_file, dynamic_cast<FloppyDisk_Raw&>(_disk));
	} catch(std::bad_cast &) {
		return load_flux(_file, _disk);
	}
}

bool FloppyFmt_IPF::load_raw(std::ifstream &, FloppyDisk_Raw &)
{
	// TODO?
	PERRF(LOG_FDC, "IPF: raw-sector disk emulation is not supported\n");
	return false;
}

bool FloppyFmt_IPF::load_flux(std::ifstream &_file, FloppyDisk &_disk)
{
	_file.seekg(0, std::ifstream::end);
	unsigned size = _file.tellg();
	if(size > 10_MB) {
		PERRF(LOG_FDC, "IPF: file's too big: %u bytes\n", size);
		return false;
	}
	_file.seekg(0, std::ifstream::beg);
	std::vector<uint8_t> data(size);
	_file.read(reinterpret_cast<char*>(&data[0]), size);
	if(_file.bad()) {
		PERRF(LOG_FDC, "IPF: cannot read file\n");
		return false;
	}
	return m_ipf.parse(data, &_disk);
}


uint32_t FloppyFmt_IPF::IPFDecode::rb(const uint8_t *&p, int count)
{
	uint32_t v = 0;
	for(int i=0; i<count; i++)
		v = (v << 8) | *p++;
	return v;
}

uint32_t FloppyFmt_IPF::IPFDecode::r32(const uint8_t *p)
{
	return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}

uint32_t FloppyFmt_IPF::IPFDecode::crc32r(const uint8_t *data, uint32_t size)
{
	// Reversed crc32
	uint32_t crc = 0xffffffff;
	for(uint32_t i=0; i != size; i++) {
		crc = crc ^ data[i];
		for(int j=0; j<8; j++)
			if(crc & 1)
				crc = (crc >> 1) ^ 0xedb88320;
			else
				crc = crc >> 1;
	}
	return ~crc;
}

bool FloppyFmt_IPF::IPFDecode::parse(std::vector<uint8_t> &data, FloppyDisk *image)
{
	tcount = 84*2+1; // Usual max
	tinfos.resize(tcount);
	if(scan_all_tags(data)) {
		return generate_tracks(image);
	}
	return false;
}

bool FloppyFmt_IPF::IPFDecode::parse_info(const uint8_t *_info)
{
	info.type = r32(_info+12);
	if(info.type != 1) {
		PDEBUGF(LOG_V1, LOG_FDC, "IPF: invalid INFO type=%u\n", info.type);
		return false;
	}
	info.encoder_type = r32(_info+16); // 1 for CAPS, 2 for SPS
	info.encoder_revision = r32(_info+20); // 1 always
	info.release = r32(_info+24);
	info.revision = r32(_info+28);
	info.origin = r32(_info+32); // Original source reference
	info.min_cylinder = r32(_info+36);
	info.max_cylinder = r32(_info+40);
	info.min_head = r32(_info+44);
	info.max_head = r32(_info+48);
	info.credit_day = r32(_info+52);  // year*1e4 + month*1e2 + day
	info.credit_time = r32(_info+56); // hour*1e7 + min*1e5 + sec*1e3 + msec
	for(int i=0; i<4; i++) {
		info.platform[i] = r32(_info+60+4*i);
	}
	info.disk_num = r32(_info+76);
	info.creator = r32(_info+80);
	for(int i=0; i<3; i++) {
		info.extra[i] = r32(_info+84+4*i);
	}
	return true;
}

FloppyFmt_IPF::IPFDecode::TrackInfo *FloppyFmt_IPF::IPFDecode::get_index(uint32_t idx)
{
	if(idx > 1000)
		return nullptr;
	if(idx >= tcount) {
		tinfos.resize(idx+1);
		tcount = idx+1;
	}

	return &tinfos[idx];
}

bool FloppyFmt_IPF::IPFDecode::parse_imge(const uint8_t *_imge, TrackInfo &_track)
{
	_track.cylinder = r32(_imge+12);
	if(_track.cylinder < info.min_cylinder || _track.cylinder > info.max_cylinder) {
		return false;
	}
	_track.head = r32(_imge+16);
	if(_track.head < info.min_head || _track.head > info.max_head) {
		return false;
	}
	_track.type = r32(_imge+20);
	_track.sigtype = r32(_imge+24); // 1 for 2us cells, no other value valid
	_track.size_bytes = r32(_imge+28);
	_track.index_bytes = r32(_imge+32);
	_track.index_cells = r32(_imge+36);
	_track.datasize_cells = r32(_imge+40);
	_track.gapsize_cells = r32(_imge+44);
	_track.size_cells = r32(_imge+48);
	_track.block_count = r32(_imge+52);
	_track.process = r32(_imge+56); // encoder process, always 0
	_track.weak_bits = r32(_imge+60);
	_track.data_key = r32(_imge+64);
	_track.reserved[0] = r32(_imge+68);
	_track.reserved[1] = r32(_imge+72);
	_track.reserved[2] = r32(_imge+76);

	return true;
}

bool FloppyFmt_IPF::IPFDecode::parse_data(const uint8_t *data, uint32_t &pos, uint32_t max_extra_size)
{
	TrackInfo *t = get_index(r32(data+24));
	if(!t)
		return false;

	t->data_size = r32(data+12);
	t->data_size_bits = r32(data+16);
	t->data = data+28;
	if(t->data_size > max_extra_size)
		return false;
	if(crc32r(t->data, t->data_size) != r32(data+20))
		return false;
	pos += t->data_size;
	return true;
}

bool FloppyFmt_IPF::IPFDecode::scan_one_tag(std::vector<uint8_t> &data, uint32_t &pos, uint8_t *&tag,
		uint32_t &tsize)
{
	if(data.size() - pos < 12) {
		PDEBUGF(LOG_V1, LOG_FDC, "IPF: Invalid file size\n");
		return false;
	}
	tag = &data[pos];
	tsize = r32(tag + 4);
	if(data.size() - pos < tsize) {
		PDEBUGF(LOG_V1, LOG_FDC, "IPF: Malformed file\n");
		return false;
	}
	uint32_t crc = r32(tag + 8);
	tag[8] = tag[9] = tag[10] = tag[11] = 0;
	if(crc32r(tag, tsize) != crc) {
		PDEBUGF(LOG_V1, LOG_FDC, "IPF: CRC error\n");
		return false;
	}
	pos += tsize;
	return true;
}

bool FloppyFmt_IPF::IPFDecode::scan_all_tags(std::vector<uint8_t> &data)
{
	uint32_t pos = 0;
	uint32_t size = data.size();
	while(pos != size) {
		uint8_t *tag;
		uint32_t tsize;

		if(!scan_one_tag(data, pos, tag, tsize)) {
			return false;
		}

		switch(r32(tag)) {
		case 0x43415053: // CAPS
			if(tsize != 12) {
				PDEBUGF(LOG_V1, LOG_FDC, "IPF: Invalid CAPS Header\n");
				return false;
			}
			break;

		case 0x494e464f: // INFO
			if(tsize != 96) {
				PDEBUGF(LOG_V1, LOG_FDC, "IPF: Invalid INFO Header\n");
				return false;
			}
			// INFO already parsed in identify()
			assert(info.type == 1);
			break;

		case 0x494d4745: // IMGE
		{
			if(tsize != 80) {
				PDEBUGF(LOG_V1, LOG_FDC, "IPF: Invalid IMGE Header\n");
				return false;
			}
			TrackInfo imge;
			if(!parse_imge(tag, imge)) {
				PDEBUGF(LOG_V1, LOG_FDC, "IPF: Invalid IMGE Block\n");
				return false;
			}
			TrackInfo *t = get_index(imge.data_key);
			if(!t) {
				PDEBUGF(LOG_V1, LOG_FDC, "IPF: Invalid Track\n");
				return false;
			}
			*t = imge;
			t->info_set = true;
			break;
		}
		case 0x44415441: // DATA
			if(tsize != 28) {
				PDEBUGF(LOG_V1, LOG_FDC, "IPF: Invalid DATA Header\n");
				return false;
			}
			if(!parse_data(tag, pos, size-pos)) {
				PDEBUGF(LOG_V1, LOG_FDC, "IPF: Invalid DATA Block\n");
				return false;
			}
			break;

		default:
			break;
		}
	}
	return true;
}

bool FloppyFmt_IPF::IPFDecode::generate_tracks(FloppyDisk *image)
{
	for(uint32_t i = 0; i != tcount; i++) {
		TrackInfo *t = &tinfos[i];
		if(t->info_set && t->data) {
			if(!generate_track(t, image)) {
				PERRF(LOG_FDC, "IPF: error generating track for cylinder %u, head %u\n",
						t->cylinder, t->head);
				return false;
			}
		} else if(t->info_set || t->data) {
			return false;
		}
	}
	return true;
}

void FloppyFmt_IPF::IPFDecode::rotate(std::vector<uint32_t> &track, uint32_t offset, uint32_t size)
{
	uint32_t done = 0;
	for(uint32_t bpos=0; done < size; bpos++) {
		uint32_t pos = bpos;
		uint32_t hold = track[pos];
		for(;;) {
			uint32_t npos = pos+offset;
			if(npos >= size)
				npos -= size;
			if(npos == bpos)
				break;
			track[pos] = track[npos];
			pos = npos;
			done++;
		}
		track[pos] = hold;
		done++;
	}
}

void FloppyFmt_IPF::IPFDecode::mark_track_splice(std::vector<uint32_t> &track, uint32_t offset, uint32_t size)
{
	for(int i=0; i<3; i++) {
		uint32_t pos = (offset + i) % size;
		uint32_t v = track[pos];
		if((v & FloppyDisk::MG_MASK) == MG_0)
			v = (v & FloppyDisk::TIME_MASK) | MG_1;
		else if((v & FloppyDisk::MG_MASK) == MG_1)
			v = (v & FloppyDisk::TIME_MASK) | MG_0;
		track[pos] = v;
	}
}

void FloppyFmt_IPF::IPFDecode::timing_set(std::vector<uint32_t> &track, uint32_t start, uint32_t end, uint32_t time)
{
	for(uint32_t i=start; i != end; i++)
		track[i] = (track[i] & FloppyDisk::MG_MASK) | time;
}

bool FloppyFmt_IPF::IPFDecode::generate_timings(TrackInfo *t, std::vector<uint32_t> &track,
		const std::vector<uint32_t> &data_pos, const std::vector<uint32_t> &gap_pos)
{
	timing_set(track, 0, t->size_cells, 2000);

	switch(t->type) {
	case 2: break;

	case 3:
		if(t->block_count >= 4)
			timing_set(track, gap_pos[3], data_pos[4], 1890);
		if(t->block_count >= 5) {
			timing_set(track, data_pos[4], gap_pos[4], 1890);
			timing_set(track, gap_pos[4], data_pos[5], 1990);
		}
		if(t->block_count >= 6) {
			timing_set(track, data_pos[5], gap_pos[5], 1990);
			timing_set(track, gap_pos[5], data_pos[6], 2090);
		}
		if(t->block_count >= 7)
			timing_set(track, data_pos[6], gap_pos[6], 2090);
		break;

	case 4:
		timing_set(track, gap_pos[t->block_count-1], data_pos[0], 1890);
		timing_set(track, data_pos[0], gap_pos[0], 1890);
		timing_set(track, gap_pos[0], data_pos[1], 1990);
		if(t->block_count >= 2) {
			timing_set(track, data_pos[1], gap_pos[1], 1990);
			timing_set(track, gap_pos[1], data_pos[2], 2090);
		}
		if(t->block_count >= 3)
			timing_set(track, data_pos[2], gap_pos[2], 2090);
		break;

	case 5:
		if(t->block_count >= 6)
			timing_set(track, data_pos[5], gap_pos[5], 2100);
		break;

	case 6:
		if(t->block_count >= 2)
			timing_set(track, data_pos[1], gap_pos[1], 2200);
		if(t->block_count >= 3)
			timing_set(track, data_pos[2], gap_pos[2], 1800);
		break;

	case 7:
		if(t->block_count >= 2)
			timing_set(track, data_pos[1], gap_pos[1], 2100);
		break;

	case 8:
		if(t->block_count >= 2)
			timing_set(track, data_pos[1], gap_pos[1], 2200);
		if(t->block_count >= 3)
			timing_set(track, data_pos[2], gap_pos[2], 2100);
		if(t->block_count >= 5)
			timing_set(track, data_pos[4], gap_pos[4], 1900);
		if(t->block_count >= 6)
			timing_set(track, data_pos[5], gap_pos[5], 1800);
		if(t->block_count >= 7)
			timing_set(track, data_pos[6], gap_pos[6], 1700);
		break;

	case 9: {
		uint32_t mask = r32(t->data + 32*t->block_count + 12);
		for(uint32_t i=1; i<t->block_count; i++)
			timing_set(track, data_pos[i], gap_pos[i], mask & (1 << (i-1)) ? 1900 : 2100);
		break;
	}

	default:
		return false;
	}

	return true;
}

bool FloppyFmt_IPF::IPFDecode::generate_track(TrackInfo *t, FloppyDisk *image)
{
	if(!t->size_cells)
		return true;

	if(t->data_size < 32*t->block_count)
		return false;

	// Annoyingly enough, too small gaps are ignored, changing the
	// total track size.  Artifact stemming from the byte-only support
	// of old times?
	t->size_cells = block_compute_real_size(t);

	if(t->index_cells >= t->size_cells)
		return false;

	std::vector<uint32_t> track(t->size_cells);
	std::vector<uint32_t> data_pos(t->block_count+1);
	std::vector<uint32_t> gap_pos(t->block_count);
	std::vector<uint32_t> splice_pos(t->block_count);

	bool context = false;
	uint32_t pos = 0;
	for(uint32_t i = 0; i != t->block_count; i++) {
		if(!generate_block(
				t, // TrackInfo
				i, // Block Desc. idx
				i == t->block_count-1 ? t->size_cells - t->index_cells : 0xffffffff, // ipos
				track,
				pos,
				data_pos[i],
				gap_pos[i],
				splice_pos[i],
				context
				)
		)
		{
			return false;
		}
	}
	if(pos != t->size_cells) {
		return false;
	}

	data_pos[t->block_count] = pos;

	mark_track_splice(track, splice_pos[t->block_count-1], t->size_cells);

	if(!generate_timings(t, track, data_pos, gap_pos)) {
		return false;
	}

	if(t->index_cells)
		rotate(track, t->size_cells - t->index_cells, t->size_cells);

	generate_track_from_levels(t->cylinder, t->head, track, splice_pos[t->block_count-1] + t->index_cells, *image);

	if(t->has_weak_cells) {
		image->set_track_damaged_info(t->cylinder, t->head);
	}

	return true;
}

void FloppyFmt_IPF::IPFDecode::track_write_raw(std::vector<uint32_t>::iterator &tpos, const uint8_t *data,
		uint32_t cells, bool &context)
{
	for(uint32_t i=0; i != cells; i++)
		*tpos++ = data[i>>3] & (0x80 >> (i & 7)) ? MG_1 : MG_0;
	if(cells)
		context = tpos[-1] == MG_1;
}

void FloppyFmt_IPF::IPFDecode::track_write_mfm(std::vector<uint32_t>::iterator &tpos, const uint8_t *data,
		uint32_t start_offset, uint32_t patlen, uint32_t cells, bool &context)
{
	patlen *= 2;
	for(uint32_t i=0; i != cells; i++) {
		uint32_t pos = (i + start_offset) % patlen;
		bool bit = data[pos>>4] & (0x80 >> ((pos >> 1) & 7));
		if(pos & 1) {
			*tpos++ = bit ? MG_1 : MG_0;
			context = bit;
		} else
			*tpos++ = context || bit ? MG_0 : MG_1;
	}
}

void FloppyFmt_IPF::IPFDecode::track_write_weak(std::vector<uint32_t>::iterator &tpos, uint32_t cells)
{
	for(uint32_t i=0; i != cells; i++)
		*tpos++ = FloppyDisk::MG_N;
}

bool FloppyFmt_IPF::IPFDecode::generate_block_data(TrackInfo *t, const uint8_t *data, const uint8_t *dlimit,
		std::vector<uint32_t>::iterator tpos, std::vector<uint32_t>::iterator tlimit, bool &context)
{
	for(;;) {
		if(data >= dlimit)
			return false;
		uint8_t val = *data++;
		if((val >> 5) > dlimit-data)
			return false;
		uint32_t param = rb(data, val >> 5);
		uint32_t tleft = tlimit - tpos;
		switch(val & 0x1f) {
		case 0: // End of description
			return !tleft;

		case 1: // Raw bytes
			if(8*param > tleft)
				return false;
			track_write_raw(tpos, data, 8*param, context);
			data += param;
			break;

		case 2: // MFM-decoded data bytes
		case 3: // MFM-decoded gap bytes
			if(16*param > tleft)
				return false;
			track_write_mfm(tpos, data, 0, 8*param, 16*param, context);
			data += param;
			break;

		case 5: // Weak bytes
			if(16*param > tleft)
				return false;
			track_write_weak(tpos, 16*param);
			t->has_weak_cells = true;
			context = 0;
			break;

		default:
			return false;
		}
	}
	// keep ide quiet
	assert(false);
	return false;
}

bool FloppyFmt_IPF::IPFDecode::generate_block_gap_0(uint32_t gap_cells, uint8_t pattern, uint32_t &spos,
		uint32_t ipos, std::vector<uint32_t>::iterator &tpos, bool &context)
{
	spos = ipos >= 16 && ipos+16 <= gap_cells ? ipos : gap_cells >> 1;
	track_write_mfm(tpos, &pattern, 0, 8, spos, context);
	uint32_t delta = 0;
	if(gap_cells & 1) {
		*tpos++ = MG_0;
		delta++;
	}
	track_write_mfm(tpos, &pattern, spos+delta-gap_cells, 8, gap_cells-spos-delta, context);
	return true;
}

bool FloppyFmt_IPF::IPFDecode::gap_description_to_reserved_size(const uint8_t *&data, const uint8_t *dlimit,
		uint32_t &res_size)
{
	res_size = 0;
	for(;;) {
		if(data >= dlimit)
			return false;
		uint8_t val = *data++;
		if((val >> 5) > dlimit-data)
			return false;
		uint32_t param = rb(data, val >> 5);
		switch(val & 0x1f) {
		case 0:
			return true;
		case 1:
			res_size += param*2;
			break;
		case 2:
			data += (param+7)/8;
			break;
		default:
			return false;
		}
	}
	// keep ide quiet
	assert(false);
	return false;
}

bool FloppyFmt_IPF::IPFDecode::generate_gap_from_description(const uint8_t *&data, const uint8_t *dlimit,
		std::vector<uint32_t>::iterator tpos, uint32_t size, bool pre, bool &context)
{
	const uint8_t *data1 = data;
	uint32_t res_size;
	if(!gap_description_to_reserved_size(data1, dlimit, res_size))
		return false;

	if(res_size > size)
		return false;
	uint8_t pattern[16];
	memset(pattern, 0, sizeof(pattern));
	uint32_t pattern_size = 0;

	uint32_t pos = 0, block_size = 0;
	for(;;) {
		uint8_t val = *data++;
		uint32_t param = rb(data, val >> 5);
		switch(val & 0x1f) {
		case 0:
			return size == pos;

		case 1:
			if(block_size)
				return false;
			block_size = param*2;
			pattern_size = 0;
			break;

		case 2:
			// You can't have a pattern at the start of a pre-slice
			// gap if there's a size afterwards
			if(pre && res_size && !block_size)
				return false;
			// You can't have two consecutive patterns
			if(pattern_size)
				return false;
			pattern_size = param;
			if(pattern_size > sizeof(pattern)*8)
				return false;

			memcpy(pattern, data, (pattern_size+7)/8);
			data += (pattern_size+7)/8;
			if(pre) {
				if(!block_size)
					block_size = size;
				else if(pos + block_size == res_size)
					block_size = size - pos;
				if(pos + block_size > size)
					return false;
				//              printf("pat=%02x size=%d pre\n", pattern[0], block_size);
				track_write_mfm(tpos, pattern, 0, pattern_size, block_size, context);
				pos += block_size;
			} else {
				if(pos == 0 && block_size && res_size != size)
					block_size = size - (res_size-block_size);
				if(!block_size)
					block_size = size - res_size;
				if(pos + block_size > size)
					return false;
				//              printf("pat=%02x block_size=%d size=%d res_size=%d post\n", pattern[0], block_size, size, res_size);
				track_write_mfm(tpos, pattern, -block_size, pattern_size, block_size, context);
				pos += block_size;
			}
			block_size = 0;
			break;

		default:
			break;
		}
	}

	// keep ide quiet
	assert(false);
	return false;
}


bool FloppyFmt_IPF::IPFDecode::generate_block_gap_1(uint32_t gap_cells, uint32_t &spos, uint32_t ipos,
		const uint8_t *data, const uint8_t *dlimit, std::vector<uint32_t>::iterator &tpos, bool &context)
{
	if(ipos >= 16 && ipos < gap_cells-16)
		spos = ipos;
	else
		spos = 0;
	return generate_gap_from_description(data, dlimit, tpos, gap_cells, true, context);
}

bool FloppyFmt_IPF::IPFDecode::generate_block_gap_2(uint32_t gap_cells, uint32_t &spos, uint32_t ipos,
		const uint8_t *data, const uint8_t *dlimit, std::vector<uint32_t>::iterator &tpos, bool &context)
{
	if(ipos >= 16 && ipos < gap_cells-16)
		spos = ipos;
	else
		spos = gap_cells;
	return generate_gap_from_description(data, dlimit, tpos, gap_cells, false, context);
}

bool FloppyFmt_IPF::IPFDecode::generate_block_gap_3(uint32_t gap_cells, uint32_t &spos, uint32_t ipos,
		const uint8_t *data, const uint8_t *dlimit, std::vector<uint32_t>::iterator &tpos,  bool &context)
{
	if(ipos >= 16 && ipos < gap_cells-16)
		spos = ipos;
	else {
		uint32_t presize, postsize;
		const uint8_t *data1 = data;
		if(!gap_description_to_reserved_size(data1, dlimit, presize))
			return false;
		if(!gap_description_to_reserved_size(data1, dlimit, postsize))
			return false;
		if(presize+postsize > gap_cells)
			return false;

		spos = presize + (gap_cells - presize - postsize)/2;
	}
	if(!generate_gap_from_description(data, dlimit, tpos, spos, true, context))
		return false;
	uint32_t delta = 0;
	if(gap_cells & 1) {
		tpos[spos] = MG_0;
		delta++;
	}

	return generate_gap_from_description(data, dlimit, tpos+spos+delta, gap_cells - spos - delta, false, context);
}

bool FloppyFmt_IPF::IPFDecode::generate_block_gap(uint32_t gap_type, uint32_t gap_cells, uint8_t pattern,
		uint32_t &spos, uint32_t ipos, const uint8_t *data, const uint8_t *dlimit,
		std::vector<uint32_t>::iterator tpos, bool &context)
{
	switch(gap_type) {
	case 0:
		return generate_block_gap_0(gap_cells, pattern, spos, ipos, tpos, context);
	case 1:
		return generate_block_gap_1(gap_cells, spos, ipos, data, dlimit, tpos, context);
	case 2:
		return generate_block_gap_2(gap_cells, spos, ipos, data, dlimit, tpos, context);
	case 3:
		return generate_block_gap_3(gap_cells, spos, ipos, data, dlimit, tpos, context);
	default:
		return false;
	}
}

bool FloppyFmt_IPF::IPFDecode::generate_block(TrackInfo *t, uint32_t idx, uint32_t ipos,std::vector<uint32_t> &track,
		uint32_t &pos, uint32_t &dpos, uint32_t &gpos, uint32_t &spos, bool &context)
{
	const uint8_t *data = t->data;
	const uint8_t *data_end = t->data + t->data_size;
	const uint8_t *thead = data + 32*idx; // Block Descriptor n.idx
	uint32_t data_cells = r32(thead);  // dataBits
	uint32_t gap_cells = r32(thead+4); // gapBits

	if(gap_cells < 8)
		gap_cells = 0;

	// +8  = gapOffset, gap description offset / datasize in bytes (when gap type = 0)
	// +12 = cellType,           1 = 2 µs cell / gap size in bytes (when gap type = 0)
	// +16 = encoderType, 1 = MFM
	// +20 = blockFlags, gap type
	// +24 = gapDefault, type 0 gap pattern (8 bits) / speed mask for sector 0 track type 9
	// +28 = dataOffset, data description offset

	uint32_t flags = r32(thead+20);
	if(flags & 4) {
		PERRF(LOG_FDC, "IPF: data stream sample length in bits is unsupported\n");
		return false;
	}

	dpos = pos;
	gpos = dpos + data_cells;
	pos = gpos + gap_cells;
	if(pos > t->size_cells)
		return false;
	if(!generate_block_data(t, data + r32(thead+28), data_end, track.begin()+dpos, track.begin()+gpos, context))
		return false;
	if(!generate_block_gap(flags, gap_cells, r32(thead+24), spos, ipos > gpos ? ipos-gpos : 0, data + r32(thead+8), data_end, track.begin()+gpos, context))
		return false;
	spos += gpos;

	return true;
}

uint32_t FloppyFmt_IPF::IPFDecode::block_compute_real_size(TrackInfo *t)
{
	uint32_t size = 0;
	const uint8_t *thead = t->data;
	for(unsigned int i=0; i != t->block_count; i++) {
		uint32_t data_cells = r32(thead);
		uint32_t gap_cells = r32(thead+4);
		if(gap_cells < 8)
			gap_cells = 0;

		size += data_cells + gap_cells;
		thead += 32;
	}
	return size;
}
