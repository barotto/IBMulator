// license:BSD-3-Clause
// copyright-holders:Michael Zapf, Marco Bortolin

// Based on MAME's "lib/formats/hxchfe_dsk.cpp"

#include "ibmulator.h"
#include "floppydisk.h"
#include "floppyfmt_hfe.h"
#include "filesys.h"
#include "utils.h"
#include <cstring>

FloppyDisk::Properties FloppyFmt_HFE::identify(std::string _file_path,
		uint64_t _file_size, FloppyDisk::Size _disk_size)
{
	UNUSED(_file_size);
	UNUSED(_disk_size);

	std::ifstream fstream = FileSys::make_ifstream(_file_path.c_str(), std::ios::binary);
	if(!fstream.is_open()){
		PWARNF(LOG_V1, LOG_FDC, "HFE: cannot open: '%s'\n", _file_path.c_str());
		return {0};
	}

	fstream.read(reinterpret_cast<char*>(&m_header), sizeof(m_header));
	if(fstream.bad()) {
		PWARNF(LOG_V1, LOG_FDC, "HFE: cannot read: '%s'\n", _file_path.c_str());
		return {0};
	}

	if(::memcmp(m_header.HEADERSIGNATURE, HFE_FORMAT_HEADER_v1, 8) == 0) {
		m_version = 1;
	} else if(::memcmp(m_header.HEADERSIGNATURE, HFE_FORMAT_HEADER_v3, 8) == 0) {
		m_version = 3;
		PWARNF(LOG_V2, LOG_FDC, "HFE: unsupported version: '%s'\n", _file_path.c_str());
		return {0};
	} else {
		PWARNF(LOG_V2, LOG_FDC, "HFE: invalid format: '%s'\n", _file_path.c_str());
		return {0};
	}

	m_geom.tracks = m_header.number_of_tracks;
	m_geom.sides = m_header.number_of_sides;

	if(m_geom.sides != 1 && m_geom.sides != 2) {
		PWARNF(LOG_V1, LOG_FDC, "HFE: invalid number of sides: '%s'\n", _file_path.c_str());
		return {0};
	}

	if(m_header.track_encoding != ISOIBM_MFM_ENCODING &&
	   m_header.track_encoding != ISOIBM_FM_ENCODING)
	{
		PWARNF(LOG_V1, LOG_FDC, "HFE: unsupported encoding: '%s'\n", _file_path.c_str());
		return {0};
	}

	if(m_header.bitRate <= 250*1.1) {
		m_geom.drate = FloppyDisk::DRATE_250;
	} else if(m_header.bitRate <= 300*1.1) {
		m_geom.drate = FloppyDisk::DRATE_300;
	} else if(m_header.bitRate <= 500*1.1) {
		m_geom.drate = FloppyDisk::DRATE_500;
	} else {
		PWARNF(LOG_V1, LOG_FDC, "HFE: unsupported bit rate: 1000 kbps\n", _file_path.c_str());
		return {0};
	}

	// read the pictracks
	unsigned pictrack_off = unsigned(m_header.track_list_offset) * 512u;
	//PDEBUGF(LOG_V2, LOG_FDC, "HFE: picktrack offset=%u: '%s'\n", pictrack_off, _file_path.c_str());
	fstream.seekg(pictrack_off, std::ios::beg);
	if(fstream.bad()) {
		PWARNF(LOG_V1, LOG_FDC, "HFE: invalid offset access: '%s'\n", _file_path.c_str());
		return {0};
	}
	m_cylinders.resize(m_header.number_of_tracks);
	unsigned cyls_size = sizeof(s_pictrack) * m_header.number_of_tracks;
	fstream.read(reinterpret_cast<char*>(&m_cylinders[0]), cyls_size);
	if(fstream.bad()) {
		PWARNF(LOG_V1, LOG_FDC, "HFE: cannot load cylinder data: '%s'\n", _file_path.c_str());
		return {0};
	}

	if(m_header.track_encoding == ISOIBM_FM_ENCODING) {
		// FM is for single density
		m_geom.type |= FloppyDisk::SIZE_8 | FloppyDisk::DENS_SD;
		m_geom.desc = str_format("8\" %sSD", m_geom.sides==1?"SS":"DS");
	} else {
		// MFM encoding is for everything else

		// Each cylinder contains the samples of both sides, 8 samples per
		// byte; the bitRate determines how many samples constitute a cell

		// DSDD: 360 KiB (5.25")= 2*40*9*512; 100000 cells/track, 2 us, 250 kbit/s
		// DSDD: 720 KiB (3.5") = 2*80*9*512; 100000 cells/track, 2 us, 250 kbit/s
		// DSHD: 1.4 MiB = 2*80*18*512 bytes; 200000 cells/track, 1 us, 500 kbit/s
		// DSED: 2.8 MiB = 2*80*36*512 bytes; 400000 cells/track, 500 ns, 1 Mbit/s

		// Use cylinder 1 (cyl 0 may have special encodings)
		int bitcount = (m_cylinders[1].track_len * 8 / 2);
		if(m_header.floppyRPM == 0) {
			int tracklen = bitcount / 8;
			if(bitcount & 7) {
				tracklen++;
			}
			double bps = m_header.bitRate * 1000.0;
			double track_period = tracklen * (4.0 / bps);
			m_header.floppyRPM = uint16_t(60.0 / track_period);
		}

		PDEBUGF(LOG_V2, LOG_FDC, "HFE: cellcount=%d, tracklen=%u, rpm=%d: '%s'\n",
				bitcount, m_cylinders[1].track_len, m_header.floppyRPM, _file_path.c_str());

		switch(m_header.floppyinterfacemode) {
			case IBMPC_ED_FLOPPYMODE:
				m_geom.type |= FloppyDisk::SIZE_3_5 | FloppyDisk::DENS_ED;
				m_geom.desc = "3.5\" DSED";
				break;
			case IBMPC_HD_FLOPPYMODE:
				m_geom.type |= FloppyDisk::DENS_HD;
				if(m_header.floppyRPM >= 360/1.1) {
					// 5.25" HD (1.2M)
					m_geom.type |= FloppyDisk::SIZE_5_25;
					m_geom.desc = "5.25\" DSHD";
				} else if(m_header.floppyRPM >= 300/1.1) {
					// 3.5" HD (1.44M)
					m_geom.type |= FloppyDisk::SIZE_3_5;
					m_geom.desc = "3.5\" DSHD";
				} else {
					PDEBUGF(LOG_V1, LOG_FDC, "HFE: invalid rpm value (%u): '%s'\n",
							m_header.floppyRPM, _file_path.c_str());
					m_geom.type |= FloppyDisk::SIZE_3_5|FloppyDisk::SIZE_5_25;
					m_geom.desc = "3.5\"/5.25\" DSHD";
				}
				break;
			case IBMPC_DD_FLOPPYMODE:
			case GENERIC_SHUGART_DD_FLOPPYMODE:
				// We cannot distinguish DD from QD without knowing the size of the floppy disk.
				// TODO? Force DD for now, no support for QD disks.
				m_geom.type |= FloppyDisk::DENS_DD;
				if(m_header.number_of_tracks < 45) {
					m_geom.type |= FloppyDisk::SIZE_5_25;
					m_geom.desc = str_format("5.25\" %sDD", m_geom.sides==1?"SS":"DS");
				} else if(m_header.number_of_tracks < 85) {
					if(m_header.floppyRPM > 360/1.1) {
						// 5.25 hd rpm
						PWARNF(LOG_V1, LOG_FDC, "HFE: invalid track count: '%s'\n", _file_path.c_str());
						return {0};
					}
					m_geom.type |= FloppyDisk::SIZE_3_5;
					m_geom.desc = "3.5\" DSDD";
				} else {
					PWARNF(LOG_V1, LOG_FDC, "HFE: invalid track count: '%s'\n", _file_path.c_str());
					return {0};
				}
				break;
			default:
				PWARNF(LOG_V1, LOG_FDC, "HFE: unsupported interface mode=%u: '%s'\n",
						m_header.floppyinterfacemode, _file_path.c_str());
				return {0};
		}
	}

	return m_geom;
}

