// license:BSD-3-Clause
// copyright-holders:Nathan Woods, Marco Bortolin

// Based on MAME's formats/flopimg.h

#ifndef IBMULATOR_HW_FLOPPYDISK_H
#define IBMULATOR_HW_FLOPPYDISK_H

#include <memory>
#include <vector>

#include <cassert>
#include <cstddef>
#include <cstdint>

class FloppyFmt;

//! Class representing a floppy disk

//! Track data consists of a series of 32-bits lsb-first values
//! representing the magnetic state.  Bits 0-27 indicate the absolute
//! position of encoded event, and bits ! 28-31 the type.  Type can be:
//! - 0, MG_F -> Flux orientation change
//! - 1, MG_N -> Start of a non-magnetized zone (neutral)
//! - 2, MG_D -> Start of a damaged zone, reads as neutral but cannot be changed by writing
//! - 3, MG_E -> End of one of the previous zones, *inclusive*
//!
//! The position is in angular units of 1/200,000,000th of a turn.
//! A N or D zone must not wrap at the 200,000,000 position, it has to
//! be split in two (the first finishing at 199,999,999, the second
//! starting at 0)
//!
//! Unformatted tracks are encoded as zero-size, and are strictly equivalent
//! to (MG_N, 0), (MG_E, 199,999,999)
//!
//! The "track splice" information indicates where to start writing
//! if you try to rewrite a physical disk with the data.  Some
//! preservation formats encode that information, it is guessed for
//! others.  The write track function of fdcs should set it.  The
//! representation is the angular position relative to the index.

class FloppyDisk
{
public:
	//! Floppy format data
	enum {
		TIME_MASK = 0x0fffffff,
		MG_MASK   = 0xf0000000,
		MG_SHIFT  = 28, //!< Bitshift constant for magnetic orientation data
		MG_F      = (0 << MG_SHIFT),    //!< - 0, MG_F -> Flux orientation change
		MG_N      = (1 << MG_SHIFT),    //!< - 1, MG_N -> Non-magnetized zone (neutral)
		MG_D      = (2 << MG_SHIFT),    //!< - 2, MG_D -> Damaged zone, reads as neutral but cannot be changed by writing
		MG_E      = (3 << MG_SHIFT)     //!< - 3, MG_E -> End of zone
	};

	enum Size {
		SIZE_MASK = 0x00700,
		SIZE_8    = 0x00100, // 8 inch disk
		SIZE_5_25 = 0x00200, // 5.25 inch disk
		SIZE_3_5  = 0x00400  // 3.5 inch disk
	};

	enum Density {
		DENS_MASK = 0x1f000,
		DENS_SHIFT= 12,
		DENS_SD   = 0x01000, // single-density
		DENS_DD   = 0x02000, // double-density
		DENS_QD   = 0x04000, // quad-density
		DENS_HD   = 0x08000, // high-density
		DENS_ED   = 0x10000  // extra-density
	};

	//! Standard DOS formatted PC floppy disk variants
	enum StdType {
		TYPE_MASK = 0xff,
		DOS_FMT   = 0x80,
		FD_NONE   = 0x00, // unknown or invalid medium type
		DD_160K   = 0x81 | SIZE_5_25 | DENS_DD, //  160K 5.25"
		DD_180K   = 0x82 | SIZE_5_25 | DENS_DD, //  180K 5.25"
		DD_320K   = 0x83 | SIZE_5_25 | DENS_DD, //  320K 5.25"
		DD_360K   = 0x84 | SIZE_5_25 | DENS_DD, //  360K 5.25"
		QD_720K   = 0x05 | SIZE_5_25 | DENS_QD, //  720K 5.25"
		DD_720K   = 0x86 | SIZE_3_5  | DENS_DD, //  720K 3.5"
		HD_1_20   = 0x87 | SIZE_5_25 | DENS_HD, // 1.20M 5.25"
		HD_1_44   = 0x88 | SIZE_3_5  | DENS_HD, // 1.44M 3.5"
		HD_1_68   = 0x09 | SIZE_3_5  | DENS_HD, // 1.68M DMF 3.5"
		HD_1_72   = 0x0a | SIZE_3_5  | DENS_HD, // 1.72M DMF 3.5"
		ED_2_88   = 0x8f | SIZE_3_5  | DENS_ED  // 2.88M 3.5"
	};

	// nominal data rates for the medium
	// note: 5.25" High Capacity (1.2M) drives always use 300kbps for DD media.
	enum DataRate {
		DRATE_250  = 2,
		DRATE_300  = 1,
		DRATE_500  = 0,
		DRATE_1000 = 3
	};

	//! Encodings
	enum Encoding {
		FM   = 0x2020464D, //!< "  FM", frequency modulation
		MFM  = 0x204D464D, //!< " MFM", modified frequency modulation
		M2FM = 0x4D32464D  //!< "M2FM", modified modified frequency modulation
	};

	// General physical and formatted characteristics.
	// if type == 0 (FD_NONE), other fields are invalid
	// if spt == 0 then sec, secsize, cap are also invalid
	struct Properties {
		unsigned type    = FD_NONE;   // disk variant as or'd combination of Density and Size,
		                              // or a value of StdType
		uint8_t  tracks  = 0;         // number of tracks per side (cylinders)
		uint8_t  sides   = 0;         // number of sides
		uint8_t  spt     = 0;         // number of sectors per track
		unsigned secsize = 0;         // sector size in bytes
		unsigned sectors = 0;         // number of formatted sectors
		unsigned cap     = 0;         // formatted capacity in bytes
		DataRate drate   = DRATE_250; // data rate as a FDC encoded value
		std::string desc = "";        // logging name
	};

