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
#include "audio/decoders/SDL_sound.h"
#include "mixer.h"

static constexpr unsigned CUE_MAX_LINE_LEN = 512;
static constexpr unsigned CUE_MAX_FILENAME_LEN = 256;


void TMSF::from_frames(int64_t _frames, unsigned _offset)
{
	_frames += _offset;

	fr = _frames % REDBOOK_FRAMES_PER_SECOND;
	_frames /= REDBOOK_FRAMES_PER_SECOND;
	sec = _frames % 60;
	_frames /= 60;
	min = static_cast<uint8_t>(_frames);
}

void TMSF::from_array(uint8_t _msf[3])
{
	min = _msf[0];
	sec = _msf[1];
	fr = _msf[2];
}

int64_t TMSF::to_frames(unsigned _offset)
{
	int64_t lba = (uint32_t(min) * 60u * REDBOOK_FRAMES_PER_SECOND) +
	              (uint32_t(sec) * REDBOOK_FRAMES_PER_SECOND) +
	              uint32_t(fr);

	if(lba >= _offset) {
		lba -= _offset;
	} else {
		lba = 0;
	}

	return lba;
}

void CdRomDisc::load(std::string _path)
{
	std::string base, ext;
	FileSys::get_file_parts(_path.c_str(), base, ext);
	ext = str_to_lower(ext);
	if(ext == ".iso") {
		load_iso(_path);
	} else if(ext == ".cue") {
		load_cue(_path);
	} else {
		throw std::runtime_error(str_format("invalid format extension: '%s'", ext.c_str()));
	}

	uint8_t first, last;
	TMSF leadOut;
	if(!get_tracks_info(first, last, leadOut)) {
		throw std::runtime_error("cannot get tracks information");
	}

	m_sectors = static_cast<uint32_t>(leadOut.to_frames());

	m_geometry.heads = 1;
	m_geometry.spt = CdRomDisc::sectors_per_track;
	m_geometry.cylinders = std::ceil(double(m_sectors) / CdRomDisc::sectors_per_track);

	m_radius = double(m_geometry.cylinders) * CdRomDisc::track_width_mm;

	for(auto & track : m_tracks) {
		if(track.is_data()) {
			PINFOF(LOG_V1, LOG_HDD, "CD-ROM:   "
				"track %u: DATA (%s/%u), "
				"start sector: %u, end sector: %u, "
				"total sectors: %u (%llu bytes)\n",
				track.number, track.mode2 ? "MODE2" : "MODE1", track.sector_size,
				track.start_sector(), track.end_sector(),
				track.length, track.length_bytes()
			);
		} else if(track.is_audio()) {
			PINFOF(LOG_V1, LOG_HDD, "CD-ROM:   "
				"track %u: AUDIO, "
				"start: %s (%u), end: %s (%u), "
				"total: %s (%u)\n",
				track.number,
				track.start_msf().to_string().c_str(), track.start_sector(),
				track.end_msf().to_string().c_str(), track.end_sector(),
				track.length_msf().to_string().c_str(), track.length
			);
		}
	}
}

void CdRomDisc::dispose()
{
	for(auto & track : m_tracks) {
		if(track.file) {
			track.file->dispose();
		}
	}
}

void CdRomDisc::load_iso(std::string _path)
{
	// data track (track 1)
	Track track = {};
	track.number = 1;
	track.file = std::make_unique<BinaryFile>();
	track.file->load(_path); // will throw in case of error
	track.attr = Track::DATA;

	// try to detect the iso type
	if(can_read_PVD(track.file.get(), BYTES_PER_MODE1_DATA, false)) {
		track.sector_size = BYTES_PER_MODE1_DATA;
		track.mode2 = false;
	} else if(can_read_PVD(track.file.get(), BYTES_PER_RAW_REDBOOK_FRAME, false)) {
		track.sector_size = BYTES_PER_RAW_REDBOOK_FRAME;
		track.mode2 = false;
	} else if(can_read_PVD(track.file.get(), BYTES_PER_MODE2_DATA, true)) {
		track.sector_size = BYTES_PER_MODE2_DATA;
		track.mode2 = true;
	} else if(can_read_PVD(track.file.get(), BYTES_PER_RAW_REDBOOK_FRAME, true)) {
		track.sector_size = BYTES_PER_RAW_REDBOOK_FRAME;
		track.mode2 = true;
	} else {
		throw std::runtime_error(str_format("'%s' is not a valid ISO image file", _path.c_str()));
	}

	track.length = track.file->length() / track.sector_size;

	PDEBUGF(LOG_V0, LOG_HDD, "CD-ROM: ISO file parsed '%s': tracks=1, attr=0x%02x, sectorSize=%u, mode2=%u\n",
		_path.c_str(), track.attr, track.sector_size, track.mode2);

	m_tracks.push_back(track);

	// lead-out track (track 2)
	Track leadout_track = {};
	leadout_track.number = 2;
	leadout_track.start = track.length;
	m_tracks.push_back(leadout_track);
}

