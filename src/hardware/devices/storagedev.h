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

#ifndef IBMULATOR_HW_STORAGEDEV_H
#define IBMULATOR_HW_STORAGEDEV_H

#include "mediaimage.h"
#include "statebuf.h"

struct DrivePerformance
{
	float    seek_max;   // Maximum seek time in milliseconds
	float    seek_trk;   // Track to track seek time in milliseconds
	unsigned rot_speed;  // Rotational speed in RPM
	unsigned interleave; // Interleave ratio
	float    overh_time; // Controller overhead time in milliseconds

	// these are derived values in microseconds calculated from the above:
	uint32_t seek_avgspeed_us; // Seek average speed
	uint32_t seek_overhead_us; // Seek overhead time
	uint32_t trk2trk_us;  // Seek track to track time
	uint32_t trk_read_us; // Full track read time
	uint32_t sec_read_us; // Sector read time
	uint32_t sec_xfer_us; // Sector transfer time, taking interleave into account
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
 * Disk storage device class.
 * Can be used to model hard drives, floppy drives and cd-rom drives.
 *
 * Timings are for CAV devices with a constant number of sectors per track.
 * TODO To model a CD-ROM this class must be expanded to consider the CLV mode
 * of operation. MediaGeometry and DrivePerformance also need to be refactored
 * to implement a single linear track.
 */
class StorageDev
{
protected:
	std::string m_name; // Device name (used for logging)

	int64_t m_sectors;       // Total number of sectors
	int m_sector_data;       // Data bytes per sector
	double m_sector_size;    // Total sector size (data + control fields)
	double m_sector_len;     // Physical length of a sector relative to a track
	double m_disk_radius;    // Disk radius in mm
	double m_track_overhead; // Additional control bytes per track

	// Head factors are used to extrapolate the performance characteristics of a
	// storage device starting from known data measured from real world.
	// If we know how a disk with a certain geometry performs, we can guess
	// a similar device but with a different geometry.
	double m_head_speed_factor; // Used to derive the head max speed
	double m_head_accel_factor; // Used to derive the head acceleration

	DriveIdent m_ident;
	MediaGeometry m_geometry;
	DrivePerformance m_performance;

	struct {
		double   head_pos;
		uint64_t head_time;
		uint64_t power_on_time;
	} m_s;

public:
	StorageDev();
	virtual ~StorageDev() {}

	virtual void install() {}
	virtual void remove() {}
	virtual void power_on(uint64_t _time);
	virtual void power_off();
	virtual void config_changed(const char *section);
	virtual void save_state(StateBuf &) {}
	virtual void restore_state(StateBuf &) {}

	const char * name() { return m_name.c_str(); }
	const char * vendor() { return m_ident.vendor; }
	const char * product() { return m_ident.product; }
	const char * revision() { return m_ident.revision; }
	const char * model() { return m_ident.model; }
	const char * serial() { return m_ident.serial; }
	const char * firmware() { return m_ident.firmware; }

	bool insert_media(const char */*_path*/) { return false; }
	void eject_media() {}
	bool is_media_present() { return false; }

	const MediaGeometry & geometry() const { return m_geometry; }
	const DrivePerformance & performance() const { return m_performance; }

	virtual int64_t sectors() const { return m_sectors; }
	virtual int64_t capacity() const { return m_sectors*m_sector_data; }

	virtual void set_name(const char *_name) { m_name = _name; }
	virtual uint32_t seek_move_time_us(unsigned _cur_cyl, unsigned _dest_cyl);
	virtual uint32_t rotational_latency_us(double _start_hw_sector, unsigned _dest_log_sector);
	virtual uint32_t transfer_time_us(uint64_t _curr_time, int64_t _xfer_lba_sector, int64_t _xfer_amount);
	virtual double head_position(double _last_pos, uint32_t _elapsed_time_us) const;
	virtual double head_position(uint64_t _time_us) const;
	virtual double head_position() const;

	virtual uint64_t power_up_eta_us() const { return 0; }
	virtual bool is_powering_up() const { return (power_up_eta_us() > 0); }
	virtual void set_space_time(double _head_pos, uint64_t _time);

	virtual void read_sector(int64_t /*_lba*/, uint8_t * /*_buffer*/, unsigned /*_len*/) {}
	virtual void write_sector(int64_t /*_lba*/, uint8_t * /*_buffer*/, unsigned /*_len*/) {}
	virtual void seek(unsigned /*_from_cyl*/, unsigned /*_to_cyl*/) {}
	virtual void seek(int64_t /*_lba*/) {}

	int64_t chs_to_lba(int64_t _c, int64_t _h, int64_t _s) const;
	int chs_to_hw_sector(int _sector) const;
	double hw_sect_to_pos(double _hw_sector) const;
	int64_t lba_to_cylinder(int64_t _lba) const;
	void lba_to_chs(int64_t _lba, int64_t &c_, int64_t &h_, int64_t &s_) const;
	double pos_to_sect(double _head_pos) const;
};

#endif
