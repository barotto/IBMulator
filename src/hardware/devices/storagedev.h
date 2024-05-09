/*
 * Copyright (C) 2016-2024  Marco Bortolin
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

#ifndef IBMULATOR_HW_STORAGEDEV_H
#define IBMULATOR_HW_STORAGEDEV_H

#include "mediaimage.h"
#include "statebuf.h"

class StorageCtrl;

struct DrivePerformance
{
	double seek_max_ms;   // Full stroke (maximum) seek time in milliseconds
	double seek_trk_ms;   // Track to track seek time in milliseconds
	double seek_third_ms; // 1/3 stroke seek time in milliseconds
	double rot_speed;     // Rotational speed in RPM
	double interleave;    // Interleave ratio

	// these are derived values in microseconds calculated from the above:
	double seek_avgspeed_us; // Seek average speed
	double seek_overhead_us; // Seek overhead time
	double trk2trk_us;       // Seek track to track time
	double trk_read_us;      // Full track read time
	double avg_rot_lat_us;   // Average rotational latency
	double sec_read_us;      // Sector read time
	double sec_xfer_us;      // Sector transfer time, taking interleave into account
	double sec2sec_us;       // Time needed to pass from a sector to the next
	double bytes_per_us;     // Bytes read per microsecond

	void update(const MediaGeometry &_geometry, double _raw_sector_bytes, double _track_overhead_bytes);
};

/**
 * Drive identification data.
 * Used for the ATA controller and general logging.
 * All strings are ASCII character padded with 20h (space) and null terminated.
 */
struct DriveIdent
{
	char vendor[9];   // Vendor name
	char product[17]; // Product id
	char revision[5]; // Product revision
	char model[41];   // Model name
	char serial[21];  // Serial number
	char firmware[9]; // Firmware revision

	//DriveIdent();

	void set_vendor(const char *_str) { set_string(vendor, _str, 8); }
	void set_product(const char *_str) { set_string(product, _str, 16); }
	void set_revision(const char *_str) { set_string(revision, _str, 4); }
	void set_model(const char *_str) { set_string(model, _str, 40); }
	void set_serial(const char *_str) { set_string(serial, _str, 20); }
	void set_firmware(const char *_str) { set_string(firmware, _str, 8); }

	void set_string(char *_dest, const char *_src, size_t _len);
	DriveIdent & operator=(const DriveIdent &);
};

/**
 * Disk storage device (hard drives, cd-rom drives).
 *
 * Timings are for CAV devices with a constant number of sectors per track.
 */
class StorageDev
{
public:
	enum DeviceCategory {
		DEV_NONE,
		DEV_HDD,
		DEV_CDROM
	};

protected:
	std::string m_ini_section;
	StorageCtrl *m_controller = nullptr;
	uint8_t m_drive_index = 0;
	DeviceCategory m_category = DEV_NONE;
	std::string m_name; // Device name (used for logging)
	std::string m_path; // File system path to data

	int64_t m_sectors = 0;        // Total number of sectors
	int m_sector_data = 0;        // Data bytes per sector
	double m_sector_size = .0;    // Total sector byte size (data + control fields)
	double m_sector_len = .0;     // Physical length of a sector relative to a track
	double m_disk_radius = .0;    // Disk radius in mm
	double m_track_overhead = .0; // Additional control bytes per track

	// Head factors are used to extrapolate the performance characteristics of a
	// storage device starting from known data measured from real world.
	// If we know how a disk with a certain geometry performs, we can guess
	// a similar device but with a different geometry.
	double m_head_speed_factor = .0; // Used to derive the head max speed
	double m_head_accel_factor = .0; // Used to derive the head acceleration

	DriveIdent m_ident = {};
	MediaGeometry m_geometry = {};
	DrivePerformance m_performance = {};

	void set_geometry(const MediaGeometry &_geometry, double _raw_sector_bytes, double _track_overhead_bytes);

	bool m_fx_enabled = false;

public:
	StorageDev(DeviceCategory _category) : m_category(_category) {}
	virtual ~StorageDev() {}

	virtual void install(StorageCtrl *_ctrl, uint8_t _id, const char * _ini_section);
	virtual void remove() {}
	virtual void power_on(uint64_t /*_time*/) {}
	virtual void power_off() {}
	virtual void config_changed() {}
	virtual void save_state(StateBuf &) {}
	virtual void restore_state(StateBuf &) {}
	virtual bool is_read_only() const { return true; }
	virtual bool is_dirty(bool /*_since_restore*/) const { return false; }
	virtual void commit() const {}

	DeviceCategory category() const { return m_category; }
	const char * name() const { return m_name.c_str(); }
	const char * path() const { return m_path.c_str(); }
	const char * vendor() const { return m_ident.vendor; }
	const char * product() const { return m_ident.product; }
	const char * revision() const { return m_ident.revision; }
	const char * model() const { return m_ident.model; }
	const char * serial() const { return m_ident.serial; }
	const char * firmware() const { return m_ident.firmware; }

	const MediaGeometry & geometry() const { return m_geometry; }
	const DrivePerformance & performance() const { return m_performance; }

	virtual int64_t sectors() const { return m_sectors; } // total count of sectors 
	virtual int64_t max_lba() const { return m_sectors - 1; } // max addressable LBA

	virtual void set_name(const char *_name) { m_name = _name; }
	virtual uint32_t seek_move_time_us(unsigned _cur_cyl, unsigned _dest_cyl);
	virtual uint32_t rotational_latency_us(double _start_hw_sector, unsigned _dest_log_sector);
	virtual uint32_t transfer_time_us(uint64_t _curr_time, int64_t _xfer_lba_sector, int64_t _xfer_amount);
	virtual uint32_t transfer_time_us(uint64_t _curr_time, int64_t _xfer_lba_sector,
			int64_t _xfer_amount, uint64_t &_look_ahead_time, bool _rot_latency=true);
	virtual double head_position(double _last_pos, uint32_t _elapsed_time_us) const;
	virtual double head_position(uint64_t _time_us) const;

	virtual uint64_t power_up_eta_us() const { return 0; }
	virtual bool is_powering_up() const { return (power_up_eta_us() > 0); }

	virtual bool read_sector(int64_t /*_lba*/, uint8_t * /*_buffer*/, unsigned /*_len*/) { return false; }
	virtual bool write_sector(int64_t /*_lba*/, uint8_t * /*_buffer*/, unsigned /*_len*/) { return false; }
	virtual void seek(unsigned /*_from_cyl*/, unsigned /*_to_cyl*/) {}
	virtual void seek(int64_t /*_lba*/) {}

	int64_t chs_to_lba(int64_t _c, int64_t _h, int64_t _s) const;
	int chs_to_hw_sector(int _sector) const;
	double hw_sect_to_pos(double _hw_sector) const;
	int64_t lba_to_cylinder(int64_t _lba) const;
	int64_t lba_to_head(int64_t _lba) const;
	void lba_to_chs(int64_t _lba, int64_t &c_, int64_t &h_, int64_t &s_) const;
	double pos_to_hw_sect(double _head_pos) const;
};

#endif
