// license:BSD-3-Clause
// copyright-holders:Nathan Woods, Marco Bortolin

// Based on MAME's formats/flopimg.h

#ifndef IBMULATOR_FLOPPYFMT_H
#define IBMULATOR_FLOPPYFMT_H

//#include <stdio.h>
#include <vector>
#include <string>
#include "floppydisk.h"

//! Class representing a floppy image format.
class FloppyFmt
{
public:
	static FloppyFmt *find(std::string _image_path);

public:

	virtual ~FloppyFmt() = default;

	virtual const char * name() const = 0;
	virtual const char * default_file_extension() const = 0;
	virtual std::vector<const char *> file_extensions() const = 0;
	virtual bool has_file_extension(std::string _ext) const = 0;
	virtual bool can_save() const { return false; }

	virtual FloppyDisk::Properties identify(std::string file_path, uint64_t file_size,
			FloppyDisk::Size _disk_size) = 0;

	virtual bool load(std::ifstream &_file, FloppyDisk &_disk) = 0;
	virtual bool save(std::ofstream &_file, const FloppyDisk &_disk);

	const std::string &loaded_file() const { return m_imgfile; }
	const FloppyDisk::Properties &loaded_props() const { return m_geom; }

	virtual std::string get_preview_string(std::string) { return ""; }

protected:

	std::string m_imgfile; // used for loading only
	FloppyDisk::Properties m_geom; // used for loading only

	//! Input for convert_to_edge
	enum {
		MG_SHIFT = 28,

		MG_0     = (4 << MG_SHIFT),
		MG_1     = (5 << MG_SHIFT),
		MG_W     = (6 << MG_SHIFT)
	};

	// **** Reader helpers ****

	//! Struct designed for easy track data description. Contains an opcode and two params.

	//! Optional, you can always do things by hand, but useful nevertheless.
	//! A vector of these structures describes one track.

	struct desc_e
	{
		int type = END; //!< An opcode
		int p1 = 0;     //!< first param
		int p2 = 0;     //!< second param
	};

	//! Opcodes of the format description language used by generate_track()
	enum {
		END,                    //!< End of description
		FM,                     //!< One byte in p1 to be fm-encoded, msb first, repeated p2 times
		MFM,                    //!< One byte in p1 to be mfm-encoded, msb first, repeated p2 times
		MFMBITS,                //!< A value of p2 bits in p1 to be mfm-encoded, msb first
		RAW,                    //!< One 16 bits word in p1 to be written raw, msb first, repeated p2 times
		RAWBYTE,                //!< One 8 bit byte in p1 to be written raw, msb first, repeated p2 times
		RAWBITS,                //!< A value of p2 bits in p1 to be copied as-is, msb first
		TRACK_ID,               //!< Track id byte, mfm-encoded
		TRACK_ID_FM,            //!< Track id byte, fm-encoded
		HEAD_ID,                //!< Head id byte, mfm-encoded
		HEAD_ID_FM,             //!< Head id byte, fm-encoded
		HEAD_ID_SWAP,           //!< Head id byte swapped (0->1, 1->0), mfm-encoded
		SECTOR_ID,              //!< Sector id byte, mfm-encoded
		SECTOR_ID_FM,           //!< Sector id byte, fm-encoded
		SIZE_ID,                //!< Sector size code on one byte [log2(size/128)], mfm-encoded
		SIZE_ID_FM,             //!< Sector size code on one byte [log2(size/128)], fm-encoded
		OFFSET_ID_O,            //!< Offset (track*2+head) byte, odd bits, mfm-encoded
		OFFSET_ID_E,            //!< Offset (track*2+head) byte, even bits, mfm-encoded
		OFFSET_ID_FM,           //!< Offset (track*2+head) byte, fm-encoded
		OFFSET_ID,              //!< Offset (track*2+head) byte, mfm-encoded
		SECTOR_ID_O,            //!< Sector id byte, odd bits, mfm-encoded
		SECTOR_ID_E,            //!< Sector id byte, even bits, mfm-encoded
		REMAIN_O,               //!< Remaining sector count, odd bits, mfm-encoded, total sector count in p1
		REMAIN_E,               //!< Remaining sector count, even bits, mfm-encoded, total sector count in p1

		SECTOR_DATA,            //!< Sector data to mfm-encode, which in p1, -1 for the current one per the sector id
		SECTOR_DATA_FM,         //!< Sector data to fm-encode, which in p1, -1 for the current one per the sector id
		SECTOR_DATA_O,          //!< Sector data to mfm-encode, odd bits only, which in p1, -1 for the current one per the sector id
		SECTOR_DATA_E,          //!< Sector data to mfm-encode, even bits only, which in p1, -1 for the current one per the sector id