MediumInfoData FloppyFmt_HFE::get_preview_string(std::string _filepath)
{
	identify(_filepath, 0, FloppyDisk::SIZE_8);
	if(!m_version) {
		std::string err("Not a valid HFE file");
		return { err, err };
	}
	std::string info = str_format("Format: HxC Floppy Emulator HFE File v.%u\n", m_version);
	info += str_format("Medium: %s %u tracks\n", m_geom.desc.c_str(), m_geom.tracks);
	info += "Encoding: ";
	switch(m_header.track_encoding) {
		case ISOIBM_MFM_ENCODING: info += "IBM MFM"; break;
		case AMIGA_MFM_ENCODING: info += "Amiga MFM"; break;
		case ISOIBM_FM_ENCODING: info += "IBM FM"; break;
		case EMU_FM_ENCODING: info += "EMU FM"; break;
		case UNKNOWN_ENCODING:
		default:  info += "unknown"; break;
	}
	info += "\n";
	info += str_format("Bitrate: %u Kbps\n", m_header.bitRate);
	info += str_format("RPM: %u\n", m_header.floppyRPM);
	//info += str_format("Single Step: 0x%x<br />", m_header.single_step);
	if(m_header.track0s0_altencoding == 0) {
		info += "Track 0 side 0 Encoding: ";
		switch(m_header.track0s0_encoding) {
			case ISOIBM_MFM_ENCODING: info += "IBM MFM"; break;
			case AMIGA_MFM_ENCODING: info += "Amiga MFM"; break;
			case ISOIBM_FM_ENCODING: info += "IBM FM"; break;
			case EMU_FM_ENCODING: info += "EMU FM"; break;
			case UNKNOWN_ENCODING:
			default:  info += "unknown"; break;
		}
		info += "\n";
	}
	if(m_header.track0s1_altencoding == 0) {
		info += "Track 0 side 1 Encoding: ";
		switch(m_header.track0s1_encoding) {
			case ISOIBM_MFM_ENCODING: info += "IBM MFM"; break;
			case AMIGA_MFM_ENCODING: info += "Amiga MFM"; break;
			case ISOIBM_FM_ENCODING: info += "IBM FM"; break;
			case EMU_FM_ENCODING: info += "EMU FM"; break;
			case UNKNOWN_ENCODING:
			default:  info += "unknown"; break;
		}
		info += "\n";
	}

	return { info, str_to_html(info) };
}

