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

#ifndef IBMULATOR_HW_CDROM_DISC_H
#define IBMULATOR_HW_CDROM_DISC_H

#include <cmath>
#include <future>
#include "mediaimage.h"
#include "audio/decoders/SDL_sound.h"
#include "utils.h"

// CD-ROM data and audio format constants
#define BYTES_PER_MODE1_DATA           2048u
#define BYTES_PER_MODE2_DATA           2336u
#define BYTES_PER_RAW_REDBOOK_FRAME    2352u
#define REDBOOK_FRAMES_PER_SECOND        75u
#define REDBOOK_CHANNELS                  2u
#define REDBOOK_BPS                       2u // bytes per sample
#define REDBOOK_PCM_FRAMES_PER_SECOND 44100u // also CD Audio sampling rate
#define REDBOOK_FRAME_PADDING           150u // The relationship between High Sierra sectors and Redbook
                                             // frames is described by the equation:
                                             // Sector = Minute * 60 * 75 + Second * 75 + Frame - 150
#define MAX_REDBOOK_FRAMES          1826091u // frames are Redbook's data unit
#define MAX_REDBOOK_SECTOR          1826090u // a sector is the index to a frame
#define MAX_REDBOOK_TRACKS               99u // a CD can contain 99 playable tracks plus the remaining leadout
#define MIN_REDBOOK_TRACKS                2u // One track plus the lead-out track
#define REDBOOK_PCM_BYTES_PER_MS      176.4f // 44.1 frames/ms * 4 bytes/frame
#define REDBOOK_PCM_BYTES_PER_MIN  10584000u // 44.1 frames/ms * 4 bytes/frame * 1000 ms/s * 60 s/min
#define BYTES_PER_REDBOOK_PCM_FRAME       4u // 2 bytes/sample * 2 samples/frame
#define AUDIO_DECODE_BUFFER_SIZE      88200u // 0.5 sec * 44100 * 4
#define MAX_REDBOOK_BYTES (MAX_REDBOOK_FRAMES * BYTES_PER_RAW_REDBOOK_FRAME) // length of a CDROM in bytes
#define MAX_REDBOOK_DURATION_MS (99 * 60 * 1000) // 99 minute CD-ROM in milliseconds
#define PCM_FRAMES_PER_REDBOOK_FRAME    588u // BYTES_PER_RAW_REDBOOK_FRAME / BYTES_PER_REDBOOK_PCM_FRAME

struct TMSF
{
	uint8_t min;
	uint8_t sec;
	uint8_t fr;

	// Logical addresses have an offset of 00/02/00 (150 frames).
	// The lead-in area (track 0) and the initial 150 sector pre-gap
	// are not accessible with logical addressing.

	TMSF() : min(0), sec(0), fr(0) {}
	TMSF(int64_t _frames, unsigned _offset = REDBOOK_FRAME_PADDING) {
		from_frames(_frames, _offset);
	}
	TMSF(uint8_t _msf[3]) {
		from_array(_msf);
	}

	void from_frames(int64_t _frames, unsigned _offset = REDBOOK_FRAME_PADDING);
	void from_array(uint8_t _msf[3]);
	int64_t to_frames(unsigned _offset = REDBOOK_FRAME_PADDING);
	std::string to_string() const {
		return str_format("%02u:%02u:%02u", min, sec, fr);
	}
};

class CdRomDisc
{
public:
	enum Type {
		TYPE_UNKNOWN = 0x00,
		TYPE_DATA = 0x01,
		TYPE_AUDIO = 0x02,
		TYPE_ERROR = 0x72
	};
	enum Decode {
		DECODE_EOF = 0,
		DECODE_ERROR = -1,
		DECODE_NOT_READY = -2
	};
	/* For timings we force a CD-ROM into a CHS geometry.
	 * This is mostly nonsense, but allows to reuse old HDD code and it's good enough.
	 * Track width: 2.1um (0.0021mm): 0.5um wide + 1.6um pitch
	 * Radius: outer=58mm, inner=25mm (650MB disc)
	 * Net available program area height: 33mm (650MB disc)
	 * Circumference (program area width): 58*2*3.14159 = 364.42444mm
	 * Tracks per program area height: 33 / 0.0021 = 15714.28
	 * Max sectors: 333000 (650MB disc)
	 * Sectors per track: 333000 / 15714.28 = 21.19 (22)
	 * +------------------+
	 * |       364mm      |
	 * |33mm              |
	 * |                  |
	 * +------------------+
	 * "CDs always store data at the same density on a single-sided disc. The
	 * capacity varies only by how closely the outward-bound spiral data track
	 * approaches the disc's outer rim. Most CD-ROMs settle for a conservative
	 * 553 MB. Some discs stretch it to 682 MB by living close to the edge.
	 * It's risky because the outer region of a disc is more susceptible to
	 * defects, and some drives have trouble reading the longer track."
	 * https://www.halfhill.com/byte/1996-10_cds.html
	 */
	static constexpr double max_sectors = 333000.0;
	static constexpr unsigned sectors_per_track = 22;
	static constexpr double track_width_mm = 0.0021;
	static constexpr double max_tracks = std::ceil(max_sectors / sectors_per_track);

