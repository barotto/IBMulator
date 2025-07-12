// license:BSD-3-Clause
// copyright-holders:Olivier Galibert, Marco Bortolin

// Based on MAME's "lib/formats/ipf_dsk.h"

#ifndef IBMULATOR_HW_FLOPPYFMT_IPF_H
#define IBMULATOR_HW_FLOPPYFMT_IPF_H

#include "floppyfmt.h"
#include "floppydisk_raw.h"

class FloppyFmt_IPF : public FloppyFmt
{
public:
	const char *name() const { return "IPF"; }
	const char *description() const { return "SPS IPF (*.ipf)"; }
	const char *default_file_extension() const { return ".ipf"; }
	std::vector<const char *> file_extensions() const { return {".ipf"}; }
	bool can_save() const  { return false; }
	FloppyFmt *create() const { return new FloppyFmt_IPF(); }

	FloppyDisk::Properties identify(std::string file_path, uint64_t file_size,
			FloppyDisk::Size _disk_size);

	bool load(std::ifstream &_file, FloppyDisk &_disk);

	MediumInfoData get_preview_string(std::string _filepath);

private:
	bool load_raw(std::ifstream &_file, FloppyDisk_Raw &_disk);
	bool load_flux(std::ifstream &_file, FloppyDisk &_disk);

	struct IPFDecode {
		struct IPFInfo {                   // INFO
			uint32_t type = 0;             // 12+0=12 mediaType
			uint32_t encoder_type = 0;     // 12+4=16 encoderType
			uint32_t encoder_revision = 0; // 12+8=20 encoderRev
			uint32_t release = 0;          // 12+12=24 fileKey
			uint32_t revision = 0;         // 12+16=28 fileRev
			uint32_t origin = 0;           // 12+20=32 origin
			uint32_t min_cylinder = 0;     // 12+24=36 minTrack
			uint32_t max_cylinder = 0;     // 12+28=40 maxTrack
			uint32_t min_head = 0;         // 12+32=44 minSide
			uint32_t max_head = 0;         // 12+36=48 maxSide
			uint32_t credit_day = 0;       // 12+40=52 creationDate year*1e4 + month*1e2 + day
			uint32_t credit_time = 0;      // 12+44=56 creationTime hour*1e7 + min*1e5 + sec*1e3 + msec
			uint32_t platform[4] = {};     // 12+48=60 platforms
			uint32_t disk_num = 0;         // 12+64=76 diskNumber
			uint32_t creator = 0;          // 12+68=80 creatorId
			uint32_t extra[3] = {0,0,0};   // 12+72=84 reserved
		} info;

		struct TrackInfo {                   // IMGE
			uint32_t cylinder = 0;           // 12+0=12  track
			uint32_t head = 0;               // 12+4=16  side
			uint32_t type = 0;               // 12+8=20  density 
			uint32_t sigtype = 0;            // 12+12=24 signalType
			uint32_t size_bytes = 0;         // 12+16=28 trackBytes
			uint32_t index_bytes = 0;        // 12+20=32 startBytePos
			uint32_t index_cells = 0;        // 12+24=36 startBitPos
			uint32_t datasize_cells = 0;     // 12+28=40 dataBits
			uint32_t gapsize_cells = 0;      // 12+32=44 gapBits
			uint32_t size_cells = 0;         // 12+36=48 trackBits (cell count)
			uint32_t block_count = 0;        // 12+40=52 blockCount
			uint32_t process = 0;            // 12+44=56 encoderProcess
			uint32_t weak_bits = 0;          // 12+48=60 trackFlags
			uint32_t data_key = 0;           // 12+52=64 dataKey
			uint32_t reserved[3] = {0,0,0};  // 12+56=68 reserved

			                               // DATA
			uint32_t data_size = 0;        // 12+0=12 length
			uint32_t data_size_bits = 0;   // 12+4=16 bitSize
			const uint8_t *data = nullptr; // 12+16=28 Extra Data Block