bool FloppyFmt_HFE::load(std::ifstream &_file, FloppyDisk &_disk)
{
	PINFOF(LOG_V1, LOG_FDC, "Reading HFE file ...\n");

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
		PERRF(LOG_FDC, "HFE: Invalid disk geometry\n");
		return false;
	}

	if(img_tracks < m_geom.tracks) {
		if(m_geom.tracks - img_tracks > FloppyFmt::DUMP_THRESHOLD) {
			PERRF(LOG_FDC, "HFE: Invalid disk geometry\n");
			return false;
		} else {
			// Some dumps has a few excess tracks to be safe,
			// lets be nice and just skip those tracks
			PWARNF(LOG_V0, LOG_FDC,
				"HFE: the floppy image has a slight excess of tracks for this disk that will be discarded"
				"(disk tracks=%d, image tracks=%d).\n",
				img_tracks, m_geom.tracks);
			m_geom.tracks = img_tracks;
		}
	}

	if(m_header.bitRate < 250 || m_header.bitRate > 500) {
		PERRF(LOG_FDC, "HFE: Unsupported bit rate %d.\n", m_header.bitRate);
		return false;
	}

	try {
		return load_raw(_file, dynamic_cast<FloppyDisk_Raw&>(_disk));
	} catch(std::bad_cast &) {
		return load_flux(_file, _disk);
	}
}

