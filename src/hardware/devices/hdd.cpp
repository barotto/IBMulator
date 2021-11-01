/*
 * Copyright (C) 2016-2021  Marco Bortolin
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
#include "hdd.h"
#include "hddparams.h"
#include "program.h"
#include "machine.h"
#include "storagectrl_ps1.h"
#include "storagectrl_ata.h"
#include <cstring>
#include <sstream>
#include <regex>

#define HDD_IMAGES_ARCHIVE "disk_images.zip"

/* Assuming the ST412/506 HD format RLL encoding, this should be the anatomy of
 * a sector:
 * SYNC   10 bytes 00h
 * IDAM    2 bytes 5eh a1h
 * ID      4 bytes cylinder head sector flags
 * ECC     4 bytes ECC value
 * GAP     5 bytes 00h
 * SYNC   11 bytes 00h
 * DAM     2 bytes 5eh a1h
 * Data  512 bytes data
 * ECC     6 bytes ECC value
 * GAP     3 bytes 00h
 * GAP    17 bytes ffh
 *
 * Tracks also have a preamble and a closing gap:
 * SYNC 11 bytes 00h
 * IAM   2 bytes a1h fch
 * GAP  12 bytes ffh
 * ...
 * SECTORS
 * ...
 * GAP ~93 bytes 00h
 */
#define HDD_SECTOR_SIZE    (512+64)  // total sector size (data + overhead)
#define HDD_TRACK_OVERHEAD  (25+64)  // start+end of track (closing GAP value
                                     // derived from observation)

#define HDD_MAX_CYLINDERS   1024 // maximum number of cylinders for custom type
#define HDD_MAX_HEADS       16   // maximim number of heads for custom type
#define HDD_MAX_SECTORS     63   // maximim number of sectors per track for custom type
                                 // apparently, there's a BIOS bug that prevents
                                 // the system to correctly format a disk with 63 spt

// The following factors were derived from measurements of a WDL-330P specimen.
// 0.99378882 = average speed = 32.0 / ((921-1)*35/1000.0), 35=avg speed in us/cyl
// 1.6240 = maximum speed in mm/ms
// 0.3328 = acceleration in mm/ms^2
#define HDD_HEAD_SPEED (1.6240 / 0.99378882)
#define HDD_HEAD_ACCEL (0.3328 / 0.99378882)
#define HDD_DISK_RADIUS 32.0

/*
   IBM HDD types 1-44

   Cyl.    Head    Sect.   Write    Land
                           p-comp   Zone
*/
const MediaGeometry HardDiskDrive::ms_hdd_types[HDD_DRIVES_TABLE_SIZE] = {
{   0,     0,       0,          0,      0 }, // 0 (none)
{ 306,     4,      17,        128,    305 }, // 1 10MB
{ 615,     4,      17,        300,    615 }, // 2 20MB
{ 615,     6,      17,        300,    615 }, // 3 31MB
{ 940,     8,      17,        512,    940 }, // 4 62MB
{ 940,     6,      17,        512,    940 }, // 5 47MB
{ 615,     4,      17,         -1,    615 }, // 6 20MB
{ 462,     8,      17,        256,    511 }, // 7 31MB
{ 733,     5,      17,         -1,    733 }, // 8 30MB
{ 900,    15,      17,         -1,    901 }, // 9 112MB
{ 820,     3,      17,         -1,    820 }, //10 20MB
{ 855,     5,      17,         -1,    855 }, //11 35MB
{ 855,     7,      17,         -1,    855 }, //12 50MB
{ 306,     8,      17,        128,    319 }, //13 20MB
{ 733,     7,      17,         -1,    733 }, //14 43MB
{   0,     0,       0,          0,      0 }, //15 (reserved)
{ 612,     4,      17,          0,    663 }, //16 20MB
{ 977,     5,      17,        300,    977 }, //17 41MB
{ 977,     7,      17,         -1,    977 }, //18 57MB
{ 1024,    7,      17,        512,   1023 }, //19 59MB
{ 733,     5,      17,        300,    732 }, //20 30MB
{ 733,     7,      17,        300,    732 }, //21 43MB
{ 733,     5,      17,        300,    733 }, //22 30MB
{ 306,     4,      17,          0,    336 }, //23 10MB
{ 612,     4,      17,        305,    663 }, //24 20MB
{ 306,     4,      17,         -1,    340 }, //25 10MB
{ 612,     4,      17,         -1,    670 }, //26 20MB
{ 698,     7,      17,        300,    732 }, //27 41MB
{ 976,     5,      17,        488,    977 }, //28 40MB
{ 306,     4,      17,          0,    340 }, //29 10MB
{ 611,     4,      17,        306,    663 }, //30 20MB
{ 732,     7,      17,        300,    732 }, //31 43MB
{ 1023,    5,      17,         -1,   1023 }, //32 42MB
{ 614,     4,      25,         -1,    663 }, //33 30MB
{ 775,     2,      27,         -1,    900 }, //34 20MB
{ 921,     2,      33,         -1,   1000 }, //35 30MB
{ 402,     4,      26,         -1,    460 }, //36 20MB
{ 580,     6,      26,         -1,    640 }, //37 44MB
{ 845,     2,      36,         -1,   1023 }, //38 30MB
{ 769,     3,      36,         -1,   1023 }, //39 41MB
{ 531,     4,      39,         -1,    532 }, //40 40MB
{ 577,     2,      36,         -1,   1023 }, //41 20MB
{ 654,     2,      32,         -1,    674 }, //42 20MB
{ 923,     5,      36,         -1,   1023 }, //43 81MB
{ 531,     8,      39,         -1,    532 }  //44 81MB
};