bool CdRomDisc::can_read_PVD(TrackFile *_file, uint16_t _sector_size, bool _mode2)
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

void CdRomDisc::load_cue(std::string _path)
{
	Track track;
	uint32_t shift = 0;
	uint32_t curr_pregap = 0;
	uint32_t total_pregap = 0;
	int32_t prestart = -1;
	int track_number;
	bool success;
	bool can_add_track = false;
	std::string cue_dir = FileSys::get_path_dir(_path.c_str());

	auto in = FileSys::make_ifstream(_path.c_str());
	if(in.fail()) {
		throw std::runtime_error("cannot open file");
	}

	PDEBUGF(LOG_V1, LOG_HDD, "CD-ROM: parsing CUE file ...\n");

	while(!in.eof()) {
		// get next line
		char buf[CUE_MAX_LINE_LEN];
		in.getline(buf, CUE_MAX_LINE_LEN);
		if(in.fail() && !in.eof()) {
			// probably a binary file
			throw std::runtime_error("invalid CUE sheet");
		}
		std::istringstream line(buf);
		std::string command;
		parse_cue_keyword(line, command);

		if(command == "TRACK") {
			if(can_add_track) {
				success = add_track(track, shift, prestart, total_pregap, curr_pregap);
			} else {
				success = true;
			}

			track.start = 0;
			track.skip = 0;
			curr_pregap = 0;
			prestart = -1;

			line >> track_number; // (cin) read into a true int first
			track.number = static_cast<uint8_t>(track_number);
			std::string type;
			parse_cue_keyword(line, type);

			if(type == "AUDIO") {
				track.sector_size = BYTES_PER_RAW_REDBOOK_FRAME;
				track.attr = Track::AUDIO;
				track.mode2 = false;
			} else if(type == "MODE1/2048") {
				track.sector_size = BYTES_PER_MODE1_DATA;
				track.attr = Track::DATA;
				track.mode2 = false;
			} else if(type == "MODE1/2352") {
				track.sector_size = BYTES_PER_RAW_REDBOOK_FRAME;
				track.attr = Track::DATA;
				track.mode2 = false;
			} else if(type == "MODE2/2336") {
				track.sector_size = BYTES_PER_MODE2_DATA;
				track.attr = Track::DATA;
				track.mode2 = true;
			} else if(type == "MODE2/2352") {
				track.sector_size = BYTES_PER_RAW_REDBOOK_FRAME;
				track.attr = Track::DATA;
				track.mode2 = true;
			} else {
				success = false;
			}
			can_add_track = true;

			PDEBUGF(LOG_V1, LOG_HDD, "   TRACK %d %s\n", track_number, type.c_str());
		} else if(command == "INDEX") {
			int index;
			line >> index;
			uint32_t frame;
			success = parse_cue_frame(line, frame);

			if(index == 1) {
				track.start = frame;
			} else if(index == 0) {
				prestart = static_cast<int32_t>(frame);
			}
			// ignore other indices

			PDEBUGF(LOG_V1, LOG_HDD, "    INDEX %d, frame: %u\n", index, frame);
		} else if(command == "FILE") {
			if(can_add_track) {
				success = add_track(track, shift, prestart, total_pregap, curr_pregap);
			} else {
				success = true;
			}
			can_add_track = false;

			std::string filename;
			parse_cue_string(line, filename);
			filename = cue_dir + "/" + filename;
			filename = FileSys::realpath(filename.c_str());
			std::string type;
			parse_cue_keyword(line, type);

			if(type == "BINARY") {
				track.file = std::make_shared<BinaryFile>();
			} else {
				track.file = std::make_shared<AudioFile>();
			}

			PDEBUGF(LOG_V1, LOG_HDD, "  FILE %s %s\n", filename.c_str(), type.c_str());

			track.file->load(filename);
		} else if(command == "PREGAP") {
			success = parse_cue_frame(line, curr_pregap);

			PDEBUGF(LOG_V1, LOG_HDD, "  PREGAP %u\n", curr_pregap);
		} else if (command == "CATALOG") {
			success = parse_cue_string(line, m_mcn);

			PDEBUGF(LOG_V1, LOG_HDD, "  CATALOG %s\n", m_mcn.c_str());
		} else if(command == "CDTEXTFILE" || command == "FLAGS"   || command == "ISRC" ||
		          command == "PERFORMER"  || command == "POSTGAP" || command == "REM" ||
		          command == "SONGWRITER" || command == "TITLE"   || command.empty())
		{
			// ignored commands
			if(!command.empty()) {
				PDEBUGF(LOG_V1, LOG_HDD, "  %s (ignored)\n", command.c_str());
			}
			success = true;
		} else {
			PDEBUGF(LOG_V1, LOG_HDD, "  unknown command: %s\n", command.c_str());
			success = false;
		}
		if(!success) {
			throw std::runtime_error("invalid CUE sheet");
		}
	}

	// add last track
	if(!add_track(track, shift, prestart, total_pregap, curr_pregap)) {
		throw std::runtime_error("cannot add the last track");
	}

	// add lead-out track
	track.number = m_tracks.back().number + 1;
	track.attr = Track::AUDIO; // sync with load iso
	track.start = 0;
	track.length = 0;
	track.file.reset();
	if(!add_track(track, shift, -1, total_pregap, 0)) {
		throw std::runtime_error("cannot add the lead-out track");
	}
}