	//! FloppyDisk constructor
	/*!
	  @param type size and density.
	  @param tracks number of tracks.
	  @param heads number of heads.
	*/
	FloppyDisk(const Properties &_props);
	virtual ~FloppyDisk();

	bool load(std::string _path, std::shared_ptr<FloppyFmt> _format);
	bool save(std::string _path, std::shared_ptr<FloppyFmt> _format);

	void load_state(std::string _imgpath, std::string _binpath);
	void save_state(std::string _binpath);

	void set_dirty() {
		m_dirty = true;
		m_dirty_restore = true;
	}
	bool is_dirty(bool _since_restore = false) const {
		// if _since_restore==true then returns a dirty state set after a restore
		// if a restore never happened returns a general dirty state because
		// m_dirty and m_dirty_restore are set to true at the same time.
		return (_since_restore) ? m_dirty_restore : m_dirty;
	}

	void set_write_protected(bool _wp) { m_wprot = _wp; }
	bool is_write_protected() const { return m_wprot; }

	std::string get_image_path() const {
		return m_loaded_image;
	}
	std::shared_ptr<FloppyFmt> get_format() const {
		return m_format;
	}
	void set_format(std::shared_ptr<FloppyFmt> _format) {
		m_format = _format;
	}
	bool can_be_committed() const;

	/*!
	  @param track
	  @param head head number
	  @return a pointer to the data buffer for this track and head
	*/
	const std::vector<uint32_t> &get_buffer(uint8_t track, uint8_t head) const {
		assert(track < track_array.size() && head < m_props.sides);
		return track_array[track][head].cell_data;
	}
	std::vector<uint32_t> &get_buffer(uint8_t track, uint8_t head) {
		assert(track < track_array.size() && head < m_props.sides);
		return track_array[track][head].cell_data;
	}

	//! Sets the write splice position.
	//! The "track splice" information indicates where to start writing
	//! if you try to rewrite a physical disk with the data.  Some
	//! preservation formats encode that information, it is guessed for
	//! others.  The write track function of fdcs should set it.  The
	//! representation is the angular position relative to the index.

	/*! @param track
	    @param head
	    @param pos the position
	*/
	void set_write_splice_position(uint8_t track, uint8_t head, uint32_t pos) {
		assert(track < track_array.size() && head < m_props.sides);
		track_array[track][head].write_splice = pos;
	}
	//! @return the current write splice position.
	uint32_t get_write_splice_position(uint8_t track, uint8_t head) const {
		assert(track < track_array.size() && head < m_props.sides);
		return track_array[track][head].write_splice;
	}

	void set_track_damaged_info(uint8_t track, uint8_t head) {
		assert(track < track_array.size() && head < m_props.sides);
		track_array[track][head].has_damaged_cells = true;
	}
	bool track_has_damaged_cells(uint8_t track, uint8_t head) const {
		assert(track < track_array.size() && head < m_props.sides);
		return track_array[track][head].has_damaged_cells;
	}

	//! @return the maximal geometry supported by this format.
	void get_maximal_geometry(int &tracks, int &heads) const;

	//! @return the current geometry of the loaded image.
	void get_actual_geometry(int &tracks, int &heads) const;

	//! @return whether a given track is formatted
	virtual bool track_is_formatted(int track, int head);

	//! changes the number of tracks, new tracks are unformatted.
	void resize_tracks(unsigned _num_of_tracks);

	//! reads and writes a specific formatted sector
	virtual void read_sector(uint8_t _c, uint8_t _h, uint8_t _s, uint8_t *buffer, uint32_t bytes);
	virtual void write_sector(uint8_t _c, uint8_t _h, uint8_t _s, const uint8_t *buffer, uint32_t bytes);

	const Properties & props() const { return m_props; }
	void set_type(unsigned _type) { m_props.type = _type; }
	bool double_step() const { return m_props.tracks <= 42; }

public:
	static const std::map<StdType, Properties> std_types;
	static Properties find_std_type(unsigned _variant);

protected:
	std::string m_loaded_image;
	Properties m_props;
	bool m_wprot = false; // write protected?
	bool m_dirty = false; // written since loaded?
	bool m_dirty_restore = false; // written since restored?
	std::shared_ptr<FloppyFmt> m_format; // the format that loaded this floppy

	struct track_info
	{
		// cell_data can memorize either cell flux or sector byte (for FloppyDisk_RAW)
		// If it's sector data the space is 4x the necessary.
		// The pro is I don't need to write special code for FloppyDisk_RAW other than 
		// slightly more complex data read/write functions 
		std::vector<uint32_t> cell_data;
		uint32_t write_splice;
		bool has_damaged_cells;

		track_info() { write_splice = 0; has_damaged_cells = false; }
		void save_state(std::ofstream &);
		void load_state(std::ifstream &);
	};

	// track number then head
	// last array size may be bigger than actual track size
	std::vector<std::vector<track_info>> track_array;
};

#endif