/* Hard disk drive performance characteristics.
 * For types other than 35,38 they are currently unknown.
 * Type 39 is the Maxtor 7040F1, which was mounted on some later model 2011.
 */
const std::map<unsigned, DrivePerformance> HardDiskDrive::ms_hdd_performance = {
{ 35, { 40.0f, 8.0f, 3600, 4, 0,0,0,0,0,0,0 } }, //35 30MB
{ 38, { 40.0f, 9.0f, 3700, 4, 0,0,0,0,0,0,0 } }, //38 30MB
//{ 39, {  0.0f, 0.0f,    0, 0, 0,0,0,0,0,0,0 } }, //39 41MB
};

// TODO this is a stub. implement proper hdd performance characteristics
static const DrivePerformance default_ps1_perf = {
	40.0f, 8.0f, 3600, 4, 0,0,0,0,0,0,0   // IBM WDL-330P
};
static const DrivePerformance default_ata_perf = {
	17.0f, 6.0f, 3700, 1, 0,0,0,0,0,0,0   // MAXTOR 7080A
};

const std::map<int, const DriveIdent> HardDiskDrive::ms_hdd_models = {
{ 0,  DriveIdent{ "IBMLTR", "STD TYPE ","1.0", "IBMLTR STD TYPE ","1", "1.0" } },
{ 35, DriveIdent{ "IBM",    "WDL-330P", "1.0", "IBM WDL-330P",    "1", "1.0" } }, //35 30MB
{ 38, DriveIdent{ "IBM",    "TYPE 38",  "1.0", "IBM TYPE 38",     "1", "1.0" } }, //38 30MB
{ 39, DriveIdent{ "MAXTOR", "7040F1",   "1.0", "MAXTOR 7040F1",   "1", "1.0" } }, //39 41MB
{ HDD_CUSTOM_DRIVE_IDX,
      DriveIdent{ "IBMLTR", "CUSTOM TYPE " STR(HDD_CUSTOM_DRIVE_IDX), "1.0",
      "IBMULATOR CUSTOM TYPE " STR(HDD_CUSTOM_DRIVE_IDX), "1", "1.0"
      }
}
};

/* Disk types 2 and 10, and 13 and 16, have not the same geometry.
 * TODO: implement a different detection mechanism.
 */
const std::map<uint64_t, int> HardDiskDrive::ms_hdd_sizes = {
  { 10653696, 1  },
//{ 10653696, 23 },
//{ 10653696, 25 },
//{ 10653696, 29 },
  { 21270528, 41 },
  { 21272576, 30 },
  { 21307392, 13 },
//{ 21307392, 16 },
//{ 21307392, 24 },
//{ 21307392, 26 },
  { 21405696, 36 },
  { 21411840, 2  },
//{ 21411840, 6  },
//{ 21411840, 10 },
  { 21427200, 34 },
  { 21430272, 42 },
  { 31122432, 35 },
  { 31150080, 38 },
  { 31436800, 33 },
  { 31900160, 8  },
//{ 31900160, 20 },
//{ 31900160, 22 },
  { 32117760, 3  },
  { 32169984, 7  },
  { 37209600, 11 },
  { 42412032, 40 },
  { 42475520, 28 },
  { 42519040, 17 },
  { 42522624, 39 },
  { 42527744, 27 },
  { 44520960, 32 },
  { 44599296, 31 },
  { 44660224, 14 },
//{ 44660224, 21 },
  { 46325760, 37 },
  { 49090560, 5  },
  { 52093440, 12 },
  { 59526656, 18 },
  { 62390272, 19 },
  { 65454080, 4  },
  { 84824064, 44 },
  { 85063680, 43 },
  { 117504000,9  }
};

