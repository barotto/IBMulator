// license:BSD-3-Clause
// copyright-holders:Olivier Galibert, Marco Bortolin

// Based on MAME's "lib/formats/upd765_dsk.h"

#ifndef IBMULATOR_HW_FLOPPYFMT_IMG_H
#define IBMULATOR_HW_FLOPPYFMT_IMG_H


#include "floppyfmt.h"
#include "floppydisk_raw.h"

class FloppyFmt_IMG : public FloppyFmt
{
public:
	FloppyFmt_IMG() {}

	const char *name() const { return "IMG"; }
	const char *description() const { return "IMG (Raw sector)(*.img)"; }
	const char *default_file_extension() const { return ".img"; }
	std::vector<const char *> file_extensions() const { return {".img",".ima"}; }
	bool can_save() const  { return true; }
	FloppyFmt *create() const { return new FloppyFmt_IMG(); }

	FloppyDisk::Properties identify(std::string file_path, uint64_t file_size,
			FloppyDisk::Size _disk_size);

	bool load(std::ifstream &_file, FloppyDisk &_disk);
	bool save(std::ofstream &_file, const FloppyDisk &_disk);

	MediumInfoData get_preview_string(std::string _filepath);

protected:
	static constexpr int SECTOR_BASE_ID = 1;
	struct encoding {
		unsigned type = 0;
		int cell_size; // See FloppyFmt for details
		int gap_4a;    // number of 4e between index and IAM sync
		int gap_1;     // number of 4e between IAM and first IDAM sync
		int gap_2;     // number of 4e between sector header and data sync
		int gap_3;     // number of 4e between sector crc and next IDAM
	} m_enc; // used by load() only

	static const std::map<unsigned, encoding> encodings;

	bool load_raw(std::ifstream &_file, FloppyDisk_Raw &_disk);
	bool load_flux(std::ifstream &_file, FloppyDisk &_disk);

	bool save_raw(std::ofstream &_file, const FloppyDisk_Raw &_disk);
	bool save_flux(std::ofstream &_file, const FloppyDisk &_disk);

	FloppyFmt::desc_e* get_desc_fm(int &current_size, int &end_gap_index);
	FloppyFmt::desc_e* get_desc_mfm(int &current_size, int &end_gap_index);
	void build_sector_description(const FloppyDisk::Properties &f, const uint8_t *sectdata,
			desc_s *sectors) const;
	void check_compatibility(const FloppyDisk &_disk, std::vector<unsigned> &candidates);
	void extract_sectors(const FloppyDisk &disk,
			const FloppyDisk::Properties &f, const encoding &e, desc_s *sdesc, int track, int head);
};

#endif