bool FloppyFmt_HFE::load_raw(std::ifstream &, FloppyDisk_Raw &)
{
	// TODO?
	PERRF(LOG_FDC, "HFE: raw-sector disk emulation is not supported\n");
	return false;
}

bool FloppyFmt_HFE::load_flux(std::ifstream &_file, FloppyDisk &_disk)
{
	int samplelength = 500'000 / m_header.bitRate;

	int size1track = (CYLTIME / samplelength) / 8;
	PDEBUGF(LOG_V2, LOG_FDC, "HFE: read %d cylinders:\n", m_geom.tracks);
	PDEBUGF(LOG_V2, LOG_FDC, "HFE:  bitrate=%u, samplelength=%02d, cyl.size=%d\n", m_header.bitRate, samplelength, size1track * 2);

	// Load the tracks
	std::vector<uint8_t> cylinder_buffer;
	for(int cyl=0; cyl < m_geom.tracks; cyl++)
	{
		int offset = int(m_cylinders[cyl].offset) << 9;
		int track_len = m_cylinders[cyl].track_len;
		int cylbufsize = (track_len + 0x1ff) & ~0x1ff;

		PDEBUGF(LOG_V3, LOG_FDC, "HFE:  C%02d: offset=0x%04x, track_len=%d, cylbufsize=%d\n",
				cyl, offset, m_cylinders[cyl].track_len, cylbufsize);

		// actual data read
		// The HFE format defines an interleave of the two sides per cylinder
		// at every 256 bytes
		cylinder_buffer.resize(cylbufsize);

		_file.seekg(offset, std::ios::beg);
		if(_file.bad()) {
			return false;
		}

		_file.read(reinterpret_cast<char*>(&cylinder_buffer[0]), cylbufsize);
		if(_file.bad()) {
			return false;
		}

		generate_track_from_hfe_bitstream(cyl, 0, samplelength, &cylinder_buffer[0], cylbufsize, _disk);
		if(m_geom.sides == 2) {
			generate_track_from_hfe_bitstream(cyl, 1, samplelength, &cylinder_buffer[0], cylbufsize, _disk);
		}
	}

	return true;
}