HardDiskDrive::HardDiskDrive()
:
StorageDev(),
m_type(0),
m_spin_up_duration(0.0),
m_save_on_close(false),
m_read_only(true),
m_tmp_disk(false),
m_ctrl(nullptr),
m_fx_enabled(false)
{
	m_sector_data = 512;
	m_sector_size = HDD_SECTOR_SIZE;
	m_track_overhead = HDD_TRACK_OVERHEAD;
	m_disk_radius = HDD_DISK_RADIUS;
	m_head_speed_factor = HDD_HEAD_SPEED;
	m_head_accel_factor = HDD_HEAD_ACCEL;
}

HardDiskDrive::~HardDiskDrive()
{
}

void HardDiskDrive::install(StorageCtrl *_ctrl)
{
	m_ctrl = _ctrl;
	m_fx_enabled = g_program.config().get_bool(SOUNDFX_SECTION, SOUNDFX_ENABLED);
	if(m_fx_enabled) {
		m_fx.install(m_name);
	}
}

void HardDiskDrive::remove()
{
	unmount(m_save_on_close, m_read_only);
	
	if(m_fx_enabled) {
		m_fx.remove();
	}
}

void HardDiskDrive::power_on(uint64_t _time)
{
	StorageDev::power_on(_time);

	if(m_disk) {
		m_s.power_on_time = _time+1;
		if(m_fx_enabled) {
			m_fx.spin(true, true);
		}
	} else {
		m_s.power_on_time = 0;
	}
}

void HardDiskDrive::power_off()
{
	StorageDev::power_off();

	if(m_fx_enabled && m_disk) {
		m_fx.spin(false, true);
	}
}

uint64_t HardDiskDrive::power_up_eta_us() const
{
	if(m_s.power_on_time) {
		uint64_t now = g_machine.get_virt_time_us_mt();
		uint64_t elapsed = 0;
		if(now >= m_s.power_on_time) {
			elapsed = now - m_s.power_on_time;
		}
		if(elapsed >= m_spin_up_duration) {
			return 0;
		}
		return (m_spin_up_duration - elapsed);
	}
	return 0;
}