			bool info_set = false;
			bool has_weak_cells = false;
		};

		std::vector<TrackInfo> tinfos;
		uint32_t tcount = 0;

		uint32_t crc32r(const uint8_t *data, uint32_t size);

		bool parse(std::vector<uint8_t> &data, FloppyDisk *image);
		bool parse_info(const uint8_t *info);
		bool parse_imge(const uint8_t *imge, TrackInfo &_track);
		bool parse_data(const uint8_t *data, uint32_t &pos, uint32_t max_extra_size);

		bool scan_one_tag(std::vector<uint8_t> &data, uint32_t &pos, uint8_t *&tag, uint32_t &tsize);
		bool scan_all_tags(std::vector<uint8_t> &data);

		static uint32_t rb(const uint8_t *&p, int count);
		static uint32_t r32(const uint8_t *p);

		TrackInfo *get_index(uint32_t idx);

		void track_write_raw(std::vector<uint32_t>::iterator &tpos, const uint8_t *data, uint32_t cells, bool &context);
		void track_write_mfm(std::vector<uint32_t>::iterator &tpos, const uint8_t *data, uint32_t start_offset, uint32_t patlen, uint32_t cells, bool &context);
		void track_write_weak(std::vector<uint32_t>::iterator &tpos, uint32_t cells);
		bool generate_block_data(TrackInfo *t, const uint8_t *data, const uint8_t *dlimit, std::vector<uint32_t>::iterator tpos, std::vector<uint32_t>::iterator tlimit, bool &context);

		bool gap_description_to_reserved_size(const uint8_t *&data, const uint8_t *dlimit, uint32_t &res_size);
		bool generate_gap_from_description(const uint8_t *&data, const uint8_t *dlimit, std::vector<uint32_t>::iterator tpos, uint32_t size, bool pre, bool &context);
		bool generate_block_gap_0(uint32_t gap_cells, uint8_t pattern, uint32_t &spos, uint32_t ipos, std::vector<uint32_t>::iterator &tpos, bool &context);
		bool generate_block_gap_1(uint32_t gap_cells, uint32_t &spos, uint32_t ipos, const uint8_t *data, const uint8_t *dlimit, std::vector<uint32_t>::iterator &tpos, bool &context);
		bool generate_block_gap_2(uint32_t gap_cells, uint32_t &spos, uint32_t ipos, const uint8_t *data, const uint8_t *dlimit, std::vector<uint32_t>::iterator &tpos, bool &context);
		bool generate_block_gap_3(uint32_t gap_cells, uint32_t &spos, uint32_t ipos, const uint8_t *data, const uint8_t *dlimit, std::vector<uint32_t>::iterator &tpos, bool &context);
		bool generate_block_gap(uint32_t gap_type, uint32_t gap_cells, uint8_t pattern, uint32_t &spos, uint32_t ipos, const uint8_t *data, const uint8_t *dlimit, std::vector<uint32_t>::iterator tpos, bool &context);

		bool generate_block(TrackInfo *t, uint32_t idx, uint32_t ipos, std::vector<uint32_t> &track, uint32_t &pos, uint32_t &dpos, uint32_t &gpos, uint32_t &spos, bool &context);
		uint32_t block_compute_real_size(TrackInfo *t);

		void timing_set(std::vector<uint32_t> &track, uint32_t start, uint32_t end, uint32_t time);
		bool generate_timings(TrackInfo *t, std::vector<uint32_t> &track, const std::vector<uint32_t> &data_pos, const std::vector<uint32_t> &gap_pos);

		void rotate(std::vector<uint32_t> &track, uint32_t offset, uint32_t size);
		void mark_track_splice(std::vector<uint32_t> &track, uint32_t offset, uint32_t size);
		bool generate_track(TrackInfo *t, FloppyDisk *image);
		bool generate_tracks(FloppyDisk *image);
	} m_ipf;
};

#endif