void FloppyFmt_HFE::generate_track_from_hfe_bitstream(int cyl, int head, int samplelength,
		const uint8_t *trackbuf, int track_end, FloppyDisk &image)
{
	// HFE has a few peculiarities:
	//
	// - "The track images do not always sum up to 200 ms but may be slightly shorter.
	//   We may assume that the last byte (last 8 samples) are part of the end
	//   gap, so it should not harm to repeat it until there are enough samples."
	//   Actually 8 samples are not enough to encode a GAP byte (4E). We will wrap around instead.
	//   And anyway, it's not always correct to assume the last byte is part of the end GAP...
	//   FIXME: This function doesn't handle this case correctly.
	//
	// - Tracks are sampled at 250 K/s for both FM and MFM, which yields
	//   50000 data bits (1 sample per cell) for MFM, while FM is twice
	//   oversampled (2 samples per cell).
	//   Accordingly, for both FM and MFM, we have 100000 samples, and the
	//   images are equally long for both recordings.
	//
	// - The oversampled FM images of HFE start with a cell 0 (no change),
	//   where a 1 would be expected for 125 K/s.
	//   In order to make an oversampled image look like a normally sampled one,
	//   we position the transition at 500 ns before the cell end.
	//   The HFE format has a 1 us minimum cell size; this means that a normally
	//   sampled FM image with 11111... at the begining means
	//
	//   125 kbit/s:    1   1   1   1   1...
	//   250 kbit/s:   01  01  01  01  01...
	//   500 kbit/s: 00010001000100010001...
	//
	//   -500             3500            7500            11500
	//     +-|---:---|---:-+ |   :   |   : +-|---:---|---:-+ |
	//     | |   :   |   : | |   :   |   : | |   :   |   : | |
	//     | |   :   |   : +-|---:---|---:-+ |   :   |   : +-|
	//  -500 0      2000    4000    6000    8000   10000   12000
	//
	//  3500 (1)     samplelength - 500
	//  7500 (1)     +samplelength
	// 11500 (1)     +samplelength
	// 15500 (1)     +samplelength
	//
	//  Double samples
	//
	//  1500 (0)    samplelength - 500
	//  3500 (1)    +samplelength
	//  5500 (0)    +samplelength
	//  7500 (1)    +samplelength
	//  9500 (0)    +samplelength
	// 11500 (1)    +samplelength
	//
	// - Subtracks are not supported

	std::vector<uint32_t> &dest = image.get_buffer(cyl, head);
	dest.clear();

	int offset = 0x100;

	if(head == 0) {
		offset = 0;
		// Tracks are assumed to be equally long for both sides, and to switch
		// sides every 0x100 bytes. So when the final block is c bytes long, and
		// it is padded to the next 0x100 multiple on the first side,
		// we skip back by c + (0x100-c) bytes, i.e. by 0x100
		track_end -= 0x100;
	}

	uint8_t curcells = 0;
	int timepos = -500;
	int track_size = -1;

	// We are creating a sequence of timestamps with flux information
	// As explained above, we arrange for the flux change to occur in the last
	// quarter of a cell

	while(timepos < CYLTIME)
	{
		curcells = trackbuf[offset];
		for (int j=0; j < 8; j++)
		{
			timepos += samplelength;
			if ((curcells & 1) != 0) {
				// Append another transition to the vector
				dest.push_back(FloppyDisk::MG_F | timepos);
			}

			// HFE uses little-endian bit order
			curcells >>= 1;
		}
		offset++;
		// We have alternating blocks of 0x100 bytes for each head
		// If we are at the block end, jump forward to the next block for this head
		if ((offset & 0xff) == 0) {
			offset += 0x100;
		}

		// If we have not reached the track end (after cyltime) but run
		// out of samples, wrap around.
		if (offset >= track_end) {
			PDEBUGF(LOG_V5, LOG_FDC, "HFE:   H%d: wrapping around at timepos=%d ns\n", head, timepos);
			offset = head * 0x100;
		}
		track_size++;
	}

	// Write splice is always at the start
	image.set_write_splice_position(cyl, head, 0);

	PDEBUGF(LOG_V5, LOG_FDC, "HFE:   H%d: timepos=%d, offset=%d, track_size=%d, track_end=%d\n", head, timepos, offset, track_size, track_end);
}