void HardDiskDrive::config_changed(const char *_section)
{
	m_save_on_close = g_program.config().get_bool(_section, DISK_SAVE);
	m_read_only = g_program.config().get_bool(_section, DISK_READONLY);

	// unmount and save the current image
	unmount(m_save_on_close, m_read_only);

	m_section = _section;

	std::string path_string = g_program.config().get_string(_section, DISK_PATH);
	if(path_string != "auto") {
		m_path = g_program.config().find_media(_section, DISK_PATH);
	}

	int type = 0;
	try {
		type = g_program.config().try_int(_section, DISK_TYPE);
	} catch(std::exception &) {
		std::string type_string = g_program.config().get_string(_section, DISK_TYPE);
		if(type_string == "custom") {
			type = HDD_CUSTOM_DRIVE_IDX;
		} else if(type_string == "auto") {
			type = g_machine.model().hdd_type;
			if(path_string!="auto" && FileSys::file_exists(m_path.c_str())) {
				// the user specified an image file with automatic type, try to
				// determine the standard type using the image size.
				uint64_t size = FileSys::get_file_size(m_path.c_str());
				try {
					type = ms_hdd_sizes.at(size);
				} catch(std::out_of_range &) {
					PERRF(LOG_HDD, "%s: I cannot determine the type of '%s'\n",
							name(), m_path.c_str());
					throw;
				}
			}
		}
	}
	if(type<=0 || type == 15 ||
	  (type > HDD_DRIVES_TABLE_SIZE && type != HDD_CUSTOM_DRIVE_IDX))
	{
		PERRF(LOG_HDD, "%s: invalid HDD type: %d\n", name(), type);
		throw std::exception();
	}

	m_type = type;
	m_tmp_disk = false;

	if(path_string == "auto") {
		std::stringstream ss;
		ss << "hdd-type" << m_type << "-";
		std::string imgname = std::regex_replace(g_machine.model().name,
				std::regex("[\\s]"), "_");
		std::regex_replace(std::ostreambuf_iterator<char>(ss),
				imgname.begin(), imgname.end(),
				std::regex("[^A-Za-z0-9\\-_]"),
				"");
		ss << ".img";
		imgname = ss.str();
		//transform to lowercase?
		//std::transform(imgname.begin(), imgname.end(), imgname.begin(), ::tolower);
		m_path = g_program.config().get_file_path(imgname, FILE_TYPE_USER);
	}

	get_profile(type, _section, m_geometry, m_performance);
	StorageDev::config_changed(_section);

	mount(m_path, m_geometry, m_read_only);

	auto model = ms_hdd_models.find(type);
	if(model != ms_hdd_models.end()) {
		m_ident = model->second;
	} else {
		// other standard types
		m_ident = ms_hdd_models.at(0);
		std::stringstream ss;
		ss << ms_hdd_models.at(0).product << type;
		m_ident.set_product(ss.str().c_str());
		ss.str("");
		ss << ms_hdd_models.at(0).model << type;
		m_ident.set_model(ss.str().c_str());
	}

	if(m_fx_enabled) {
		m_fx.config_changed();
		m_spin_up_duration = g_program.config().get_real(_section, DISK_SPINUP_TIME,
				m_fx.spin_up_time_us()/1e6) * 1e6;
	} else {
		m_spin_up_duration = g_program.config().get_real(_section, DISK_SPINUP_TIME, 10) * USEC_PER_SECOND;
	}

	PINFOF(LOG_V0, LOG_HDD, "Installed %s as type %d%s\n", name(), m_type, m_type==HDD_CUSTOM_DRIVE_IDX?" (custom)":"");
	PINFOF(LOG_V0, LOG_HDD, "  Interface: %s\n", m_ctrl->name());
	PINFOF(LOG_V0, LOG_HDD, "  Capacity: %.1fMB, %.1fMiB, %lu sectors\n",
			double(size())/(1000.0*1000.0), double(size())/(1024.0*1024.0), m_sectors);
	PINFOF(LOG_V0, LOG_HDD, "  Geometry: C:%u, H:%u, S:%u\n",
			m_geometry.cylinders, m_geometry.heads, m_geometry.spt);
	PINFOF(LOG_V1, LOG_HDD, "  Model: %s\n", m_ident.model);
	PINFOF(LOG_V2, LOG_HDD, "  Data bits per track: %u\n", m_geometry.spt*512*8);
	PINFOF(LOG_V1, LOG_HDD, "  Interleave: %u:1\n", m_performance.interleave);
	PINFOF(LOG_V1, LOG_HDD, "  Performance characteristics:\n");
	PINFOF(LOG_V1, LOG_HDD, "    rotational speed: %u rpm\n", m_performance.rot_speed);
	PINFOF(LOG_V1, LOG_HDD, "    maximum seek time: %.1f ms\n", m_performance.seek_max);
	PINFOF(LOG_V1, LOG_HDD, "    track-to-track seek time: %.1f ms\n", m_performance.seek_trk);
	PINFOF(LOG_V2, LOG_HDD, "      seek overhead time: %u us\n", m_performance.seek_overhead_us);
	PINFOF(LOG_V2, LOG_HDD, "      seek avgspeed time: %u us/cyl\n", m_performance.seek_avgspeed_us);
	PINFOF(LOG_V2, LOG_HDD, "    track read time (rot.lat.): %u us\n", m_performance.trk_read_us);
	PINFOF(LOG_V2, LOG_HDD, "    sector read time: %u us\n", m_performance.sec_read_us);
	PDEBUGF(LOG_V2, LOG_HDD,"    spin up time: %u us\n", m_spin_up_duration);

	g_program.config().set_int(_section, DISK_TYPE, m_type);

	g_program.config().set_string(_section, DISK_PATH, m_path);
	g_program.config().set_int(_section, DISK_CYLINDERS, m_geometry.cylinders);
	g_program.config().set_int(_section, DISK_HEADS, m_geometry.heads);
	g_program.config().set_int(_section, DISK_SPT, m_geometry.spt);

	g_program.config().set_real(_section, DISK_SEEK_MAX, m_performance.seek_max);
	g_program.config().set_real(_section, DISK_SEEK_TRK, m_performance.seek_trk);
	g_program.config().set_int(_section, DISK_ROT_SPEED, m_performance.rot_speed);
	g_program.config().set_int(_section, DISK_INTERLEAVE, m_performance.interleave);
}

