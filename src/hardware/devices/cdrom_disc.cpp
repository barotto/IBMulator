/*
 * Copyright (C) 2024  Marco Bortolin
 * Copyright (C) 2019-2023  The DOSBox Staging Team
 * Copyright (C) 2002-2021  The DOSBox Team
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
#include "filesys.h"
#include "cdrom_disc.h"
#include "utils.h"



void TMSF::from_frames(uint32_t frames)
{
	fr = frames % REDBOOK_FRAMES_PER_SECOND;
	frames /= REDBOOK_FRAMES_PER_SECOND;
	sec = frames % 60;
	frames /= 60;
	min = static_cast<uint8_t>(frames);
}

uint32_t TMSF::to_frames()
{
	return
		(uint32_t(min) * 60u * REDBOOK_FRAMES_PER_SECOND) +
		(uint32_t(sec) * REDBOOK_FRAMES_PER_SECOND) +
		uint32_t(fr);
}

void CdRomDisc_Image::load(std::string _path)
{
	std::string base, ext;
	FileSys::get_file_parts(_path.c_str(), base, ext);
	ext = str_to_lower(ext);
	if(ext == ".iso") {
		load_iso(_path);
	} else {
		throw std::runtime_error(str_format("'%s' is not a valid format extension.", ext.c_str()));
	}
}

void CdRomDisc_Image::BinaryFile::load(std::string _path)
{
	m_file = FileSys::make_ifstream(_path.c_str(), std::ios::in|std::ios::binary);
	if(m_file.fail()) {
		throw std::runtime_error(str_format("cannot open file: '%s'.", _path.c_str()));
	}
	FileSys::get_file_stats(_path.c_str(), &m_length, nullptr);
}

void CdRomDisc_Image::load_iso(std::string _path)
{
	// data track (track 1)
	Track track = {};
	track.number = 1;
	track.file = std::make_shared<BinaryFile>();
	track.file->load(_path); // will throw in case of error
	track.attr = 0x40; // data

	// try to detect the iso type
	if(can_read_PVD(track.file.get(), BYTES_PER_MODE1_DATA, false)) {
		track.sectorSize = BYTES_PER_MODE1_DATA;
		track.mode2 = false;
	} else if(can_read_PVD(track.file.get(), BYTES_PER_RAW_REDBOOK_FRAME, false)) {
		track.sectorSize = BYTES_PER_RAW_REDBOOK_FRAME;
		track.mode2 = false;
	} else if(can_read_PVD(track.file.get(), BYTES_PER_MODE2_DATA, true)) {
		track.sectorSize = BYTES_PER_MODE2_DATA;
		track.mode2 = true;
	} else if(can_read_PVD(track.file.get(), BYTES_PER_RAW_REDBOOK_FRAME, true)) {
		track.sectorSize = BYTES_PER_RAW_REDBOOK_FRAME;
		track.mode2 = true;
	} else {
		throw std::runtime_error(str_format("'%s' is not a valid ISO image file.", _path.c_str()));
	}

	track.length = track.file->length() / track.sectorSize;

	PDEBUGF(LOG_V0, LOG_HDD, "CD-ROM: ISO file parsed '%s': tracks=1, attr=0x%02x, sectorSize=%u, mode2=%u\n",
		_path.c_str(), track.attr, track.sectorSize, track.mode2);

	m_tracks.push_back(track);

	// lead-out track (track 2)
	Track leadout_track = {};
	leadout_track.number = 2;
	leadout_track.start = track.length;
	m_tracks.push_back(leadout_track);
}

bool CdRomDisc_Image::can_read_PVD(TrackFile *_file, uint16_t _sector_size, bool _mode2)
{
	// can we read a Primary Volume Descriptor?
	assert(_file);

	// Initialize our array in the event file->read() doesn't fully write it
	uint8_t pvd[BYTES_PER_MODE1_DATA] = {0};
	uint32_t seek = 16 * _sector_size;  // first vd is located at sector 16
	if(_sector_size == BYTES_PER_RAW_REDBOOK_FRAME && !_mode2) {
		seek += 16; // SYNC + HDR
	}
	if(_mode2) {
		// Mode 2 XA CD-ROM
		seek += 24;  // SYNC + HDR + 8 bytes for the sub-header
	}
	_file->read(pvd, seek, BYTES_PER_MODE1_DATA);
	// pvd[0] = descriptor type, pvd[1..5] = standard identifier,
	// pvd[6] = iso version (+8 for High Sierra)
	return ((pvd[0] == 1 && !strncmp((char*)(&pvd[1]), "CD001", 5) && pvd[6]  == 1) ||
	        (pvd[8] == 1 && !strncmp((char*)(&pvd[9]), "CDROM", 5) && pvd[14] == 1));
}

bool CdRomDisc_Image::read_sector(uint8_t *_buffer, uint32_t _sector, uint32_t _bytes)
{
	auto track = get_track(_sector);

	// Guard: Bail if the requested sector fell outside our tracks
	if(track == m_tracks.end() || !track->file) {
		PDEBUGF(LOG_V0, LOG_HDD, "CD-ROM: read_sector: %u is on an invalid track\n", _sector);
		return false;
	}

	uint32_t offset = track->skip + (_sector - track->start) * track->sectorSize;

	bool is_raw = (_bytes == BYTES_PER_RAW_REDBOOK_FRAME);

	if(track->sectorSize != BYTES_PER_RAW_REDBOOK_FRAME && is_raw) {
		// TODO
		PDEBUGF(LOG_V0, LOG_HDD, "CD-ROM: read_sector: track=%2d, raw=%u, sector=%ld, bytes=%d [failed: RAW requested]\n",
				track->number, is_raw, _sector, _bytes);
		return false;
	}
	if(track->sectorSize == BYTES_PER_RAW_REDBOOK_FRAME && !track->mode2 && !is_raw) {
		offset += 16; // SYNC + HDR
	}
	if(track->mode2 && !is_raw) {
		// Mode 2 XA CD-ROM
		offset += 24; // SYNC + HDR + 8 bytes for the sub-header
	}

	PDEBUGF(LOG_V0, LOG_HDD, "CD-ROM: read_sector: track=%2d, raw=%u, sector=%ld, bytes=%d\n",
			track->number, is_raw, _sector, _bytes);

	return track->file->read(_buffer, offset, _bytes);
}

bool CdRomDisc_Image::BinaryFile::read(uint8_t *_buffer, uint32_t _offset, uint32_t _bytes)
{
	assert(_buffer);
	assert(_offset <= MAX_REDBOOK_BYTES);
	assert(_bytes <= MAX_REDBOOK_BYTES);

	if(_offset >= m_length) {
		_bytes = 0;
	} else if(_offset + _bytes > m_length) {
		_bytes = m_length - _offset;
	}

	if(_bytes == 0) {
		return true;
	}

	if(!seek(_offset)) {
		return false;
	}

	m_file.read((char *)_buffer, _bytes);
	return m_file.fail();
}

bool CdRomDisc_Image::BinaryFile::seek(uint32_t _offset)
{
	assert(_offset <= MAX_REDBOOK_BYTES);

	if(_offset >= m_length) {
		PDEBUGF(LOG_V0, LOG_HDD, "CD-ROM: seek: offset=%u beyond the disc size\n", _offset);
		return false;
	}

	m_file.seekg(_offset, std::ios::beg);
	return !m_file.fail();
}

CdRomDisc_Image::TrackIterator CdRomDisc_Image::get_track(uint32_t _sector)
{
	// Guard if we have no tracks or the sector is beyond the lead-out
	if(_sector > MAX_REDBOOK_SECTOR || m_tracks.size() < MIN_REDBOOK_TRACKS ||
			_sector >= m_tracks.back().start)
	{
		PDEBUGF(LOG_V0, LOG_HDD, "CD-ROM: get_track: sector %u outside range\n", _sector);
		return m_tracks.end();
	}

	// Walk the tracks checking if the desired sector falls inside of a given
	// track's range, which starts at the end of the prior track and goes to
	// the current track's (start + length).
	TrackIterator track = m_tracks.begin();
	uint32_t lower_bound = track->start;
	while(track != m_tracks.end()) {
		const uint32_t upper_bound = track->start + track->length;
		if(lower_bound <= _sector && _sector < upper_bound) {
			break;
		}
		++track;
		lower_bound = upper_bound;
	}

	if(track != m_tracks.end() && track->number != 1) {
		if(_sector < track->start) {
			PDEBUGF(LOG_V0, LOG_HDD, "CD-ROM: get_track: sector %d => in the pregap of track %d [pregap %d, start %d, end %d]\n",
					_sector, track->number, prev(track)->start - prev(track)->length,
					track->start, track->start + track->length);
		} else {
			PDEBUGF(LOG_V0, LOG_HDD, "CD-ROM: GetTrack at sector %d => track %d [start %d, end %d]\n",
					_sector, track->number, track->start, track->start + track->length);
		}
	}

	return track;
}

bool CdRomDisc_Image::get_audio_tracks(uint8_t &start_track_num, uint8_t &end_track_num, TMSF &lead_out_msf)
{
	// Guard: A valid CD has atleast two tracks: the first plus the lead-out,
	// so bail out if we have fewer than 2 tracks
	if(m_tracks.size() < MIN_REDBOOK_TRACKS) {
		PERRF(LOG_HDD, "CD-ROM: get_audio_tracks(): too few tracks: %u\n", m_tracks.size());
		return false;
	}
	start_track_num = m_tracks.front().number;
	end_track_num = next(m_tracks.crbegin())->number; // next(crbegin) == [vec.size - 2]
	lead_out_msf.from_frames(m_tracks.back().start + REDBOOK_FRAME_PADDING);

	PDEBUGF(LOG_V0, LOG_HDD, "CD-ROM: get_audio_tracks(): start track=%02d, last playable=%02d, lead-out=%02d:%02d:%02d\n",
			start_track_num, end_track_num, lead_out_msf.min, lead_out_msf.sec, lead_out_msf.fr);

	return true;
}

bool CdRomDisc_Image::get_audio_track_info(uint8_t _track, TMSF &start_, unsigned char &attr_)
{
	if(m_tracks.size() < MIN_REDBOOK_TRACKS || _track < 1 || _track > 99 || _track >= m_tracks.size()) {
		PDEBUGF(LOG_V2, LOG_HDD,
			"CD-ROM: get_audio_track_info: track %u outside the CD's track range [1 to %u)\n",
			_track, m_tracks.size());
		return false;
	}

	const int requested_track_index = static_cast<int>(_track) - 1;
	TrackIterator track = m_tracks.begin() + requested_track_index;
	start_.from_frames(track->start + REDBOOK_FRAME_PADDING);
	attr_ = track->attr;

	PDEBUGF(LOG_V2, LOG_HDD,
		"CD-ROM: get_audio_track_info: track %u => MSF %02d:%02d:%02d, which is sector %d\n",
		_track, start_.min, start_.sec, start_.fr, start_.to_frames());

	return true;
}

bool CdRomDisc_Image::get_audio_sub(uint8_t &attr_, uint8_t &track_num_, uint8_t &index_, TMSF &relPos_, TMSF &absPos_)
{
	// Setup valid defaults to handle all scenarios
	attr_ = 0;
	track_num_ = 1;
	index_ = 1;
	uint32_t absolute_sector = 0;
	uint32_t relative_sector = 0;

	if(!m_tracks.empty()) {
		// the first track is valid
		auto track = m_tracks.begin();

		if(false) {
			// TODO current audio track
		} else {
			// TODO search for the first audio track
		}

		attr_ = track->attr;
		track_num_ = track->number;
	}

	absPos_.from_frames(absolute_sector + REDBOOK_FRAME_PADDING);
	relPos_.from_frames(relative_sector);

	return true;
}

CdRomDisc_Image::Type CdRomDisc_Image::type() const
{
	if(m_tracks.empty()) {
		return TYPE_ERROR;
	}
	unsigned type = m_tracks.begin()->attr == 0x40;
	for(auto & track : m_tracks) {
		if(track.attr == 0) {
			type |= TYPE_CDDA_AUDIO;
			break;
		}
	}
	return static_cast<Type>(type);
}