bool CdRomDisc::parse_cue_keyword(std::istream &_in, std::string &keyword_)
{
	_in >> keyword_;
	keyword_ = str_to_upper(keyword_);
	return true;
}

bool CdRomDisc::parse_cue_string(std::istream &_in, std::string &str_)
{
	auto pos = _in.tellg();
	_in >> str_;
	if(str_[0] == '\"') {
		if(str_[str_.size() - 1] == '\"') {
			str_.assign(str_, 1, str_.size() - 2);
		} else {
			_in.seekg(pos);
			char buffer[CUE_MAX_FILENAME_LEN];
			_in.getline(buffer, CUE_MAX_FILENAME_LEN, '\"'); // skip
			_in.getline(buffer, CUE_MAX_FILENAME_LEN, '\"');
			str_ = buffer;
		}
	}
	return true;
}

bool CdRomDisc::parse_cue_frame(std::istream &_in, uint32_t &frames_)
{
	std::string msf_str;
	_in >> msf_str;
	TMSF msf;
	bool success = sscanf(msf_str.c_str(), "%hhu:%hhu:%hhu", &msf.min, &msf.sec, &msf.fr) == 3;
	// CUE frames are relative and don't have the Redbook absolute offset
	frames_ = msf.to_frames(0);
	return success;
}