void HardDiskDrive::save_state(StateBuf &_state)
{
	PINFOF(LOG_V1, LOG_HDD, "%s: saving state\n", name());

	_state.write(&m_s, {sizeof(m_s), "Hard Disk Drive"});

	if(m_disk) {
		std::string path = _state.get_basename() + "-" + m_section + ".img";
		m_disk->save_state(path.c_str());
	}
}

void HardDiskDrive::restore_state(StateBuf &_state)
{
	PINFOF(LOG_V1, LOG_HDD, "%s: restoring state\n", name());

	if(m_fx_enabled) {
		m_fx.clear_events();
	}

	// restore_state comes after config_changed, so:
	// 1. the old disk has been serialized and unmounted
	// 2. a new disk is mounted, with path in m_imgpath
	// 3. geometry and performance are determined
	if(m_type > 0) {
		assert(m_disk != nullptr);
		std::string imgfile = _state.get_basename() + "-" + m_section + ".img";
		if(!FileSys::file_exists(imgfile.c_str())) {
			PERRF(LOG_HDD, "%s: unable to find state image %s\n", name(), imgfile.c_str());
			throw std::exception();
		}
		MediaGeometry geom = m_disk->geometry();
		m_disk.reset(nullptr); // this calls the destructor and closes the file
		//the saved state is read only
		mount(imgfile, geom, true);
		if(m_fx_enabled) {
			m_fx.spin(true, false);
		}
	} else if(m_fx_enabled) {
		m_fx.spin(false, false);
	}

	_state.read(&m_s, {sizeof(m_s), "Hard Disk Drive"});
}

void HardDiskDrive::get_profile(int _type_id, const char *_section,
		MediaGeometry &_geom, DrivePerformance &_perf)
{
	if(ms_hdd_performance.find(_type_id) != ms_hdd_performance.end()) {
		_perf = ms_hdd_performance.at(_type_id);
		_geom = ms_hdd_types[_type_id];
		// ATA drives have a typical interleave of 1:1
		// TODO this is a stub. implement proper hdd performance characteristics
		if(dynamic_cast<StorageCtrl_ATA*>(m_ctrl) != nullptr) {
			_perf.interleave = 1;
		}
	} else if(_type_id>0 && _type_id!=15 && _type_id<=HDD_CUSTOM_DRIVE_IDX) {
		if(_type_id == HDD_CUSTOM_DRIVE_IDX) {
			_geom.cylinders = g_program.config().get_int(_section, DISK_CYLINDERS);
			_geom.heads = g_program.config().get_int(_section, DISK_HEADS);
			_geom.spt = g_program.config().get_int(_section, DISK_SPT);
			_geom.wpcomp = 0xFFFF;
			_geom.lzone = _geom.cylinders;
			PINFOF(LOG_V1, LOG_HDD, "%s: custom geometry: C=%d H=%d S=%d\n",
				name(), _geom.cylinders, _geom.heads, _geom.spt);
		} else if(_type_id<HDD_DRIVES_TABLE_SIZE) {
			_geom = ms_hdd_types[_type_id];
		} else {
			PERRF(LOG_HDD, "%s: invalid drive type: %d\n", name(), _type_id);
			throw std::exception();
		}
		bool is_ata = (dynamic_cast<StorageCtrl_ATA*>(m_ctrl) != nullptr);
		if(is_ata) {
			_perf = default_ata_perf;
		} else {
			_perf = default_ps1_perf;
		}
	} else {
		PERRF(LOG_HDD, "Invalid drive type: %d\n", _type_id);
		throw std::exception();
	}

	float seek_max = g_program.config().get_real(_section, DISK_SEEK_MAX, -1.);
	float seek_trk = g_program.config().get_real(_section, DISK_SEEK_TRK, -1.);
	unsigned rot_speed = g_program.config().get_int(_section, DISK_ROT_SPEED, 0);
	unsigned interleave = g_program.config().get_int(_section, DISK_INTERLEAVE, 0);
	if(seek_max > 0.) {
		_perf.seek_max = seek_max;
	}
	if(seek_trk > 0.) {
		_perf.seek_trk = seek_trk;
	}
	if(rot_speed > 0) {
		_perf.rot_speed = rot_speed;
	}
	if(_perf.rot_speed < 3600) {
		_perf.rot_speed = 3600;
		PINFOF(LOG_V0, LOG_HDD, "rotational speed set to the minimum: %u RPM\n", _perf.rot_speed);
	} else if(_perf.rot_speed > 7200) {
		_perf.rot_speed = 7200;
		PINFOF(LOG_V0, LOG_HDD, "rotational speed set to the maximum: %u RPM\n", _perf.rot_speed);
	}
	if(interleave > 0) {
		_perf.interleave = interleave;
	}

	if(_geom.cylinders == 0 || _geom.cylinders > HDD_MAX_CYLINDERS) {
		PERRF(LOG_HDD, "Cylinders must be within 1 and %d: %d\n", HDD_MAX_CYLINDERS,_geom.cylinders);
		throw std::exception();
	}
	if(_geom.heads == 0 || _geom.heads > HDD_MAX_HEADS) {
		PERRF(LOG_HDD, "Heads must be within 1 and %d: %d\n", HDD_MAX_HEADS, _geom.heads);
		throw std::exception();
	}
	if(_geom.spt == 0 || _geom.spt > HDD_MAX_SECTORS) {
		PERRF(LOG_HDD, "Sectors must be within 1 and %d: %d\n", HDD_MAX_SECTORS, _geom.spt);
		throw std::exception();
	}
}

