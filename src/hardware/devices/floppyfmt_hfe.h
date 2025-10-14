// license:BSD-3-Clause
// copyright-holders:Michael Zapf, Marco Bortolin

// Based on MAME's "lib/formats/hxchfe_dsk.h"

#ifndef IBMULATOR_HW_FLOPPYFMT_HFE_H
#define IBMULATOR_HW_FLOPPYFMT_HFE_H

#include "floppyfmt.h"
#include "floppydisk_raw.h"

class FloppyFmt_HFE : public FloppyFmt
{
public:
	struct HFEHeader
	{
		uint8_t  HEADERSIGNATURE[8];   // 00-07 "HXCPICFE" for v1, "HXCHFEV3" for v3
		uint8_t  formatrevision;       //    08 Revision = 0
		uint8_t  number_of_tracks;     //    09 Number of tracks (cylinders) in the file
		uint8_t  number_of_sides;      //    0a Number of valid sides
		uint8_t  track_encoding;       //    0b Track Encoding mode
		uint16_t bitRate;              // 0c-0d Bitrate in Kbit/s.
		uint16_t floppyRPM;            // 0e-0f Revolutions per minute
		uint8_t  floppyinterfacemode;  //    10 Floppy interface mode.
		uint8_t  reserved;             //    11 do not use
		uint16_t track_list_offset;    // 12-13 Offset of the track list LUT in block of 512 bytes (Ex: 1=0x200)
		uint8_t  write_allowed;        //    14 The Floppy image is not write protected ?
		uint8_t  single_step;          //    15 0xFF : Single Step – 0x00 Double Step mode
		uint8_t  track0s0_altencoding; //    16 0x00 : Use an alternate track_encoding for track 0 Side 0
		uint8_t  track0s0_encoding;    //    17 alternate track_encoding for track 0 Side 0
		uint8_t  track0s1_altencoding; //    18 0x00 : Use an alternate track_encoding for track 0 Side 1
		uint8_t  track0s1_encoding;    //    19 alternate track_encoding for track 0 Side 1
	} GCC_ATTRIBUTE(packed);

	// Byte order is little endian.
	// track0s0_encoding is only valid when track0s0_altencoding is 0xff
	// track0s1_encoding is only valid when track0s1_altencoding is 0xff

	// A track is divided in blocks of 512 bytes
	struct s_pictrack
	{
		uint16_t offset;    // Offset of the track data in blocks of 512 bytes (Ex: 2=0x400)
		uint16_t track_len; // Length of the track data in bytes.
	} GCC_ATTRIBUTE(packed);

	enum {
		HEADER_LENGTH = 512,
		TRACK_TABLE_LENGTH = 512,
		TRACK_TABLE_ENTRIES = TRACK_TABLE_LENGTH / sizeof(s_pictrack),
		TRACKS_OFFSET = HEADER_LENGTH + TRACK_TABLE_LENGTH
	};

	static constexpr const char * HFE_FORMAT_HEADER_v1 = "HXCPICFE";
	static constexpr const char * HFE_FORMAT_HEADER_v3 = "HXCHFEV3";
	static constexpr const int RPM = 300;
	static constexpr long int CYLTIME = (long int)(1'000'000'000LL * 60 / RPM); // one rotation in nanosec

	enum encoding_t
	{
		ISOIBM_MFM_ENCODING = 0x00,
		AMIGA_MFM_ENCODING,
		ISOIBM_FM_ENCODING,
		EMU_FM_ENCODING,
		UNKNOWN_ENCODING = 0xff
	};

	enum floppymode_t
	{
		IBMPC_DD_FLOPPYMODE = 00,
		IBMPC_HD_FLOPPYMODE,
		ATARIST_DD_FLOPPYMODE,
		ATARIST_HD_FLOPPYMODE,
		AMIGA_DD_FLOPPYMODE,
		AMIGA_HD_FLOPPYMODE,
		CPC_DD_FLOPPYMODE,
		GENERIC_SHUGART_DD_FLOPPYMODE,
		IBMPC_ED_FLOPPYMODE,
		MSX2_DD_FLOPPYMODE,
		C64_DD_FLOPPYMODE,
		EMU_SHUGART_FLOPPYMODE,
		S950_DD_FLOPPYMODE,
		S950_HD_FLOPPYMODE,
		DISABLE_FLOPPYMODE = 0xfe
	};

public:
	FloppyFmt_HFE() {}

	const char *name() const { return "HFE"; }
	const char *description() const { return "HFE (HxC Floppy Emu)(*.hfe)"; }
	const char *default_file_extension() const { return ".hfe"; }
	std::vector<const char *> file_extensions() const { return {".hfe"}; }
	bool can_save() const  { return true; }
	FloppyFmt *create() const { return new FloppyFmt_HFE(); }

	FloppyDisk::Properties identify(std::string file_path, uint64_t file_size,
			FloppyDisk::Size _disk_size);

	bool load(std::ifstream &_file, FloppyDisk &_disk);
	bool save(std::ofstream &_file, const FloppyDisk &_disk);

	MediumInfoData get_preview_string(std::string _filepath);

protected:
	bool load_raw(std::ifstream &_file, FloppyDisk_Raw &_disk);
	bool load_flux(std::ifstream &_file, FloppyDisk &_disk);

	int determine_cell_size(const std::vector<uint32_t> &tbuf) const;
	
	void generate_track_from_hfe_bitstream(int cyl, int head, int samplelength,
			const uint8_t *trackbuf, int track_end, FloppyDisk &image);
	void generate_hfe_bitstream_from_track(
			int cyl, int head, long cyltime,
			int samplelength, std::vector<uint8_t> &cylinder_buffer,
			const FloppyDisk &_disk) const;
	void generate_hfe_bitstream_from_track(int cyl, int head,
			int &samplelength, encoding_t &encoding, uint8_t *cylinder_buffer,
			int &track_bytes, const FloppyDisk &_disk);
	HFEHeader m_header = {};
	int m_version = 0;
	std::vector<s_pictrack> m_cylinders;

};

#endif