bool CdRomDisc::add_track(Track &curr_, uint32_t &shift_, const int32_t _prestart,
		uint32_t &total_pregap_, uint32_t _curr_pregap)
{
	uint32_t skip = 0;

	// frames between index 0(prestart) and 1(curr.start) must be skipped
	if(_prestart >= 0) {
		int start = static_cast<int>(curr_.start);
		if(_prestart > start) {
			PERRF(LOG_HDD, "CD-ROM: add_track: prestart %d cannot be > curr.start %d\n",
			        _prestart, start);
			return false;
		}
		skip = static_cast<uint32_t>(start - _prestart);
	}

	if(m_tracks.empty()) {
		// Add the first track
		assert(curr_.number == 1);
		curr_.skip = skip * curr_.sector_size;
		curr_.start += _curr_pregap;
		total_pregap_ = _curr_pregap;
	} else {
		Track &prev = m_tracks.back();

		if(prev.file == curr_.file) {
			// current track consumes data from the same file as the previous
			curr_.start += shift_;
			if(!prev.length) {
				prev.length = curr_.start + total_pregap_ - prev.start - skip;
			}
			curr_.skip += prev.skip + prev.length * prev.sector_size + skip * curr_.sector_size;
			total_pregap_ += _curr_pregap;
			curr_.start += total_pregap_;
		} else {
			// current track uses a different file as the previous track
			const uint32_t tmp = uint32_t(prev.file->length() - size_t(prev.skip));
			prev.length = tmp / prev.sector_size;
			if(tmp % prev.sector_size != 0) {
				prev.length++; // padding
			}

			curr_.start += prev.start + prev.length + _curr_pregap;
			curr_.skip = skip * curr_.sector_size;
			shift_ += prev.start + prev.length;
			total_pregap_ = _curr_pregap;
		}
		// error checks
		if(curr_.number <= 1 ||
		   prev.number + 1 != curr_.number ||
		   curr_.start < prev.start + prev.length
		)
		{
			PERRF(LOG_HDD, "CD-ROM: add_track: failed consistency checks: "
				"curr_.number (%d) <= 1, "
				"prev.number (%d) + 1 != curr_.number (%d), "
				"curr_.start (%d) < prev.start (%d) + prev.length (%d)\n",
				curr_.number, prev.number, curr_.number,
				curr_.start, prev.start, prev.length
			);
			return false;
		}
	}

	m_tracks.push_back(curr_);

	return true;
}

bool CdRomDisc::read_sector(uint8_t *_buffer, uint32_t _sector, uint32_t _bytes)
{
	auto track = get_track(_sector);

	// Guard: Bail if the requested sector fell outside our tracks
	if(track == m_tracks.end() || !track->file) {
		PDEBUGF(LOG_V0, LOG_HDD, "CD-ROM: read_sector: %u is on an invalid track!\n", _sector);
		return false;
	}

	uint32_t offset = track->skip + (_sector - track->start) * track->sector_size;

	bool is_raw = (_bytes == BYTES_PER_RAW_REDBOOK_FRAME);

	if(track->sector_size != BYTES_PER_RAW_REDBOOK_FRAME && is_raw) {
		// TODO
		PDEBUGF(LOG_V0, LOG_HDD, "CD-ROM: read_sector: track=%2d, raw=%u, sector=%ld, bytes=%d [failed: RAW requested]\n",
				track->number, is_raw, _sector, _bytes);
		return false;
	}
	if(track->sector_size == BYTES_PER_RAW_REDBOOK_FRAME && !track->mode2 && !is_raw) {
		offset += 16; // SYNC + HDR
	}
	if(track->mode2 && !is_raw) {
		// Mode 2 XA CD-ROM
		offset += 24; // SYNC + HDR + 8 bytes for the sub-header
	}

	PDEBUGF(LOG_V1, LOG_HDD, "CD-ROM: read_sector: track=%2d, raw=%u, sector=%ld, bytes=%d\n",
			track->number, is_raw, _sector, _bytes);

	return track->file->read(_buffer, offset, _bytes);
}

int64_t CdRomDisc::Track::sector_to_byte(int64_t _sector)
{
	int64_t sector_offset = _sector - start;
	int64_t byte_offset = skip + sector_offset * sector_size;
	return byte_offset;
}

void CdRomDisc::BinaryFile::load(std::string _path)
{
	// CdRomLoader thread

	m_file = FileSys::make_ifstream(_path.c_str(), std::ios::in|std::ios::binary);
	if(m_file.fail()) {
		throw std::runtime_error(str_format("cannot open file: '%s'", _path.c_str()));
	}
	FileSys::get_file_stats(_path.c_str(), &m_length, nullptr);
	PINFOF(LOG_V1, LOG_HDD, "CD-ROM:   loaded '%s'\n", _path.c_str());
}