void HardDiskDrive::mount(std::string _imgpath, MediaGeometry _geom, bool _read_only)
{
	if(_imgpath.empty()) {
		PERRF(LOG_HDD, "You need to specify a HDD image file\n");
		throw std::exception();
	}
	if(FileSys::is_directory(_imgpath.c_str())) {
		PERRF(LOG_HDD, "Cannot use a directory as an image file\n");
		throw std::exception();
	}

	m_disk = std::unique_ptr<FlatMediaImage>(new FlatMediaImage());
	m_disk->geometry() = _geom;

	if(!FileSys::file_exists(_imgpath.c_str())) {
		PINFOF(LOG_V0, LOG_HDD, "Creating new image file '%s'\n", _imgpath.c_str());
		if(HAVE_LIBARCHIVE && m_type < HDD_DRIVES_TABLE_SIZE) {
			std::string imgsrc = g_program.config().get_file_path(HDD_IMAGES_ARCHIVE, FILE_TYPE_ASSET);
			if(!FileSys::file_exists(imgsrc.c_str())) {
				PERRF(LOG_HDD, "Cannot find the image file archive " HDD_IMAGES_ARCHIVE "\n");
				throw std::exception();
			}
			std::stringstream ss;
			ss << "hdd-type" << m_type << ".img";
			if(!FileSys::extract_file(imgsrc.c_str(), ss.str().c_str(), _imgpath.c_str())) {
				PERRF(LOG_HDD, "Cannot extract image file '%s' from " HDD_IMAGES_ARCHIVE "\n", ss.str().c_str());
				throw std::exception();
			}
		} else {
			//create a new image
			try {
				m_disk->create(_imgpath.c_str(), m_sectors);
				PINFOF(LOG_V0, LOG_HDD, "The image is not pre-formatted: use FDISK and FORMAT\n");
			} catch(std::exception &e) {
				PERRF(LOG_HDD, "Unable to create the image file\n");
				throw;
			}
		}
	} else {
		PINFOF(LOG_V0, LOG_HDD, "Using image file '%s'\n", _imgpath.c_str());
	}

	if(_read_only || !FileSys::is_file_writeable(_imgpath.c_str()))	{
		PINFOF(LOG_V1, LOG_HDD, "The image file is read-only, using a replica\n");

		std::string dir, base, ext;
		if(!FileSys::get_path_parts(_imgpath.c_str(), dir, base, ext)) {
			PERRF(LOG_HDD, "Error while determining the image file path\n");
			throw std::exception();
		}
		std::string tpl = g_program.config().get_cfg_home()
		                + FS_SEP + base + "-XXXXXX";

		//opening a temp image
		//this works in C++11, where strings are guaranteed to be contiguous:
		if(dynamic_cast<FlatMediaImage*>(m_disk.get())->open_temp(_imgpath.c_str(), tpl) < 0) {
			PERRF(LOG_HDD, "Can't open the image file\n");
			throw std::exception();
		}
		m_tmp_disk = true;
	} else {
		if(m_disk->open(_imgpath.c_str()) < 0) {
			PERRF(LOG_HDD, "Error opening the image file\n");
			throw std::exception();
		}
	}
}