		CRC_CCITT_START,        //!< Start a CCITT CRC calculation, with the usual x^16 + x^12 + x^5 + 1 (11021) polynomial, p1 = crc id
		CRC_CCITT_FM_START,     //!< Start a CCITT CRC calculation, with the usual x^16 + x^12 + x^5 + 1 (11021) polynomial, p1 = crc id
		CRC_END,                //!< End the checksum, p1 = crc id
		CRC,                    //!< Write a checksum in the appropriate format, p1 = crc id

		SECTOR_LOOP_START,      //!< Start of the per-sector loop, sector number goes from p1 to p2 inclusive
		SECTOR_LOOP_END,        //!< End of the per-sector loop
		SECTOR_INTERLEAVE_SKEW  //!< Defines interleave and skew for sector counting
	};


	//! Sector data description
	struct desc_s
	{
		unsigned size;       //!< Sector size in bytes
		const uint8_t *data; //!< Sector data
		uint8_t sector_id;   //!< Sector ID
		uint8_t sector_info; //!< Sector free byte
	};


	/*! @brief Generate one track according to the description vector.
	    @param desc track data description
	    @param track
	    @param head
	    @param sect a vector indexed by sector id.
	    @param sect_count number of sectors.
	    @param track_size in _cells_, i.e. 100000 for a usual 2us-per-cell track at 300rpm.
	    @param image
	*/
	static void generate_track(const desc_e *desc, int track, int head, const desc_s *sect,
			int sect_count, int track_size, FloppyDisk &image);

	/*! @brief Generate a track from cell binary values, MSB-first.
	    @param track
	    @param head
	    @param trackbuf track input buffer.
	    @param track_size in cells, not bytes.
	    @param image
	    @param splice write splice position
	*/
	static void generate_track_from_bitstream(int track, int head, const uint8_t *trackbuf,
			int track_size, FloppyDisk *image, int splice = 0);

	//! @brief Generate a track from cell level values (0/1/W/D/N).

	/*! Note that this function needs to be able to split cells in two,
	    so no time value should be less than 2, and even values are a
	    good idea.
	*/
	/*! @param track
	    @param head
	    @param trackbuf track input buffer.
	    @param track_size in cells, not bytes.
	    @param splice_pos is the position of the track splice.  For normal
	    formats, use -1.  For protected formats, you're supposed to
	    know. trackbuf may be modified at that position or after.
	    @param image
	*/
	static void generate_track_from_levels(int track, int head, std::vector<uint32_t> &trackbuf,
			int splice_pos, FloppyDisk &_disk);

	//! Normalize the times in a cell buffer to sum up to 200000000
	static void normalize_times(std::vector<uint32_t> &buffer);

	// **** Writer helpers ****

	/*! @brief Rebuild a cell bitstream for a track.
	    Takes the cell standard
	    angular size as a parameter, gives out a msb-first bitstream.

	    Beware that fuzzy bits will always give out the same value.
	    @param track
	    @param head
	    @param cell_size
	    @param trackbuf Output buffer size should be 34% more than the nominal number
	    of cells (the dpll tolerates a cell size down to 75% of the
	    nominal one, with gives a cell count of 1/0.75=1.333... times
	    the nominal one).
	    @param track_size Output size is given in bits (cells).
	    @param image
	*/
	/*! @verbatim
	 Computing the standard angular size of a cell is
	 simple. Noting:
	   d = standard cell duration in microseconds
	   r = motor rotational speed in rpm
	 then:
	   a = r * d * 10 / 3.
	 Some values:
	   Type           Cell    RPM    Size

	 8" DD            1       360    1200
	 5.25" SD         4       300    4000
	 5.25" DD         2       300    2000
	 5.25" HD         1       360    1200
	 3.5" SD          4       300    4000
	 3.5" DD          2       300    2000
	 3.5" HD          1       300    1000
	 3.5" ED          0.5     300     500
	 @endverbatim
	 */

	static std::vector<bool> generate_bitstream_from_track(int track, int head, int cell_size,
			const FloppyDisk &_disk);
	static std::vector<uint8_t> generate_nibbles_from_bitstream(const std::vector<bool> &bitstream);

	struct desc_pc_sector
	{
		uint8_t track, head, sector, size;
		int actual_size;
		uint8_t *data;
		bool deleted;
		bool bad_crc;
	};

	struct desc_gcr_sector
	{
		uint8_t track, head, sector, info;
		uint8_t *tag;
		uint8_t *data;
	};

	static int calc_default_pc_gap3_size(uint32_t form_factor, int sector_size);
	static void build_pc_track_fm(uint8_t track, uint8_t head, FloppyDisk &image,
			unsigned cell_count, unsigned sector_count, const desc_pc_sector *sects,
			int gap_3, int gap_4a=40, int gap_1=26, int gap_2=11);
	static void build_pc_track_mfm(uint8_t track, uint8_t head, FloppyDisk &image,
			unsigned cell_count, unsigned sector_count, const desc_pc_sector *sects,
			int gap_3, int gap_4a=80, int gap_1=50, int gap_2=22);

