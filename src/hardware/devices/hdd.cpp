/*
 * Copyright (C) 2016  Marco Bortolin
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
#include <cstring>

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
const std::map<uint, HDDPerformance> HardDiskDrive::ms_hdd_performance = {
{ 35, { 40.0f, 8.0f, 3600, 4, 5.0f, 0,0,0,0,0,0 } }, //35 30MB
{ 38, { 40.0f, 9.0f, 3700, 4, 5.0f, 0,0,0,0,0,0 } }, //38 30MB
{ 39, {  0.0f, 0.0f,    0, 0, 0.0f, 0,0,0,0,0,0 } }, //39 41MB
};


HardDiskDrive::HardDiskDrive()
{
	memset(&m_s, 0, sizeof(m_s));
}

HardDiskDrive::~HardDiskDrive()
{
	unmount();
}

void HardDiskDrive::set_space_time(double _head_pos, uint64_t _head_time)
{
	m_s.head_pos = _head_pos;
	m_s.head_time = _head_time;
}

unsigned HardDiskDrive::chs_to_lba(unsigned _c, unsigned _h, unsigned _s) const
{
	assert(_s>0);
	return (_c * m_disk->geometry.heads + _h ) * m_disk->geometry.spt + (_s-1);
}

void HardDiskDrive::lba_to_chs(unsigned _lba, unsigned &c_, unsigned &h_, unsigned &s_) const
{
	c_ = _lba / (m_disk->geometry.heads * m_disk->geometry.spt);
	h_ = (_lba / m_disk->geometry.spt) % m_disk->geometry.heads;
	s_ = (_lba % m_disk->geometry.spt) + 1;
}

double HardDiskDrive::pos_to_sect(double _head_pos) const
{
	double sector = double(m_disk->geometry.spt) + HDD_TRACK_OVERHEAD/double(HDD_SECTOR_SIZE);
	return (_head_pos * sector);
}

double HardDiskDrive::sect_to_pos(double _hw_sector) const
{
	return (_hw_sector * m_sect_size);
}

int HardDiskDrive::hw_sector_number(int _logical_sector) const
{
	return (((_logical_sector-1)*m_performance.interleave) % m_disk->geometry.spt);
}

double HardDiskDrive::head_position(double _last_pos, uint32_t _elapsed_time_us) const
{
	double cur_pos = _last_pos + (double(_elapsed_time_us) / m_performance.trk_read_us);
	cur_pos = cur_pos - floor(cur_pos);
	return cur_pos;
}

double HardDiskDrive::head_position(uint64_t _time_us) const
{
	assert(_time_us >= m_s.head_time);
	uint32_t elapsed_time_us = _time_us - m_s.head_time;
	return head_position(m_s.head_pos, elapsed_time_us);
}

double HardDiskDrive::head_position() const
{
	return m_s.head_pos;
}

void HardDiskDrive::install()
{
	m_fx.install();
	m_spin_up_duration = m_fx.spin_up_time_us();
}

void HardDiskDrive::remove()
{
	unmount();
	m_fx.remove();
}

void HardDiskDrive::power_on(uint64_t _time)
{
	memset(&m_s, 0, sizeof(m_s));
	if(m_disk) {
		m_s.power_on_time = _time+1;
		m_fx.spin(true, true);
	}
}

void HardDiskDrive::power_off()
{
	if(m_disk) {
		m_fx.spin(false, true);
	}
	memset(&m_s, 0, sizeof(m_s));
}

uint64_t HardDiskDrive::spin_up_eta_us() const
{
	if(m_s.power_on_time) {
		uint64_t elapsed = g_machine.get_virt_time_us_mt() - m_s.power_on_time;
		if(elapsed >= m_spin_up_duration) {
			return 0;
		}
		return (m_spin_up_duration - elapsed);
	}
	return 0;
}

void HardDiskDrive::config_changed()
{
	// unmount the current image and serialize it if needed
	unmount();

	//get_enum throws if value is not allowed:
	m_type = g_program.config().get_int(DRIVES_SECTION, DRIVES_HDD);
	if(m_type<0 || m_type == 15 ||
	  (m_type > HDD_DRIVES_TABLE_SIZE && m_type != HDD_CUSTOM_DRIVE_IDX))
	{
		PERRF(LOG_HDD, "Invalid HDD type %d\n", m_type);
		throw std::exception();
	}

	m_fx.config_changed();

	if(m_type == 0) {
		PINFOF(LOG_V0, LOG_HDD, "Drive C not installed\n");
		return;
	}

	MediaGeometry geom;
	HDDPerformance perf;
	get_profile(m_type, geom, perf);

	m_imgpath = g_program.config().find_media(DISK_C_SECTION, DISK_PATH);
	// read only is a start up configuration property
	bool read_only = g_program.config(0).get_bool(DISK_C_SECTION, DISK_READONLY);
	mount(m_imgpath, geom, read_only);

	m_tmp_disk = false;
	m_sectors = geom.spt * geom.cylinders * geom.heads;
	m_performance = perf;
	m_performance.trk_read_us = round(6.0e7 / perf.rot_speed);
	m_performance.trk2trk_us = perf.seek_trk * 1000.0;

	/* Track seek phases:
	 * 1. acceleration (the disk arm gets moving);
	 * 2. coasting (the arm is moving at full speed);
	 * 3. deceleration (the arm slows down);
	 * 4. settling (the head is positioned over the correct track).
	 * Here we divide the total seek time in 2 values: avgspeed and overhead,
	 * derived from the only 2 values given in HDD specifications:
	 * track-to-track and maximum (full stroke).
	 *
	 * trk2trk = overhead + avgspeed
	 * maximum = overhead + avgspeed*(ncyls-1)
	 *
	 * overhead = trk2trk - avgspeed
	 * avgspeed = (maximum - trk2trk) / (ncyls-2)
	 *
	 * So the average speed includes points 1,2,3.
	 */
	m_performance.seek_avgspeed_us = round(((perf.seek_max-perf.seek_trk) / double(geom.cylinders-2)) * 1000.0);
	m_performance.seek_overhead_us = m_performance.trk2trk_us - m_performance.seek_avgspeed_us;

	double bytes_pt = (geom.spt*HDD_SECTOR_SIZE + HDD_TRACK_OVERHEAD);
	double bytes_us = bytes_pt / double(m_performance.trk_read_us);
	m_performance.sec_read_us = round(HDD_SECTOR_SIZE / bytes_us);
	m_sect_size = (1.0 / bytes_pt) * HDD_SECTOR_SIZE;
	m_performance.sec_xfer_us = double(m_performance.sec_read_us) * std::max(1.0,(double(perf.interleave) * 0.8f));

	PDEBUGF(LOG_V0, LOG_HDD, "Performance characteristics:\n");
	PDEBUGF(LOG_V0, LOG_HDD, "  track-to-track seek time: %d us\n", m_performance.trk2trk_us);
	PDEBUGF(LOG_V0, LOG_HDD, "    seek overhead time: %d us\n", m_performance.seek_overhead_us);
	PDEBUGF(LOG_V0, LOG_HDD, "    seek avgspeed time: %d us/cyl\n", m_performance.seek_avgspeed_us);
	PDEBUGF(LOG_V0, LOG_HDD, "  track read time (rot.lat.): %d us\n", m_performance.trk_read_us);
	PDEBUGF(LOG_V0, LOG_HDD, "  sector read time: %d us\n", m_performance.sec_read_us);
	PDEBUGF(LOG_V0, LOG_HDD, "  command overhead: %d us\n", int(perf.overh_time*1000.0));

	PINFOF(LOG_V0, LOG_HDD, "Installed drive C as type %d (%.1fMiB)\n",
			m_type, double(m_disk->hd_size)/(1024.0*1024.0));
	PINFOF(LOG_V1, LOG_HDD, "  Cylinders: %d\n", geom.cylinders);
	PINFOF(LOG_V1, LOG_HDD, "  Heads: %d\n", geom.heads);
	PINFOF(LOG_V1, LOG_HDD, "  Sectors per track: %d\n", geom.spt);
	PINFOF(LOG_V2, LOG_HDD, "  Rotational speed: %d RPM\n", perf.rot_speed);
	PINFOF(LOG_V2, LOG_HDD, "  Interleave: %d:1\n", perf.interleave);
	PINFOF(LOG_V2, LOG_HDD, "  Overhead time: %.1f ms\n", perf.overh_time);
	PINFOF(LOG_V2, LOG_HDD, "  data bits per track: %d\n", geom.spt*512*8);
}