bool FloppyFmt_HFE::save(std::ofstream &_file, const FloppyDisk &_disk)
{
	int cylinders, heads;
	_disk.get_maximal_geometry(cylinders, heads);

	if(cylinders * sizeof(s_pictrack) > TRACK_TABLE_LENGTH) {
		PERRF(LOG_FDC, "HFE: Too many cylinders\n");
		return false;
	}

	// Determine the encoding
	encoding_t track_encoding = ISOIBM_MFM_ENCODING;
	encoding_t track0s0_encoding = ISOIBM_MFM_ENCODING;
	encoding_t track0s1_encoding = ISOIBM_MFM_ENCODING;

	// ISOIBM may change between FM and MFM
	int cell_size = 0;
	int samplerate = 250;

	const std::vector<uint32_t> &tbuf = _disk.get_buffer(1, 0);  // use track 1; may have to use more or others
	cell_size = determine_cell_size(tbuf);
	if(cell_size == 4000) {
		track_encoding = ISOIBM_FM_ENCODING;
	}

	// Check for alternative encodings for track 0 side 0 or side 1.
	const std::vector<uint32_t> &tbuf0 = _disk.get_buffer(0, 0);
	int cell_size0 = determine_cell_size(tbuf0);
	if(cell_size0 == 4000) {
		track0s0_encoding = ISOIBM_FM_ENCODING;
	}

	const std::vector<uint32_t> &tbuf1 = _disk.get_buffer(0, 1);
	cell_size0 = determine_cell_size(tbuf1);
	if(cell_size0 == 4000) {
		track0s1_encoding = ISOIBM_FM_ENCODING;
	}

	if(cell_size < 2000) {
		samplerate = 500;
	}

	uint8_t floppymode = DISABLE_FLOPPYMODE;
	switch(_disk.props().type & FloppyDisk::DENS_MASK) {
		case FloppyDisk::DENS_DD:
			floppymode = IBMPC_DD_FLOPPYMODE;
			break;
		case FloppyDisk::DENS_HD:
			floppymode = IBMPC_HD_FLOPPYMODE;
			break;
		default:
			break;
	}

	// Set up the header
	uint8_t header[HEADER_LENGTH];
	// fill it with the default value 0xff
	std::fill(std::begin(header), std::end(header), 0xff);

	std::memcpy(header, HFE_FORMAT_HEADER_v1, 8);
	header[0x08] = 0;
	header[0x09] = cylinders;
	header[0x0a] = heads;
	header[0x0b] = track_encoding;
	header[0x0c] = samplerate & 0xff;
	header[0x0d] = (samplerate >> 8) & 0xff;
	header[0x0e] = 0; // RPM is not used
	header[0x0f] = 0; // RPM is not used
	header[0x10] = floppymode;
	header[0x11] = 0;
	header[0x12] = 1;
	header[0x13] = 0;
	header[0x14] = !_disk.is_write_protected() ? 0xff : 0x00;
	header[0x15] = !_disk.double_step() ? 0xff : 0x00;

	// If no difference, keep the filled 0xff
	if(track0s0_encoding != track_encoding) {
		header[0x16] = 0x00;
		header[0x17] = track0s0_encoding;
	}
	if(track0s1_encoding != track_encoding) {
		header[0x18] = 0x00;
		header[0x19] = track0s1_encoding;
	}

	if(!FileSys::append(_file, header, HEADER_LENGTH)) {
		PERRF(LOG_FDC, "HFE: Cannot write to file.\n");
		return false;
	}

	// Set up the track list
	int samplelength = 500'000 / samplerate;

	// Calculate the buffer length for the cylinder
	int size1track = (CYLTIME / samplelength) / 8;

	// Round up the length of one side to a 0x100 multiple (padding)
	int cylsize = ((size1track + 0xff) & ~0xff) + size1track;
	// Buffer size is multiple of 0x200
	int cylbufsize = (cylsize + 0x1ff) & ~0x1ff;

	PDEBUGF(LOG_V2, LOG_FDC, "HFE: write %d cylinders of %d bytes:\n", cylinders, cylsize);
	PDEBUGF(LOG_V2, LOG_FDC, "HFE:  cyltime=%d, cell_size=%d, samplelength=%d, trk.size=%d, cylsize=%d, cylbufsize=%d\n",
			CYLTIME, cell_size, samplelength, size1track, cylsize, cylbufsize);

	// Create the lookup table
	// Each entry contains two 16-bit values
	int trackpos = TRACKS_OFFSET;
	std::vector<s_pictrack> track_table(TRACK_TABLE_ENTRIES);
	// The HxC program fills it with the default value 0xFF, and so do we.
	std::fill(std::begin(track_table), std::end(track_table), s_pictrack{0xffff,0xffff});

	for(int cyl = 0; cyl < cylinders; cyl++) {
		track_table[cyl].offset = trackpos >> 9; // position in 512 bytes blocks
		track_table[cyl].track_len = cylsize; // 2 tracks but only the first track is multiple of 256
		trackpos += cylbufsize;
	}

	if(!FileSys::append(_file, &track_table[0], TRACK_TABLE_LENGTH)) {
		PERRF(LOG_FDC, "HFE: Cannot write to file.\n");
		return false;
	}

	std::vector<uint8_t> cylbuf(cylbufsize);

	for(int cyl = 0; cyl < cylinders; cyl++) {
		PDEBUGF(LOG_V3, LOG_FDC, "HFE:  C%02d: offset=0x%04x\n", cyl, track_table[cyl].offset << 9);

		// Even when the image is set as single-sided, we write both sides.
		// 0-fill the cyl buffer to account for unformatted tracks or single side.
		std::fill(std::begin(cylbuf), std::end(cylbuf), 0);
		generate_hfe_bitstream_from_track(cyl, 0, CYLTIME, samplelength, cylbuf, _disk);
		generate_hfe_bitstream_from_track(cyl, 1, CYLTIME, samplelength, cylbuf, _disk);

		// Save each track; get the position and length from the lookup table
		if(!FileSys::write_at(_file, track_table[cyl].offset << 9, &cylbuf[0], cylbufsize)) {
			PERRF(LOG_FDC, "HFE: Cannot write to file.\n");
			return false;
		}
	}

	return true;
}