bool CdRomDisc::BinaryFile::read(uint8_t *_buffer, uint32_t _offset, uint32_t _bytes)
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

	if(!seek(_offset, false)) {
		return false;
	}

	m_file.read((char *)_buffer, _bytes);
	return m_file.fail();
}

bool CdRomDisc::BinaryFile::seek(uint32_t _offset, bool _async)
{
	// Mixer and Machine threads

	UNUSED(_async);

	// When dealing with CUE/BIN audio tracks, the requested byte position maps
	// one-to-one with the bytes in raw binary image,

	assert(_offset <= MAX_REDBOOK_BYTES);

	if(_offset >= m_length) {
		PDEBUGF(LOG_V0, LOG_HDD, "CD-ROM: seek: offset=%u beyond the disc size.\n", _offset);
		return false;
	}

	m_file.seekg(_offset, std::ios::beg);

	m_audio_pos = _offset;

	return !m_file.fail();
}

int CdRomDisc::BinaryFile::decode(uint8_t *_buffer, uint32_t _req_pcm_frames)
{
	// Mixer thread

	assert(_buffer);
	assert(_req_pcm_frames <= MAX_REDBOOK_FRAMES);
	assert(m_file.is_open() && m_length);

	if(static_cast<size_t>(m_file.tellg()) != m_audio_pos) {
		if(!seek(m_audio_pos, false)) {
			return DECODE_ERROR;
		}
	}

	m_file.read(reinterpret_cast<char*>(_buffer), _req_pcm_frames * BYTES_PER_REDBOOK_PCM_FRAME);

	// Except in the constructors of std::strstreambuf, negative values of std::streamsize are never used.
	const uint32_t bytes_read = static_cast<uint32_t>(m_file.gcount());

	if(bytes_read == 0) {
		if(m_file.eof()) {
			return DECODE_EOF;
		}
		if(m_file.fail()) {
			return DECODE_ERROR;
		}
	}

	m_audio_pos += bytes_read;

	const auto dec_pcm_frames = ceil_udivide(bytes_read, BYTES_PER_REDBOOK_PCM_FRAME);
	PDEBUGF(LOG_V3, LOG_MIXER, "CD-ROM: PCM frames decoded: %u of %u requested.\n", dec_pcm_frames, _req_pcm_frames);

	return dec_pcm_frames;
}

void CdRomDisc::AudioFile::load(std::string _path)
{
	// CdRomLoader thread

	// SDL_Sound first tries using a decoder having a matching
	// registered extension as the filename, and then falls back to
	// trying each decoder before finally giving up.

	Sound_AudioInfo desired = { AUDIO_S16, REDBOOK_CHANNELS, REDBOOK_PCM_FRAMES_PER_SECOND };
	m_file = Sound_NewSampleFromFile(_path.c_str(), &desired, AUDIO_DECODE_BUFFER_SIZE);
	m_audio_pos = 0;
	if(m_file) {
		// Sound_GetDuration returns milliseconds but length()
		// needs to return bytes, so we covert using PCM bytes/s
		const auto track_ms = Sound_GetDuration(m_file);
		const auto track_bytes = static_cast<float>(track_ms) * REDBOOK_PCM_BYTES_PER_MS;
		m_length = static_cast<uint64_t>(track_bytes);

		PINFOF(LOG_V1, LOG_HDD, "CD-ROM:   loaded '%s' [%d Hz, %d-channel, %2.1f minutes]\n",
				_path.c_str(), rate(), channels(),
				length() / static_cast<double>(REDBOOK_PCM_BYTES_PER_MIN));
	} else {
		throw std::runtime_error(str_format("unsupported CD-DA track format: '%s'", _path.c_str()));
	}
}

bool CdRomDisc::AudioFile::read(uint8_t *_buffer, uint32_t _offset, uint32_t _bytes)
{
	// TODO Support DAE?
	UNUSED(_offset);
	std::memset(_buffer, 0, _bytes);
	return true;
}

