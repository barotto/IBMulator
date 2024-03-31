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
#define AUDIO_DECODE_BUFFER_SIZE      16512u
#define MAX_REDBOOK_BYTES (MAX_REDBOOK_FRAMES * BYTES_PER_RAW_REDBOOK_FRAME) // length of a CDROM in bytes
#define MAX_REDBOOK_DURATION_MS (99 * 60 * 1000) // 99 minute CD-ROM in milliseconds

struct TMSF
{
	uint8_t min;
	uint8_t sec;
	uint8_t fr;

	void from_frames(uint32_t);
	uint32_t to_frames();
};

class CdRomDisc
{
public:
	enum Type {
		TYPE_CDROM_DATA = 0x01,
		TYPE_CDDA_AUDIO = 0x02,
		TYPE_CDROM_DATA_AUDIO = 0x03,
		TYPE_ERROR = 0x72
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

public:
	virtual ~CdRomDisc() {}

	virtual void load(std::string _path) = 0;
	virtual bool get_audio_tracks(uint8_t &start_track_num, uint8_t &end_track_num, TMSF &lead_out_msf) = 0;
	virtual bool get_audio_track_info(uint8_t _track, TMSF &start_, unsigned char &attr_) = 0;
	virtual bool get_audio_sub(uint8_t &attr_, uint8_t &track_, uint8_t &index_, TMSF &relPos_, TMSF &absPos_) = 0;
	virtual bool read_sector(uint8_t *_buffer, uint32_t _lba, uint32_t _bytes) = 0;
	virtual unsigned tracks_count() const = 0;
	virtual Type type() const = 0;
};

class CdRomDisc_Image : public CdRomDisc
{
private:
	class TrackFile {
	protected:
		uint64_t m_length = 0;

	public:
		virtual ~TrackFile() {}

		size_t length() const { return m_length; }

		virtual void load(std::string _path) = 0;
		virtual bool read(uint8_t *_buffer, uint32_t _offset, uint32_t _bytes) = 0;
		virtual bool seek(uint32_t _offset) = 0;
	};
	
	class BinaryFile : public TrackFile {
	private:
		std::ifstream m_file;

	public:
		void load(std::string _path);
		bool read(uint8_t *_buffer, uint32_t _offset, uint32_t _bytes);
		bool seek(uint32_t _offset);
	};

	// Nested struct definition
	struct Track {
		std::shared_ptr<TrackFile> file;
		uint32_t start = 0;
		uint32_t length = 0;
		uint32_t skip = 0;
		uint16_t sectorSize = 0;
		uint8_t number = 0;
		uint8_t attr = 0;
		bool mode2 = false;
	};

	using TrackIterator = std::vector<Track>::iterator;

	std::vector<Track> m_tracks;
	std::vector<uint8_t> m_readBuffer;

public:
	void load(std::string _path);
	bool get_audio_tracks(uint8_t &start_track_num, uint8_t &end_track_num, TMSF &lead_out_msf);
	bool get_audio_track_info(uint8_t _track, TMSF &start_, unsigned char &attr_);
	bool get_audio_sub(uint8_t &attr_, uint8_t &track_, uint8_t &index_, TMSF &relPos_, TMSF &absPos_);
	bool read_sector(uint8_t *_buffer, uint32_t _sector, uint32_t _bytes);
	unsigned tracks_count() const { return m_tracks.empty() ? 0 : m_tracks.size() - 1; }
	Type type() const;

	static std::vector<const char *> get_compatible_file_extensions() {
		return {".iso"};
	}

private:
	void load_iso(std::string _path);
	bool can_read_PVD(TrackFile *_file, uint16_t _sector_size, bool _mode2);

	TrackIterator get_track(uint32_t _sector);
};

#endif