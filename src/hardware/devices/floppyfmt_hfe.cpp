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
		m_geom.drate = FloppyDisk::DRATE_1000;
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

	if(m_header.bitRate > 500) {
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
	int samplelength = 500000 / m_header.bitRate;

	// Load the tracks
	std::vector<uint8_t> cylinder_buffer;
	for(int cyl=0; cyl < m_geom.tracks; cyl++)
	{
		// actual data read
		// The HFE format defines an interleave of the two sides per cylinder
		// at every 256 bytes
		cylinder_buffer.resize(m_cylinders[cyl].track_len);

		int offset = int(m_cylinders[cyl].offset) << 9;
		_file.seekg(offset, std::ios::beg);
		if(_file.bad()) {
			return false;
		}

		_file.read(reinterpret_cast<char*>(&cylinder_buffer[0]), m_cylinders[cyl].track_len);
		if(_file.bad()) {
			return false;
		}

		generate_track_from_hfe_bitstream(cyl, 0, samplelength, &cylinder_buffer[0], m_cylinders[cyl].track_len, _disk);
		if(m_geom.sides == 2) {
			generate_track_from_hfe_bitstream(cyl, 1, samplelength, &cylinder_buffer[0], m_cylinders[cyl].track_len, _disk);
		}
	}

	return true;
}

void FloppyFmt_HFE::generate_track_from_hfe_bitstream(int cyl, int head, int samplelength,
		const uint8_t *trackbuf, int track_end, FloppyDisk &image)
{
	// HFE has a minor issue: The track images do not sum up to 200 ms.
	// Tracks are samples at 250 kbit/s for both FM and MFM, which yields
	// 50000 data bits (100000 samples) for MFM, while FM is twice oversampled
	// (4 samples per actual data bit)
	// Hence, for both FM and MFM, we need 100000 samples.

	// Track length 61B0 (both sides, FM)
	// 100 + 100 + ... + 100 + (B0+50)    = 3000 + B0 + 50 (pad)
	//    100 + 100 + .... + 100  +  B0   = 3000 + B0 = 99712 samples   (-288)

	// Track length 61C0 (both sides, MFM)
	// 100 + 100 + ... + 100 + (C0+40)       = 3000 + C0 + 40 (pad)
	//    100 + 100 + .... + 100   +   C0    = 3000 + C0 = 99840 samples   (-160)

	// Solution: Repeat the last byte until we have enough samples
	// Note: We do not call normalize_times here because we're doing the job here

	// MG_1 / MG_0 are (logical) levels that indicate transition / no change
	// MG_F is the position of a flux transition

	std::vector<uint32_t> &dest = image.get_buffer(cyl, head);
	dest.clear();

	int offset = 0x100;

	if(head == 0) {
		offset = 0;
		track_end -= 0x0100;
	}

	uint8_t current = 0;
	int time  = 0;

	// Oversampled FM images (250 kbit/s) start with a 0, where a 1 is
	// expected for 125 kbit/s.
	// In order to make an oversampled image look like a normally sampled one,
	// we position the transition at 500 ns before the cell end.
	// The HFE format has a 1 us minimum cell size; this means that a normally
	// sampled FM image with 11111... at the begining means
	// 125 kbit/s:    1   1   1   1   1...
	// 250 kbit/s:   01  01  01  01  01...
	// 500 kbit/s: 00010001000100010001...
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

	time = -500;

	// We are creating a sequence of timestamps with flux info
	// Note that the flux change occurs in the last quarter of a cell

	while(time < 200'000'000)   // one rotation in nanosec
	{
		current = trackbuf[offset];
		for (int j=0; j < 8; j++)
		{
			time += samplelength;
			if ((current & 1)!=0)
				// Append another transition to the vector
				dest.push_back(FloppyDisk::MG_F | time);

			// HFE uses little-endian bit order
			current >>= 1;
		}
		offset++;
		if ((offset & 0xff)==0) {
			offset += 0x100;
		}

		// When we have not reached the track end (after 0.2 sec) but run
		// out of samples, repeat the last value
		if (offset >= track_end) offset = track_end - 1;
	}

	image.set_write_splice_position(cyl, head, 0);
}