void HardDiskDrive::save_state(StateBuf &_state)
{
	PINFOF(LOG_V1, LOG_HDD, "HDD: saving state\n");

	_state.write(&m_s, {sizeof(m_s), "Hard Disk Drive"});

	if(m_disk) {
		std::string path = _state.get_basename() + "-hdd.img";
		m_disk->save_state(path.c_str());
	}
}

void HardDiskDrive::restore_state(StateBuf &_state)
{
	PINFOF(LOG_V1, LOG_HDD, "HDD: restoring state\n");

	m_fx.clear_events();

	// restore_state comes after config_changed, so:
	// 1. the old disk has been serialized and unmounted
	// 2. a new disk is mounted, with path in m_imgpath
	// 3. geometry and performance are determined
	if(m_type > 0) {
		assert(m_disk != nullptr);
		std::string imgfile = _state.get_basename() + "-hdd.img";
		if(!FileSys::file_exists(imgfile.c_str())) {
			PERRF(LOG_HDD, "Unable to find state image %s\n", imgfile.c_str());
			throw std::exception();
		}
		MediaGeometry geom = m_disk->geometry;
		m_disk.reset(nullptr); // this calls the destructor and closes the file
		//the saved state is read only
		mount(_state.get_basename() + "-hdd.img", geom, true);
		m_fx.spin(true, false);
	} else {
		m_fx.spin(false, false);
	}

	_state.read(&m_s, {sizeof(m_s), "Hard Disk Drive"});
}

