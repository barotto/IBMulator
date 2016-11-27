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

#ifndef IBMULATOR_HW_HDD_H
#define IBMULATOR_HW_HDD_H

#include "mediaimage.h"
#include "harddrvfx.h"
#include <memory>

#define HDD_DRIVES_TABLE_SIZE 45
#define HDD_CUSTOM_DRIVE_IDX 45

struct HDDPerformance
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

class HardDiskDrive
{
private:
	struct {
		double   head_pos;
		uint64_t head_time;

		uint64_t power_on_time;
	} m_s;

	int m_type;
	uint32_t m_sectors;
	double m_sect_size;
	uint64_t m_spin_up_duration;

	std::unique_ptr<MediaImage> m_disk;
	HDDPerformance m_performance;
	std::string m_original_path;
	MediaGeometry m_original_geom;
	bool m_write_protect;
	bool m_save_on_close;
	bool m_tmp_disk;

	static const MediaGeometry ms_hdd_types[HDD_DRIVES_TABLE_SIZE];
	static const std::map<uint, HDDPerformance> ms_hdd_performance;

	HardDriveFX m_fx;

public:
	HardDiskDrive();
	~HardDiskDrive();

	void install();
	void remove();
	void power_on(uint64_t _time);
	void power_off();
	void config_changed();

	const MediaGeometry &  geometry() const { return m_disk->geometry; }
	const HDDPerformance & performance() const { return m_performance; }

	unsigned type() const { return m_type; }
	uint32_t seek_move_time_us(unsigned _cur_cyl, unsigned _dest_cyl);
	uint32_t rotational_latency_us(double _start_hw_sector, unsigned _dest_log_sector);
	int      hw_sector_number(int _logical_sector) const;
	double   head_position(double _last_pos, uint32_t _elapsed_time_us) const;
	double   head_position(uint64_t _time_us) const;
	double   head_position() const;

	uint64_t spin_up_eta_us() const;
	bool is_spinning_up() const { return (spin_up_eta_us() > 0); }
	void set_space_time(double _head_pos, uint64_t _time);

	void read_sector(unsigned _c, unsigned _h, unsigned _s, uint8_t *_buffer);
	void write_sector(unsigned _c, unsigned _h, unsigned _s, uint8_t *_buffer);

	void seek(unsigned _from_cyl, unsigned _to_cyl);

	void save_state(StateBuf &_state);
	void restore_state(StateBuf &_state);

	unsigned chs_to_lba(unsigned _c, unsigned _h, unsigned _s) const;
	void lba_to_chs(unsigned _lba, unsigned &_c, unsigned &_h, unsigned &_s) const;
	double pos_to_sect(double _head_pos) const;
	double sect_to_pos(double _hw_sector) const;

private:
	void get_profile(int _type_id, MediaGeometry &geom_, HDDPerformance &perf_);
	void mount(std::string _imgpath, MediaGeometry _geom, HDDPerformance _perf);
	void unmount();
};

#endif