bool FloppyFmt_HFE::save(std::ofstream &_file, const FloppyDisk &_disk)
{
	std::vector<uint8_t> cylbuf;

	// Create a buffer that is big enough to handle HD formats. We don't
	// know the track length until we generate the HFE bitstream.
	cylbuf.resize(0x10000); // multiple of 512

	std::vector<s_pictrack> pictrack;
	uint8_t header[HEADER_LENGTH];
	uint8_t track_table[TRACK_TABLE_LENGTH] = {};

	// Set up header
	memcpy(header, HFE_FORMAT_HEADER_v1, 8);

	header[8] = 0;
	// Can we change the number of tracks or heads?
	int cylinders, heads;
	_disk.get_actual_geometry(cylinders, heads);

	if(cylinders * sizeof(s_pictrack) > TRACK_TABLE_LENGTH) {
		PERRF(LOG_FDC, "HFE: Too many cylinders\n");
		return false;
	}

	pictrack.resize(cylinders);

	header[9] = cylinders;
	header[10] = heads;
	// Floppy RPM is not used
	header[14] = 0;
	header[15] = 0;

	// Bit rate and encoding will be set later
	encoding_t track0s0_encoding = UNKNOWN_ENCODING;
	encoding_t track0s1_encoding = UNKNOWN_ENCODING;
	encoding_t track_encoding = UNKNOWN_ENCODING;

	switch(_disk.props().type & FloppyDisk::DENS_MASK) {
		case FloppyDisk::DENS_DD:
			header[16] = IBMPC_DD_FLOPPYMODE;
			break;
		case FloppyDisk::DENS_HD:
			header[16] = IBMPC_HD_FLOPPYMODE;
			break;
		default:
			header[16] = DISABLE_FLOPPYMODE;
			break;
	}
	header[17] = 0;

	// The track lookup table is located at offset 0x200 (as 512 multiple)
	header[18] = 1;
	header[19] = 0;

	header[20] = !_disk.is_write_protected() ? 0xff : 0x00;
	header[21] = !_disk.double_step() ? 0xff : 0x00;

	header[22] = 0xff;
	header[23] = track0s0_encoding;
	header[24] = 0xff;
	header[25] = track0s1_encoding;

	// Fill the remaining bytes with 0xff
	for (int i=26; i < HEADER_LENGTH; i++) {
		header[i] = 0xff;
	}

	// Header and track table will be finalized at the end
	// write an incomplete header (1 block)
	if(!FileSys::append(_file, header, HEADER_LENGTH)) {
		PERRF(LOG_FDC, "HFE: Cannot write to file\n");
		return false;
	}
	// write a dummy track table (1 block)
	if(!FileSys::append(_file, track_table, TRACK_TABLE_LENGTH)) {
		PERRF(LOG_FDC, "HFE: Cannot write to file\n");
		return false;
	}
	// track data start here
	// start at block 2 (0x200*2=1024)
	int track_offset = HEADER_LENGTH + TRACK_TABLE_LENGTH;

	int samplelength = 2000;

	// We won't have more than 200000 cells on the track
	encoding_t trkenc_s0 = UNKNOWN_ENCODING, trkenc_s1 = UNKNOWN_ENCODING;
	for(int cyl=0; cyl<cylinders; cyl++)
	{
		// After the call, the encoding will be set to FM or MFM
		if(cyl <= 1) {
			trkenc_s0 = UNKNOWN_ENCODING;
			trkenc_s1 = UNKNOWN_ENCODING;
		}
		int side0_bytes = 0, side1_bytes = 0;
		generate_hfe_bitstream_from_track(cyl, 0, samplelength, trkenc_s0, &cylbuf[0],
				side0_bytes, _disk);
		if(heads == 2) {
			generate_hfe_bitstream_from_track(cyl, 1, samplelength, trkenc_s1, &cylbuf[0],
				side1_bytes, _disk);
		}
		// hfe allows for different trk0 side encodings, but the track length is common to both
		int track_len = std::max(side0_bytes, side1_bytes) * 2;
		track_encoding = trkenc_s0;
		pictrack[cyl].offset = track_offset >> 9; // in 512 bytes blocks
		pictrack[cyl].track_len = track_len; // real length in bytes
		track_len = (track_len + 0x1FF) & 0xfe00; // multiple of 512 bytes

		// Write the current cylinder
		if(!FileSys::append(_file, &cylbuf[0], track_len)) {
			PERRF(LOG_FDC, "HFE: Cannot write to file\n");
			return false;
		}

		track_offset += track_len;

		if(cyl == 0) {
			track0s0_encoding = trkenc_s0;
			track0s1_encoding = trkenc_s1;
		} else {
			int bit_rate = 500'000 / samplelength;
			header[12] = bit_rate & 0xff;
			header[13] = (bit_rate >> 8) & 0xff;
		}
	}

	// Complete and update the header
	header[11] = track_encoding;
	if(track0s0_encoding != track_encoding) {
		header[22] = 0;
		header[23] = track0s0_encoding;
	}
	if(track0s1_encoding != track_encoding) {
		header[24] = 0;
		header[25] = track0s1_encoding;
	}
	if(!FileSys::write_at(_file, 0, header, HEADER_LENGTH)) {
		PERRF(LOG_FDC, "HFE: Cannot write to file\n");
		return false;
	}

	// Complete and update the track table
	for(int i=0; i<cylinders; i++) {
		track_table[i*4]   =  pictrack[i].offset & 0xff;
		track_table[i*4+1] = (pictrack[i].offset>>8) & 0xff;
		track_table[i*4+2] =  pictrack[i].track_len & 0xff;
		track_table[i*4+3] = (pictrack[i].track_len>>8) & 0xff;
	}
	// Set the remainder to 0xff
	for(int i=cylinders*4; i<TRACK_TABLE_LENGTH; i++) {
		track_table[i] = 0xff;
	}
	if(!FileSys::write_at(_file, HEADER_LENGTH, track_table, TRACK_TABLE_LENGTH)) {
		PERRF(LOG_FDC, "HFE: Cannot write to file\n");
		return false;
	}

	return true;
}