bool CdRomDisc::AudioFile::seek(uint32_t _byte_offset, bool _async)
{
	// Mixer and Machine threads

	// When dealing with codec-based tracks, we need the codec's help to seek to
	// the equivalent Redbook position within the track, regardless of the
	// track's sampling rate, bit-depth, or number of channels.  To do this,
	// we convert the byte offset to a time-offset, and use the Sound_Seek()
	// function to move the read position.

	assert(m_file);
	assert(_byte_offset < MAX_REDBOOK_BYTES);

	if(m_seeking) {
		return false;
	}

	if(_byte_offset > m_length) {
		return false;
	}

	if(m_audio_pos == _byte_offset) {
		return true;
	}

	if(!(m_file->flags & SOUND_SAMPLEFLAG_CANSEEK)) {
		return false;
	}

	// Convert the position from a byte offset to time offset, in milliseconds.
	const uint32_t pos_in_frames = ceil_udivide(_byte_offset, BYTES_PER_RAW_REDBOOK_FRAME);
	const uint32_t pos_in_ms = ceil_udivide(pos_in_frames * 1000u, REDBOOK_FRAMES_PER_SECOND);

	// Perform the seek and update our position
	bool success = true;
	if(_async) {
		m_seeking = true;
		m_seek_result = std::async(std::launch::async, [=] {
			bool result;
			if(_byte_offset == 0) {
				result = static_cast<bool>(Sound_Rewind(m_file));
			} else {
				result = static_cast<bool>(Sound_Seek(m_file, pos_in_ms));
			}
			// test: std::this_thread::sleep_for(std::chrono::milliseconds(2000));
			m_audio_pos = result ? _byte_offset : MAX_REDBOOK_BYTES;
			m_seeking = false;
			return result;
		});
	} else {
		if(_byte_offset == 0) {
			success = static_cast<bool>(Sound_Rewind(m_file));
		} else {
			success = static_cast<bool>(Sound_Seek(m_file, pos_in_ms));
		}
		m_audio_pos = success ? _byte_offset : MAX_REDBOOK_BYTES;
	}
	m_decoded_bytes = 0;

	return success;
}

int CdRomDisc::AudioFile::decode(uint8_t *_buffer, uint32_t _req_pcm_frames)
{
	// Mixer thread

	assert(_buffer);

	if(!m_seeking && m_seek_result.valid() && !m_seek_result.get()) {
		return DECODE_ERROR;
	} else if(m_seeking) {
		return DECODE_NOT_READY;
	}

	if(m_audio_pos >= MAX_REDBOOK_BYTES) {
		return DECODE_EOF;
	}

	if(m_decoded_bytes == 0) {
		if(m_file->flags & SOUND_SAMPLEFLAG_EOF) {
			return DECODE_EOF;
		}
		if(m_file->flags & SOUND_SAMPLEFLAG_ERROR) {
			return DECODE_ERROR;
		}
	}

	uint32_t needed_bytes = _req_pcm_frames * BYTES_PER_REDBOOK_PCM_FRAME;
	uint32_t bytes_written = 0;

	while(bytes_written < needed_bytes) {
		if(m_decoded_bytes == 0) {
			// if there wasn't previously an error or EOF, read more.
			if((m_file->flags & SOUND_SAMPLEFLAG_ERROR) == 0 &&
			   (m_file->flags & SOUND_SAMPLEFLAG_EOF) == 0
			)
			{
				m_decoded_bytes = Sound_Decode(m_file);
				m_decoded_ptr = static_cast<uint8_t*>(m_file->buffer);
			}
		}
		if(m_decoded_bytes == 0) {
			break;
		}

		if(needed_bytes < bytes_written) {
			throw std::logic_error("CdRomDisc::AudioFile::decode: needed_bytes < bytes_written");
		}
		auto cpysize = needed_bytes - bytes_written;
		if(cpysize > m_decoded_bytes) {
			cpysize = m_decoded_bytes;
		}

		// if it's 0, next iteration will decode more or decide we're done.
		if(cpysize > 0) {
			std::memcpy(_buffer + bytes_written, m_decoded_ptr, cpysize);
			bytes_written += cpysize;
			m_decoded_ptr += cpysize;
			m_decoded_bytes -= cpysize;
		}
	}

	// the decoder might have reached EOF or encountered an error
	// but if something got decoded return that and complain later
	if(bytes_written == 0) {
		if(m_file->flags & SOUND_SAMPLEFLAG_EOF) {
			return DECODE_EOF;
		}
		if(m_file->flags & SOUND_SAMPLEFLAG_ERROR) {
			return DECODE_ERROR;
		}
	}

	m_audio_pos += bytes_written;

	const auto dec_pcm_frames = ceil_udivide(bytes_written, BYTES_PER_REDBOOK_PCM_FRAME);
	PDEBUGF(LOG_V3, LOG_MIXER, "CD-ROM: PCM frames decoded: %u of %u requested (Sound)\n", dec_pcm_frames, _req_pcm_frames);

	return dec_pcm_frames;
}

