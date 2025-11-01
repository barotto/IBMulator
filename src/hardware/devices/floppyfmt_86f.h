// license:BSD-3-Clause
// copyright-holders:Olivier Galibert, Marco Bortolin

// Based on MAME's "lib/formats/86f_dsk.h"

#ifndef IBMULATOR_HW_FLOPPYFMT_86F_H
#define IBMULATOR_HW_FLOPPYFMT_86F_H

#include "floppyfmt.h"
#include "floppydisk_raw.h"

class FloppyFmt_86F : public FloppyFmt
{
private:
	struct Header
	{
		uint8_t headername[4];   // 00-03 Magic 4 bytes ("86BF")
		uint8_t minor_version;   //    04 Minor version
		uint8_t major_version;   //    05 Major version
		uint16_t flags;          // 06-07 Disk flags (16-bit)
		uint32_t firsttrackoffs; //    08 Offsets of tracks
	} GCC_ATTRIBUTE(packed);

	Header m_header = {};

	struct {
		bool surf_desc = false;
		int hole = 0;
		int sides = 1;
		bool write_prot = false;
		double rpm_adjust = 1.0;
		bool bitcell_mode = false;
		int disk_type = 0;
		int zone_type = 0;
		bool big_endian = false;
		bool total_bc_bit = false;
		bool extra_count_is_total = false;
		int tracks_count = 0;
		int image_cylinders = 0;
		bool double_step = false;
	} m_settings;

	std::vector<uint32_t> m_tracklist;

	enum
	{
		SURFACE_DESC = 1,
		TYPE_MASK = 6,
		TYPE_DD = 0,
		TYPE_HD = 2,
		TYPE_ED = 4,
		TYPE_ED2000 = 6,
		TWO_SIDES = 8,
		WRITE_PROTECT = 0x10,
		RPM_MASK = 0x60,
		RPM_0 = 0,
		RPM_1 = 0x20,
		RPM_15 = 0x40,
		RPM_2 = 0x60,
		EXTRA_BC = 0x80,
		ZONED_RPM = 0x100,
		ZONE_MASK = 0x600,
		ZONE_PREA2_1 = 0,
		ZONE_PREA2_2 = 0x200,
		ZONE_A2 = 0x400,
		ZONE_C64 = 0x600,
		ENDIAN_BIG = 0x800,
		RPM_FAST = 0x1000,
		TOTAL_BC = 0x1000
	};

	static constexpr const char * _86F_FORMAT_HEADER = "86BF";
	static constexpr int _86F_MAJOR_VERSION = 2;
	static constexpr int _86F_MINOR_VERSION = 12;

	struct TrackInfo {
		std::array<uint8_t,10> info_data;
		unsigned data_offset = 0;
		bool extra_bc_present = false;
		bool bc_is_total = false;
		double rpm_adjust = 1.0;

		TrackInfo() {}
		TrackInfo(unsigned _data_offset, bool _bc, bool _bc_total, double _rpm_adjust) :
			data_offset(_data_offset),
			extra_bc_present(_bc), bc_is_total(_bc_total), rpm_adjust(_rpm_adjust) {}

		uint16_t get_flags();
		int get_rpm();
		FloppyDisk::Encoding get_encoding();
		FloppyDisk::DataRate get_drate();
		int get_drate_kbps();
		int get_count();
		int get_index();
		unsigned get_bit_length();
		unsigned get_byte_length();
		void read_track_data(std::ifstream &_fstream, std::vector<uint8_t> &_buffer, bool _desc = false);

		std::string get_info_str();
	};

public:
	FloppyFmt_86F() {}

	const char *name() const { return "86F"; }
	const char *description() const { return "86F (86Box Floppy)(*.86f)"; }
	const char *default_file_extension() const { return ".86f"; }
	std::vector<const char *> file_extensions() const { return {".86f"}; }
	bool can_save() const  { return false; }
	FloppyFmt *create() const { return new FloppyFmt_86F(); }

	FloppyDisk::Properties identify(std::string file_path, uint64_t file_size,
			FloppyDisk::Size _disk_size);

	bool load(std::ifstream &_file, FloppyDisk &_disk);

	MediumInfoData get_preview_string(std::string _filepath);

private:
	void decode_header();
	bool detect_duplicate_odd_tracks(std::ifstream &_fstream);

	bool load_raw(std::ifstream &_file, FloppyDisk_Raw &_disk);
	bool load_flux(std::ifstream &_file, FloppyDisk &_disk);

	uint32_t get_track_words();

	bool read_track_offsets(std::ifstream &_fstream);
	TrackInfo read_track_info(std::ifstream &_fstream, int _track_idx);
	TrackInfo read_track_info(std::ifstream &_fstream, uint8_t _cyl, uint8_t _head);

	void generate_track_from_bitstream_with_weak(int cyl, int head,
		const uint8_t *trackbuf, const uint8_t *weak, int index_cell, int track_size,
		FloppyDisk &_disk) const;
};

#endif