void FloppyFmt_HFE::generate_hfe_bitstream_from_track(int cyl, int head,
		int &samplelength, encoding_t &encoding, uint8_t *cylinder_buffer,
		int &track_bytes, const FloppyDisk &_disk)
{
	// We are using an own implementation here because the result of the
	// parent class method would require some post-processing that we
	// can easily avoid.

	// See floppy_image_format_t::generate_bitstream_from_track
	// as the original code

	track_bytes = 0;

	const std::vector<uint32_t> &tbuf = _disk.get_buffer(cyl, head);
	if(tbuf.size() <= 1)
	{
		// Unformatted track
		// TODO must handle that according to HFE
		int track_size = 200'000'000 / samplelength;
		track_bytes = (track_size + 7) / 8;
		int track_blocks = (track_bytes + 0x1FF) >> 9;
		for(int b=0; b<track_blocks; b++) {
			int buffoff = b * 0x200 + head * 0x100;
			memset(&cylinder_buffer[buffoff], 0, 0x100);
		}
		return;
	}

	// Find out whether we have FM or MFM recording, and determine the bit rate.
	// This is needed for the format header.
	//
	// The encoding may have changed by reformatting; we cannot rely on the
	// header when loading.
	//
	// FM:   encoding 1    -> flux length = 4 us (min)          ambivalent
	//       encoding 10   -> flux length = 8 us (max)          ambivalent
	// MFM:  encoding 10   -> flux length = 4 us (min, DD)      ambivalent
	//       encoding 100  -> flux length = 6 us (DD)           significant
	//       encoding 1000 -> flux length = 8 us (max, DD)      ambivalent
	//       encoding 10   -> flux length = 2 us (min, HD)      significant
	//       encoding 100  -> flux length = 3 us (max, HD)      significant

	// If we have MFM, we should very soon detect a flux length of 6 us.
	// But if we have FM, how long should we search to be sure?
	// We assume that after 2000 us we should have reached the first IDAM,
	// which contains a sequence 1001, implying a flux length of 6 us.
	// If there was no such flux in that area, this can safely be assumed to be FM.

	// Do it only for the first track; the format only supports one encoding.
	if (encoding == UNKNOWN_ENCODING)
	{
		bool mfm_recording = false;
		int time0 = 0;
		int minflux = 4000;
		int fluxlen = 0;
		// Skip the beginning (may have a short cell)
		for(size_t i=2; (i < tbuf.size()-1) && (time0 < 2'000'000) && !mfm_recording; i++)
		{
			time0 = tbuf[i] & FloppyDisk::TIME_MASK;
			fluxlen = (tbuf[i+1] & FloppyDisk::TIME_MASK) - time0;
			if ((fluxlen < 3500) || (fluxlen > 5500 && fluxlen < 6500)) {
				mfm_recording = true;
			}
			if (fluxlen < minflux) {
				minflux = fluxlen;
			}
		}
		encoding = mfm_recording ? ISOIBM_MFM_ENCODING : ISOIBM_FM_ENCODING;

		// samplelength = 1000ns => 10^6 cells/sec => 500 kbit/s
		// samplelength = 2000ns => 250 kbit/s
		// We stay with double sampling at 250 kbit/s for FM
		if (minflux < 3500) {
			samplelength = 1000;
		} else {
			samplelength = 2000;
		}
	}

	// Start at the write splice
	uint32_t splice = _disk.get_write_splice_position(cyl, head);

	int cur_pos = splice;
	int cur_entry = 0;

	// Fast-forward to the write splice position (always 0 in this format)
	while(cur_entry < int(tbuf.size())-1 && int(tbuf[cur_entry+1] & FloppyDisk::TIME_MASK) < cur_pos) {
		cur_entry++;
	}

	int period = samplelength;
	int period_adjust_base = period * 0.05;

	int min_period = int(samplelength * 0.75);
	int max_period = int(samplelength * 1.25);
	int phase_adjust = 0;
	int freq_hist = 0;
	int next = 0;

	// Prepare offset for the format storage
	int offset = 0x100; // side 1
	if(head == 0) {
		offset = 0;
	}

	uint8_t bit = 0x01;
	uint8_t current = 0;

	while (next < 200'000'000) {
		int edge = tbuf[cur_entry] & FloppyDisk::TIME_MASK;

		// Start of track? Use next entry.
		if(edge == 0)
		{
			edge = tbuf[++cur_entry] & FloppyDisk::TIME_MASK;
		}

		// Wrapped over end?
		if(edge < cur_pos) {
			edge += 200'000'000;
		}

		// End of cell
		next = cur_pos + period + phase_adjust;

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

		cur_pos = next;

		bit = (bit << 1) & 0xff;
		if (bit == 0)
		{
			bit = 0x01;
			cylinder_buffer[offset++] = current;
			track_bytes++;
			if ((offset & 0xff) == 0) {
				offset += 0x100;
			}
			current = 0;
		}

		// Fast-forward to next cell
		while (cur_entry < int(tbuf.size())-1 && int(tbuf[cur_entry] & FloppyDisk::TIME_MASK) < cur_pos) {
			cur_entry++;
		}

		// Reaching the end of the track
		if (cur_entry == int(tbuf.size())-1 && int(tbuf[cur_entry] & FloppyDisk::TIME_MASK) < cur_pos)
		{
			// Wrap to index 0 or 1 depending on whether there is a transition exactly at the index hole
			cur_entry = (tbuf[int(tbuf.size())-1] & FloppyDisk::MG_MASK) != (tbuf[0] & FloppyDisk::MG_MASK) ?
			            0 : 1;
		}
	}
	// Write the current byte when not done
	if (bit != 0x01) {
		cylinder_buffer[offset] = current;
	}
}