void HardDiskDrive::get_profile(int _type_id, MediaGeometry &_geom, HDDPerformance &_perf)
{
	//the only performance values I have are those of type 35 and 38
	if(_type_id == 35 || _type_id == 38) {
		_perf = ms_hdd_performance.at(_type_id);
		_geom = ms_hdd_types[_type_id];
	} else if(_type_id>0 && _type_id!=15 && _type_id<=HDD_CUSTOM_DRIVE_IDX) {
		if(_type_id == HDD_CUSTOM_DRIVE_IDX) {
			_geom.cylinders = g_program.config().get_int(DISK_C_SECTION, DISK_CYLINDERS);
			_geom.heads = g_program.config().get_int(DISK_C_SECTION, DISK_HEADS);
			_geom.spt = g_program.config().get_int(DISK_C_SECTION, DISK_SPT);
			_geom.wpcomp = 0xFFFF;
			_geom.lzone = _geom.cylinders;
			PINFOF(LOG_V1, LOG_HDD, "Custom geometry: C=%d H=%d S=%d\n",
				_geom.cylinders, _geom.heads, _geom.spt);
		} else if(_type_id<HDD_DRIVES_TABLE_SIZE) {
			_geom = ms_hdd_types[_type_id];
		} else {
			PERRF(LOG_HDD, "Invalid drive type: %d\n", _type_id);
			throw std::exception();
		}
		_perf.seek_max = std::max(0., g_program.config().get_real(DISK_C_SECTION, DISK_SEEK_MAX));
		_perf.seek_trk = std::max(0., g_program.config().get_real(DISK_C_SECTION, DISK_SEEK_TRK));
		_perf.rot_speed = std::max(1, g_program.config().get_int(DISK_C_SECTION, DISK_ROT_SPEED));
		if(_perf.rot_speed < 3600) {
			_perf.rot_speed = 3600;
			PINFOF(LOG_V0, LOG_HDD, "rotational speed set to the minimum: %u RPM\n", _perf.rot_speed);
		} else if(_perf.rot_speed > 7200) {
			_perf.rot_speed = 7200;
			PINFOF(LOG_V0, LOG_HDD, "rotational speed set to the maximum: %u RPM\n", _perf.rot_speed);
		}
		_perf.interleave = std::max(1, g_program.config().get_int(DISK_C_SECTION, DISK_INTERLEAVE));
		_perf.overh_time = std::max(0.0, g_program.config().get_real(DISK_C_SECTION, DISK_OVERH_TIME));
	} else {
		PERRF(LOG_HDD, "Invalid drive type: %d\n", _type_id);
		throw std::exception();
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

	set_space_time(0.0, 0);

	m_disk = std::unique_ptr<FlatMediaImage>(new FlatMediaImage());
	m_disk->geometry = _geom;

	if(!FileSys::file_exists(_imgpath.c_str())) {
		PINFOF(LOG_V0, LOG_HDD, "Creating new image file '%s'\n", _imgpath.c_str());
		if(HAVE_LIBARCHIVE && m_type == 35) {
			std::string imgsrc = g_program.config().get_file_path("hdd.img.zip", FILE_TYPE_ASSET);
			if(!FileSys::file_exists(imgsrc.c_str())) {
				PERRF(LOG_HDD, "Cannot find the image file archive 'hdd.img.zip'\n");
				throw std::exception();
			}
			if(!FileSys::extract_file(imgsrc.c_str(), "hdd.img", _imgpath.c_str())) {
				PERRF(LOG_HDD, "Cannot extract the image file 'hdd.img' from the archive\n");
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
		if(dynamic_cast<FlatMediaImage*>(m_disk.get())->open_temp(_imgpath.c_str(), &tpl[0]) < 0) {
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

void HardDiskDrive::unmount()
{
	if(!m_disk || !m_disk->is_open()) {
		return;
	}

	// write protect and save on close are from the start up configuration
	bool write_protect = g_program.config(0).get_bool(DISK_C_SECTION, DISK_READONLY);
	bool save_on_close = g_program.config(0).get_bool(DISK_C_SECTION, DISK_SAVE);
	if(m_tmp_disk) {
		if(!save_on_close) {
			PINFOF(LOG_V0, LOG_HDD,
					"Disk image file not saved because '" DISK_SAVE "' option is set to false in the configuration file\n");
		} else if(write_protect) {
			PINFOF(LOG_V0, LOG_HDD,
					"Disk image file not saved because '" DISK_READONLY "' option is set to true in the configuration file\n");
		} else {
			// make the current disk state permanent.
			bool save = true;
			if(FileSys::file_exists(m_imgpath.c_str())) {
				if(FileSys::get_file_size(m_imgpath.c_str()) != size()) {
					//TODO this is true only for flat media images
					PINFOF(LOG_V0, LOG_HDD, "Disk geometry mismatch, temporary image not saved!\n");
					save = false;
				}
				if(!FileSys::is_file_writeable(m_imgpath.c_str())) {
					PINFOF(LOG_V0, LOG_HDD, "Disk image file is write protected, temporary image not saved!\n");
					save = false;
				}
			}
			if(save) {
				PINFOF(LOG_V0, LOG_HDD,
						"Saving disk image to '%s'\n",m_imgpath.c_str());
				m_disk->save_state(m_imgpath.c_str());
			}
		}
	}

	m_disk->close();
	if(m_tmp_disk) {
		PDEBUGF(LOG_V0, LOG_HDD, "Removing temporary image file '%s'\n", m_disk->get_name().c_str());
		::remove(m_disk->get_name().c_str());
	}
	m_disk.reset(nullptr);
}

uint32_t HardDiskDrive::seek_move_time_us(unsigned _cur_cyl, unsigned _dest_cyl)
{
	/* We assume a linear head movement, but in the real world the head
	 * describes an arc onto the platter surface.
	 */
	const double platter_radius = 32.0; //in mm
	const double cylinder_width = platter_radius / m_disk->geometry.cylinders;
	//speed in mm/ms
	const double avg_speed = platter_radius /
			(((m_disk->geometry.cylinders-1)*m_performance.seek_avgspeed_us)/1000.0);

	/* The following factors were derived from perf measurement of a WDL-330P
	 * specimen.
	 * 0.99378882 = average speed = 32.0 / ((921-1)*35/1000.0), 35=avg speed in us/cyl
	 * 1.6240 = maximum speed in mm/ms
	 * 0.3328 = acceleration in mm/ms^2
	 */
	const double speed_factor = 1.6240 / 0.99378882;
	const double accel_factor = 0.3328 / 0.99378882;
	double max_speed = avg_speed * speed_factor; // mm/ms
	double accel     = avg_speed * accel_factor; // mm/ms^2

	int delta_cyl = abs(int(_cur_cyl) - int(_dest_cyl));
	double distance = double(delta_cyl) * cylinder_width;

	/*  move time = acceleration + coasting at max speed + deceleration
	 */
	uint32_t move_time = 0;
	double acc_space = (max_speed*max_speed) / (2.0*accel);
	double acc_time;
	double coasting_space;
	double coasting_time;

	if(distance < acc_space*2.0) {
		// not enough space to reach max speed
		acc_space = distance / 2.0;
		coasting_space = 0.0;
	} else {
		coasting_space = distance - acc_space*2.0;
	}
	acc_time = sqrt(acc_space/(0.5*accel));
	acc_time *= 2.0; // I assume acceleration = deceleration
	coasting_time = coasting_space / max_speed;

	acc_time *= 1000.0; // ms to us
	coasting_time *= 1000.0;

	move_time = acc_time + coasting_time;

	PDEBUGF(LOG_V2, LOG_HDD, "HDD SEEK MOVE dist:%.2f,acc_space:%.2f,acc_time:%.0f,co_space:%.2f,co_time:%.0f,tot.move:%.0f\n",
			distance, acc_space, acc_time, coasting_space, coasting_time, move_time);

	return move_time;
}

uint32_t HardDiskDrive::rotational_latency_us(
		double _head_position,    // the head position at time0
		unsigned _dest_log_sector // the destination logical sector number
		)
{
	double distance;
	assert(_head_position>=0.f && _head_position<=1.f);

	/* To determine the rotational latency we now need to determine the time
	 * needed to position the head above the desired logical sector.
	 * The logical sector position takes into account the interleave.
	 */
	double dest_hw_sector = ((_dest_log_sector-1)*m_performance.interleave)
			% m_disk->geometry.spt;
	double dest_position = m_sect_size * dest_hw_sector;
	assert(dest_position>=0.f);
	if(_head_position > dest_position) {
		distance = (1.f - _head_position) + dest_position;
	} else {
		distance = dest_position - _head_position;
	}
	assert(distance>=0.f);
	uint32_t latency_us = round(distance * m_performance.trk_read_us);

	return latency_us;
}

void HardDiskDrive::read_sector(unsigned _c, unsigned _h, unsigned _s, uint8_t *_buffer)
{
	assert(_buffer != nullptr);
	unsigned lba = chs_to_lba(_c,_h,_s);
	assert(lba < m_sectors);
	int64_t offset = lba * 512;
	int64_t pos = m_disk->lseek(offset, SEEK_SET);
	assert(pos == offset);
	ssize_t res = m_disk->read(_buffer, 512);
	assert(res == 512);
}

void HardDiskDrive::write_sector(unsigned _c, unsigned _h, unsigned _s, uint8_t *_buffer)
{
	assert(_buffer != nullptr);
	unsigned lba = chs_to_lba(_c,_h,_s);
	assert(lba < m_sectors);
	int64_t offset = lba * 512;
	int64_t pos = m_disk->lseek(offset, SEEK_SET);
	assert(pos == offset);
	ssize_t res = m_disk->write(_buffer, 512);
	assert(res == 512);
}

void HardDiskDrive::seek(unsigned _from_cyl, unsigned _to_cyl)
{
	m_fx.seek(_from_cyl, _to_cyl, m_disk->geometry.cylinders);
}