int FloppyFmt_HFE::determine_cell_size(const std::vector<uint32_t> &tbuf) const
{
	// Find out the cell size so we can tell whether this is FM or MFM recording.
	//
	// Some systems may have a fixed recording; the size should then be set
	// on instantiation.
	// The encoding may have changed by reformatting; we cannot rely on the
	// header that we loaded.
	//
	// The HFE format needs this information for its format header, which is
	// a bit tricky, because we have to assume a correctly formatted track.
	// Some flux lengths may appear in different recordings:
	//
	//                        Encodings by time in us
	//  Flux lengths   Dens   2     3     4     5     6      7      8
	//  Cell size 4us  SD     -     -     1     -     -      -      10
	//            2us  DD     -     -     10    -     100    -      1000
	//            1us  HD     10    100   1000  -     -      -      -
	//
	// To be sure, we have to find a flux length of 6us (MFM/DD) or 3 us or
	// 2 us (MFM/HD). A length of 4 us may appear for all densities.
	// If there is at least one MFM-IDAM on the track, this will deliver
	// a 6 us length for DD or 3 us for HD (01000[100]1000[100]1).
	// Otherwise we assume FM (4 us).

	// MAME track format:
	// xlllllll xlllllll xlllllll xlllllll xlllllll xlllllll ...
	// |\--+--/
	// |  Area Length in ns (TIME_MASK=0x0fffffff), max: 199999999 ns
	// +- Area code (0=Flux change, 1=non-magnetized, 2=damaged, 3=end of zone)
	//
	// Empty track: zero-length array, equiv to [(non_mag,0ns),(end, 199999999ns)]
	//
	// The HFE format only supports flux changes, no demagnetized or defect zones.
	//
	int cell_start = -1;
	int cell_size = 4000;

	// Skip the beginning (may have a short cell)
	for(unsigned i=2; i < tbuf.size(); i++)
	{
		if (cell_start >= 0)
		{
			int fluxlen = (tbuf[i] & FloppyDisk::TIME_MASK) - cell_start;
			// Is this a flux length of less than 3.5us (HD) or of 6 us (DD)?
			if (fluxlen < 3500)
			{
				cell_size = 1000;
				break;
			}
			if ((fluxlen > 5500 && fluxlen < 6500))
			{
				cell_size = 2000;
				break;
			}
		}
		// We only measure from the last flux change
		if ((tbuf[i] & FloppyDisk::MG_MASK) == FloppyDisk::MG_F) {
			cell_start = tbuf[i] & FloppyDisk::TIME_MASK;
		} else {
			cell_start = -1;
		}
	}
	return cell_size;
}