void HardDiskDrive::unmount(bool _save, bool _read_only)
{
	if(!m_disk || !m_disk->is_open()) {
		return;
	}

	if(m_tmp_disk) {
		if(!_save) {
			PINFOF(LOG_V0, LOG_HDD,
					"Disk image file for %s not saved because '" DISK_SAVE "' option is set to false in the configuration file\n",
					name());
		} else if(_read_only) {
			PINFOF(LOG_V0, LOG_HDD,
					"Disk image file for %s not saved because '" DISK_READONLY "' option is set to true in the configuration file\n",
					name());
		} else {
			// make the current disk state permanent.
			bool save = true;
			if(FileSys::file_exists(m_path.c_str())) {
				if(FileSys::get_file_size(m_path.c_str()) != size()) {
					//TODO this is true only for flat media images
					PINFOF(LOG_V0, LOG_HDD, "%s: disk geometry mismatch, temporary image not saved!\n",
							name());
					save = false;
				}
				if(!FileSys::is_file_writeable(m_path.c_str())) {
					PINFOF(LOG_V0, LOG_HDD, "%s: image file is write protected, temporary image not saved!\n",
							name());
					save = false;
				}
			}
			if(save) {
				PINFOF(LOG_V0, LOG_HDD,
						"Saving %s image to '%s'\n", name(), m_path.c_str());
				m_disk->save_state(m_path.c_str());
			}
		}
	}

	m_disk->close();
	if(m_tmp_disk) {
		PDEBUGF(LOG_V0, LOG_HDD, "Removing temporary image file '%s'\n", m_disk->get_name().c_str());
		if(FileSys::remove(m_disk->get_name().c_str()) != 0) {
			PERRF(LOG_HDD, "Error removing temporary image file '%s'\n", m_disk->get_name().c_str());
		}
	}
	m_disk.reset(nullptr);
}

void HardDiskDrive::read_sector(int64_t _lba, uint8_t *_buffer, unsigned _len)
{
	assert(_lba < m_sectors);
	assert(_buffer != nullptr);
	assert(_len == 512);

	int64_t offset = _lba * _len;
	int64_t pos = m_disk->lseek(offset, SEEK_SET);
	if(pos != offset) {
		PERRF(LOG_HDD, "%s: could not seek image file at byte %lu\n",
				name(), offset);
		throw std::exception();
	}
	ssize_t res = m_disk->read(_buffer, _len);
	if(res != _len) {
		PERRF(LOG_HDD, "%s: could not read image file at byte %lu\n",
				name(), offset);
		throw std::exception();
	}
}

void HardDiskDrive::write_sector(int64_t _lba, uint8_t *_buffer, unsigned _len)
{
	assert(_lba < m_sectors);
	assert(_buffer != nullptr);
	assert(_len == 512);

	int64_t offset = _lba * _len;
	int64_t pos = m_disk->lseek(offset, SEEK_SET);
	if(pos != offset) {
		PERRF(LOG_HDD, "%s: could not seek image file at byte %lu\n",
				name(), offset);
		throw std::exception();
	}
	ssize_t res = m_disk->write(_buffer, _len);
	if(res != _len) {
		PERRF(LOG_HDD, "%s: could not write image file at byte %lu\n",
				name(), offset);
		throw std::exception();
	}
}

void HardDiskDrive::seek(unsigned _from_cyl, unsigned _to_cyl)
{
	if(m_fx_enabled) {
		m_fx.seek(_from_cyl, _to_cyl, m_disk->geometry().cylinders);
	}
}

int64_t HardDiskDrive::get_hdd_type_size(int _hdd_type)
{
	if(_hdd_type >= HDD_DRIVES_TABLE_SIZE) {
		return 0;
	}
	const MediaGeometry & geom = ms_hdd_types[_hdd_type];
	int64_t sectors = uint64_t(geom.spt) * geom.cylinders * geom.heads;
	return sectors*512;
}