	class TrackFile {
	protected:
		uint64_t m_length = 0; // bytes
		size_t m_audio_pos = MAX_REDBOOK_BYTES; // byte position for audio decoding
		std::atomic<bool> m_seeking = false;
		std::future<bool> m_seek_result;

	public:
		virtual ~TrackFile() {}

		size_t length() const { return m_length; }

		virtual uint32_t rate() const = 0;
		virtual uint8_t channels() const = 0;

		virtual void load(std::string _path) = 0;
		virtual bool read(uint8_t *_buffer, uint32_t _offset, uint32_t _bytes) = 0;
		virtual bool seek(uint32_t _offset, bool _async) = 0;
		virtual bool is_seeking() { return m_seeking; }
		// returns number of decoded PCM frames, can be lower than _pcm_frames
		virtual int decode(uint8_t *_buffer, uint32_t _pcm_frames) = 0;
		virtual void dispose() {}
	};

	struct Track {
		enum Type {
			AUDIO = 0x00,
			DATA = 0x40
		};
		std::shared_ptr<TrackFile> file;
		uint32_t start = 0; // the logical start sector
		uint32_t length = 0; // size in logical sectors
		uint32_t skip = 0; // byte offset on the file
		uint16_t sector_size = 0; // the byte size of the logical sectors
		uint8_t number = 0; // track number 1-based (track 0 is unaccessible)
		uint8_t attr = 0; // attributes (DATA track = 0x40)
		bool mode2 = false;

		bool is_data() const { return file && attr == Type::DATA; }
		bool is_audio() const { return file && attr == Type::AUDIO; }
		int64_t sector_to_byte(int64_t _sector);
		uint32_t start_sector() const { return start; }
		uint32_t end_sector() const { return start + length; }
		TMSF start_msf() const { return TMSF(start_sector(),0); }
		TMSF end_msf() const { return TMSF(end_sector(),0); }
		TMSF length_msf() const { return TMSF(length,0); }
		uint64_t length_bytes() const { return uint64_t(length) * sector_size; }
	};

	using Tracks = std::vector<Track>;
	using TrackIterator = std::vector<Track>::iterator;

private:
	class BinaryFile : public TrackFile {
	private:
		std::ifstream m_file;

	public:
		uint32_t rate() const { return 44100; }
		uint8_t channels() const { return 2; }

		void load(std::string _path);
		bool read(uint8_t *_buffer, uint32_t _offset, uint32_t _bytes);
		bool seek(uint32_t _offset, bool _async);
		int decode(uint8_t *_buffer, uint32_t _pcm_frames);
	};

	class AudioFile : public TrackFile {
	private:
		Sound_Sample *m_file = nullptr; // source SDL_sound file info for decoding
		uint32_t m_decoded_bytes = 0; // available decoded audio bytes (format: 16bit,2ch,44.1KHz)
		uint8_t *m_decoded_ptr = nullptr; // pointer to decoded audio

	public:
		uint32_t rate() const {
			return m_file ? m_file->actual.rate : 0;
		}
		uint8_t channels() const {
			return m_file ? m_file->actual.channels : 0;
		}

		void load(std::string _path);
		bool read(uint8_t *_buffer, uint32_t _offset, uint32_t _bytes);
		bool seek(uint32_t _offset, bool _async);
		int decode(uint8_t *_buffer, uint32_t _pcm_frames);
		void dispose();
	};

	std::vector<Track> m_tracks;
	std::vector<uint8_t> m_readBuffer;
	std::string m_mcn; // Media Catalogue Number

	MediaGeometry m_geometry;
	uint32_t m_sectors = 0;
	double m_radius = .0;

public:
	void load(std::string _path);
	void dispose();
	bool get_tracks_info(uint8_t &start_track_num, uint8_t &end_track_num, TMSF &lead_out_msf);
	bool get_track_info(uint8_t _track, TMSF &start_, unsigned char &attr_);
	bool read_sector(uint8_t *_buffer, uint32_t _sector, uint32_t _bytes);
	Tracks & tracks() { return m_tracks; }
	TrackIterator get_track(uint32_t _sector);
	unsigned tracks_count() const { return m_tracks.empty() ? 0 : m_tracks.size() - 1; }

	Type type() const;
	const MediaGeometry & geometry() const { return m_geometry; }
	uint32_t sectors() const { return m_sectors; }
	double radius() const { return m_radius; }

	const std::string & MCN() const { return m_mcn; }

	static std::vector<const char *> get_compatible_file_extensions() {
		return {".iso", ".cue"};
	}

private:
	void load_iso(std::string _path);
	bool can_read_PVD(TrackFile *_file, uint16_t _sector_size, bool _mode2);
	void load_cue(std::string _path);
	bool parse_cue_keyword(std::istream &_in, std::string &keyword_);
	bool parse_cue_string(std::istream &_in, std::string &str_);
	bool parse_cue_frame(std::istream &_in, uint32_t &frames_);
	bool add_track(Track &curr_, uint32_t &shift_, const int32_t _prestart,
			uint32_t &totalPregap_, uint32_t _currPregap);
};

#endif