	//! @brief Extract standard sectors from a regenerated bitstream.
	//! Returns a vector of the vector contents, indexed by the sector id.  Missing sectors have size zero.

	//! PC-type sectors with MFM encoding, sector size can go from 128 bytes to 16K.
	static std::vector<std::vector<uint8_t>> extract_sectors_from_bitstream_mfm_pc(const std::vector<bool> &bitstream);

	//! PC-type sectors with FM encoding
	static std::vector<std::vector<uint8_t>> extract_sectors_from_bitstream_fm_pc(const std::vector<bool> &bitstream);


	//! @brief Get a geometry (including sectors) from an image.

	//!   PC-type sectors with MFM encoding
	static void get_geometry_mfm_pc(const FloppyDisk &, int cell_size, int &track_count, int &head_count, int &sector_count);
	//!   PC-type sectors with FM encoding
	static void get_geometry_fm_pc(const FloppyDisk &, int cell_size, int &track_count, int &head_count, int &sector_count);


	//!  Regenerate the data for a full track.
	//!  PC-type sectors with MFM encoding and fixed-size.
	static void get_track_data_mfm_pc(uint8_t track, uint8_t head, const FloppyDisk &image,
			unsigned cell_size, unsigned sector_size, unsigned sector_count, uint8_t *sectdata);

	//!  Regenerate the data for a full track.
	//!  PC-type sectors with FM encoding and fixed-size.
	static void get_track_data_fm_pc(uint8_t track, uint8_t head, const FloppyDisk &image,
			unsigned cell_size, unsigned sector_size, unsigned sector_count, uint8_t *sectdata);

	//! Look up a bit in a level-type stream.
	static bool bit_r(const std::vector<uint32_t> &buffer, int offset);
	//! Look up multiple bits
	static uint32_t bitn_r(const std::vector<uint32_t> &buffer, int offset, int count);
	//! Write a bit with a given size.
	static void bit_w(std::vector<uint32_t> &buffer, bool val, uint32_t size = 1000);
	static void bit_w(std::vector<uint32_t> &buffer, bool val, uint32_t size, int offset);
	//! Calculate a CCITT-type CRC.
	static uint16_t calc_crc_ccitt(const std::vector<uint32_t> &buffer, int start, int end);
	//! Write a series of (raw) bits
	static void raw_w(std::vector<uint32_t> &buffer, int n, uint32_t val, uint32_t size = 1000);
	static void raw_w(std::vector<uint32_t> &buffer, int n, uint32_t val, uint32_t size, int offset);
	//! FM-encode and write a series of bits
	static void fm_w(std::vector<uint32_t> &buffer, int n, uint32_t val, uint32_t size = 1000);
	static void fm_w(std::vector<uint32_t> &buffer, int n, uint32_t val, uint32_t size, int offset);
	//! MFM-encode and write a series of bits
	static void mfm_w(std::vector<uint32_t> &buffer, int n, uint32_t val, uint32_t size = 1000);
	static void mfm_w(std::vector<uint32_t> &buffer, int n, uint32_t val, uint32_t size, int offset);
	//! MFM-encode every two bits and write
	static void mfm_half_w(std::vector<uint32_t> &buffer, int start_bit, uint32_t val, uint32_t size = 1000);

	static uint8_t sbyte_mfm_r(const std::vector<bool> &bitstream, uint32_t &pos);

	//! Max number of excess tracks to be discarded from disk image to fit floppy drive
	enum { DUMP_THRESHOLD = 2 };

private:
	enum { CRC_NONE, CRC_CCITT, CRC_CCITT_FM };
	enum { MAX_CRC_COUNT = 64 };

	//! Holds data used internally for generating CRCs.
	struct gen_crc_info
	{
		int type,  //!< Type of CRC
			start, //!< Start position
			end,   //!< End position
			write; //!< where to write the CRC
		bool fixup_mfm_clock; //!< would the MFM clock bit after the CRC need to be fixed?
	};

	static bool type_no_data(int type);
	static bool type_data_mfm(int type, int p1, const gen_crc_info *crcs);

	static int crc_cells_size(int type);
	static void fixup_crc_ccitt(std::vector<uint32_t> &buffer, const gen_crc_info *crc);
	static void fixup_crc_ccitt_fm(std::vector<uint32_t> &buffer, const gen_crc_info *crc);
	static void fixup_crcs(std::vector<uint32_t> &buffer, gen_crc_info *crcs);
	static void collect_crcs(const desc_e *desc, gen_crc_info *crcs);

	static int sbit_rp(const std::vector<bool> &bitstream, uint32_t &pos);

	static int calc_sector_index(int num, int interleave, int skew, int total_sectors, int track_head);
};

#endif