void FloppyFmt_HFE::generate_hfe_bitstream_from_track(
		int cyl, int head, long cyltime,
		int samplelength, std::vector<uint8_t> &cylinder_buffer,
		const FloppyDisk &_disk) const
{
	// See FloppyFmt::generate_bitstream_from_track
	// as the original code

	const std::vector<uint32_t> &tbuf = _disk.get_buffer(cyl, head);
	if(tbuf.size() <= 1)
	{
		// Unformatted track
		// HFE does not support unformatted tracks. Return without changes,
		// we assume that the track image was initialized with zeros.
		return;
	}

	// We start directly at position 0, as this format does not preserve a
	// write splice position
	int cur_time = 0;
	int buf_pos = 0;

	// The remaining part of this method is very similar to the implementation
	// of the PLL in FloppyFmt, except that it directly creates the
	// bytes for the format. Bits are stored from right to left in each byte.
	int period = samplelength;
	int period_adjust_base = period * 0.05;

	int min_period = int(samplelength * 0.75);
	int max_period = int(samplelength * 1.25);
	int phase_adjust = 0;
	int freq_hist = 0;
	int next = 0;

	int track_end = cylinder_buffer.size();

	// Prepare offset for the format storage
	int offset = 0x100; // side 1
	if(head == 0) {
		offset = 0;
		track_end -= 0x0100;
	}

	uint8_t bit = 0x01;
	uint8_t current = 0;

	// The HFE format fills all the track buffer, including the padding, regardless of the actual track length.
	// We will wrap around at the end of track.
	while(offset < track_end) {
		int edge = tbuf[buf_pos] & FloppyDisk::TIME_MASK;

		// Edge on start of track? Use next entry.
		if(edge == 0)
		{
			cur_time = 0;
			edge = tbuf[++buf_pos] & FloppyDisk::TIME_MASK;
		}

		// Wrapped over end?
		if(edge < cur_time) {
			edge += cyltime;
		}

		// End of cell
		next = cur_time + period + phase_adjust;

		// End of the window is at next; edge is the actual transition
		if(edge >= next)
		{
			// No transition in the window -> 0
			phase_adjust = 0;
		}
		else
		{
			// Transition in the window -> 1
			current |= bit;
			int delta = edge - (next - period / 2);

			phase_adjust = 0.65 * delta;

			if (delta < 0)
			{
				if (freq_hist < 0) freq_hist--;
				else freq_hist = -1;
			}
			else
			{
				if (delta > 0)
				{
					if(freq_hist > 0) {
						freq_hist++;
					} else {
						freq_hist = 1;
					}
				}
				else freq_hist = 0;
			}

			if (freq_hist)
			{
				int afh = freq_hist < 0 ? -freq_hist : freq_hist;
				if (afh > 1)
				{
					int aper = period_adjust_base * delta / period;
					if (!aper) {
						aper = freq_hist < 0 ? -1 : 1;
					}
					period += aper;

					if (period < min_period) {
						period = min_period;
					} else if (period > max_period) {
						period = max_period;
					}
				}
			}
		}

		cur_time = next;

		// Wrap over the start of the track
		if (cur_time >= cyltime)
		{
			cur_time -= cyltime;
			buf_pos = 0;
		}

		bit = (bit << 1) & 0xff;
		if (bit == 0)
		{
			// All 8 cells done, write result byte to track image and start
			// over with the next one
			bit = 0x01;
			cylinder_buffer[offset++] = current;

			// Do we have a limit for the track end?
			if((track_end > 0) && (offset >= track_end)) {
				break;
			}

			// Skip to next block for this head
			if((offset & 0xff) == 0) {
				offset += 0x100;
			}
			current = 0;
		}

		// We may have more entries before the edge that indicates the end of
		// this cell. But this cell is done, so skip them all.
		// Fast-forward to next cell
		while (buf_pos < int(tbuf.size())-1 && int(tbuf[buf_pos] & FloppyDisk::TIME_MASK) < cur_time)
		{
			buf_pos++;
		}

		// Reaching the end of the track
		// Wrap around
		if (buf_pos == int(tbuf.size())-1 && int(tbuf[buf_pos] & FloppyDisk::TIME_MASK) < cur_time)
		{
			buf_pos = 0;
		}
	}
	// Write the current byte when not done
	if (bit != 0x01) {
		if(offset >= track_end) {
			// This can happen in case of bugs in track bitstream generation.
			PWARNF(LOG_V0, LOG_FDC, "HFE:     %d: invalid buffer offset %d >= %d\n", cyl, offset, track_end);
		} else {
			cylinder_buffer[offset] = current;
		}
	}

	PDEBUGF(LOG_V5, LOG_FDC, "HFE:   H%d: cur_time=%d, bit=%d, offset=%d, track_end=%d\n", head, cur_time, bit, offset, track_end);
}
