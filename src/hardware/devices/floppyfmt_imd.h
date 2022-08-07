// license:BSD-3-Clause
// copyright-holders:Miodrag Milanovic, Marco Bortolin

// Based on MAME's "lib/formats/imd_dsk.h"

#ifndef IBMULATOR_HW_FLOPPYFMT_IMD_H
#define IBMULATOR_HW_FLOPPYFMT_IMD_H

#include "floppyfmt.h"
#include "floppydisk_raw.h"

class FloppyFmt_IMD : public FloppyFmt
{
public:
	FloppyFmt_IMD() {}

	const char *name() const { return "IMD"; }
	const char *description() const { return "IMD (ImageDisk)(*.imd)"; }
	const char *default_file_extension() const { return ".imd"; }
	std::vector<const char *> file_extensions() const { return {".imd"}; }
	bool can_save() const  { return false; }
	FloppyFmt *create() const { return new FloppyFmt_IMD(); }

	FloppyDisk::Properties identify(std::string file_path, uint64_t file_size,
			FloppyDisk::Size _disk_size);

	bool load(std::ifstream &_file, FloppyDisk &_disk);

	std::string get_preview_string(std::string _filepath);

protected:
	struct TrackInfo {
		uint8_t mode    = 0; // 1 byte  Mode value                  (0-5)
		uint8_t cyl     = 0; // 1 byte  Cylinder                    (0-n)
		uint8_t head    = 0; // 1 byte  Head                        (0-1)
		uint8_t spt     = 0; // 1 byte  number of sectors in track  (1-n)
		uint8_t secsize = 0; // 1 byte  sector size                 (0-6)

		bool has_cyl_map() const { return head & 0x80; }
		bool has_head_map() const { return head & 0x40; }
		bool has_secsize_table() const { return secsize == 0xff; }
		unsigned get_actual_secsize() const { return secsize < 7 ? 128 << secsize : 8192; }
		unsigned get_rate_kbps() const {
			static const unsigned rates[3] = { 500,300,250 };
			return rates[mode % 3];
		}

	} GCC_ATTRIBUTE(packed);

	std::vector<char> m_header;
	unsigned m_load_offset = 0;
	bool m_std_dos = true;

	bool load_raw(std::ifstream &_file, FloppyDisk_Raw &_disk);
	bool load_flux(std::ifstream &_file, FloppyDisk &_disk);
};

#endif