void CdRomDisc::AudioFile::dispose()
{
	if(m_seek_result.valid()) {
		m_seek_result.wait();
	}
	Sound_FreeSample(m_file);
	m_file = nullptr;
}

CdRomDisc::TrackIterator CdRomDisc::get_track(uint32_t _sector)
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
		if(_sector >= lower_bound && _sector < upper_bound) {
			break;
		}
		++track;
		lower_bound = upper_bound;
	}

	if(track != m_tracks.end() && track->number != 1) {
		if(_sector < track->start) {
			PDEBUGF(LOG_V2, LOG_HDD, "CD-ROM: get_track: sector %d => in the pregap of track %d [pregap %d, start %d, end %d]\n",
					_sector, track->number, prev(track)->start - prev(track)->length,
					track->start, track->start + track->length);
		} else {
			PDEBUGF(LOG_V2, LOG_HDD, "CD-ROM: GetTrack at sector %d => track %d [start %d, end %d]\n",
					_sector, track->number, track->start, track->start + track->length);
		}
	}

	return track;
}

bool CdRomDisc::get_tracks_info(uint8_t &start_track_num_, uint8_t &end_track_num_, TMSF &lead_out_msf_)
{
	// A valid CD has atleast two tracks: the first plus the lead-out
	if(m_tracks.size() < MIN_REDBOOK_TRACKS) {
		PERRF(LOG_HDD, "CD-ROM: too few tracks: %u\n", m_tracks.size());
		return false;
	}
	start_track_num_ = m_tracks.front().number;
	end_track_num_ = next(m_tracks.crbegin())->number; // next(crbegin) == [vec.size - 2]
	lead_out_msf_.from_frames(m_tracks.back().start);

	PDEBUGF(LOG_V1, LOG_HDD, "CD-ROM: get_tracks_info: start track=%02d, last playable=%02d, lead-out=%02d:%02d:%02d\n",
			start_track_num_, end_track_num_, lead_out_msf_.min, lead_out_msf_.sec, lead_out_msf_.fr);

	return true;
}

bool CdRomDisc::get_track_info(uint8_t _track_number, TMSF &start_, unsigned char &attr_)
{
	if(m_tracks.size() < MIN_REDBOOK_TRACKS || _track_number < 1 || _track_number > 99 || _track_number >= m_tracks.size()) {
		PDEBUGF(LOG_V2, LOG_HDD,
			"CD-ROM: get_track_info: track %u outside the CD's track range [1 to %u)\n",
			_track_number, m_tracks.size());
		return false;
	}

	const unsigned requested_track_index = static_cast<unsigned>(_track_number) - 1;
	TrackIterator track = m_tracks.begin() + requested_track_index;
	start_.from_frames(track->start);
	attr_ = track->attr;

	PDEBUGF(LOG_V2, LOG_HDD,
		"CD-ROM: get_track_info: track %u => MSF %02d:%02d:%02d, logical sector %lld\n",
		_track_number, start_.min, start_.sec, start_.fr, start_.to_frames());

	return true;
}

CdRomDisc::Type CdRomDisc::type() const
{
	if(!tracks_count()) {
		return TYPE_ERROR;
	}
	unsigned type = m_tracks.begin()->is_data() ? TYPE_DATA : TYPE_UNKNOWN;
	for(auto & track : m_tracks) {
		if(track.is_audio()) {
			type |= TYPE_AUDIO;
			break;
		}
	}
	return static_cast<Type>(type);
}