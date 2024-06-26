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

#include "ibmulator.h"
#include "storagedev.h"
#include "program.h"
#include <cstring>
#include <cmath>

/*
DriveIdent::DriveIdent()
{
	memset(this, 0x20, sizeof(DriveIdent));
	vendor[8] = 0;
	product[16] = 0;
	revision[4] = 0;
	model[40] = 0;
	serial[20] = 0;
	firmware[8] = 0;
}
*/

void DriveIdent::set_string(char *_dest, const char *_src, size_t _len)
{
	strncpy(_dest, _src, _len);
	while(strlen(_dest) < _len) {
		strcat(_dest, " ");
	}
	_dest[_len] = 0;
}

DriveIdent & DriveIdent::operator=(const DriveIdent &_src)
{
	set_vendor(_src.vendor);
	set_product(_src.product);
	set_revision(_src.revision);
	set_model(_src.model);
	set_serial(_src.serial);
	set_firmware(_src.firmware);

	return *this;
}

void StorageDev::install(StorageCtrl *_ctrl, uint8_t _id, const char * _ini_section)
{
	m_ini_section = _ini_section;
	m_controller = _ctrl;
	m_drive_index = _id;
	m_fx_enabled = g_program.config().get_bool(SOUNDFX_SECTION, SOUNDFX_ENABLED);
}

int64_t StorageDev::chs_to_lba(int64_t _c, int64_t _h, int64_t _s) const
{
	assert(_s > 0);
	return (_c * m_geometry.heads + _h ) * m_geometry.spt + (_s-1);
}

void StorageDev::lba_to_chs(int64_t _lba, int64_t &c_, int64_t &h_, int64_t &s_) const
{
	c_ = _lba / (m_geometry.heads * m_geometry.spt);
	h_ = (_lba / m_geometry.spt) % m_geometry.heads;
	s_ = (_lba % m_geometry.spt) + 1;
}

int64_t StorageDev::lba_to_cylinder(int64_t _lba) const
{
	return (_lba / (m_geometry.heads * m_geometry.spt));
}

int64_t StorageDev::lba_to_head(int64_t _lba) const
{
	return (_lba / m_geometry.spt) % m_geometry.heads;
}

double StorageDev::pos_to_hw_sect(double _head_pos) const
{
	double sector = double(m_geometry.spt) + m_track_overhead/m_sector_size;
	return (_head_pos * sector);
}

/**
 * Returns the track position corresponding to a hardware sector.
 * @param _hw_sector  the hardware sector index, 0-based.
 * @return the position of the starting point of _hw_sector in the range 0.0-1.0
 */
double StorageDev::hw_sect_to_pos(double _hw_sector) const
{
	return (_hw_sector * m_sector_len);
}

/**
 * Returns the hardware sector number corresponding to a given CHS sector.
 * Hardware sectors are 0-based and take into account the interleave value.
 */
int StorageDev::chs_to_hw_sector(int _sector) const
{
	return unsigned((_sector-1)*m_performance.interleave) % m_geometry.spt;
}

double StorageDev::head_position(double _last_pos, uint32_t _elapsed_time_us) const
{
	double cur_pos = _last_pos + (double(_elapsed_time_us) / m_performance.trk_read_us);
	cur_pos = cur_pos - floor(cur_pos);
	return cur_pos;
}

double StorageDev::head_position(uint64_t _time_us) const
{
	return head_position(0.0, _time_us);
}

void StorageDev::set_geometry(const MediaGeometry &_geometry, double _raw_sector_bytes, double _track_overhead_bytes)
{
	m_geometry = _geometry;

	m_sectors = uint64_t(_geometry.spt) * _geometry.cylinders * _geometry.heads;

	m_sector_size = _raw_sector_bytes;
	m_track_overhead = _track_overhead_bytes;

	double track_bytes = (_geometry.spt * _raw_sector_bytes + _track_overhead_bytes);
	m_sector_len = (1.0 / track_bytes) * _raw_sector_bytes;
}

void DrivePerformance::update(const MediaGeometry &_geometry, double _raw_sector_bytes, double _track_overhead_bytes)
{
	/* See comment for seek_move_time_us().
	 * Here we divide the total seek time in 2 values: avgspeed and overhead,
	 * (where avgspeed is the time to traverse 1 cylinder and overhead are all
	 * the latencies) derived from the only 2 values given in HDD specifications:
	 * track-to-track and maximum (full stroke).
	 *
	 * trk2trk = overhead + avgspeed
	 * maximum = overhead + avgspeed*(ncyls-1)
	 *
	 * overhead = trk2trk - avgspeed
	 * avgspeed = (maximum - trk2trk) / (ncyls-2)
	 *
	 * So the average speed includes phases 1, 2, and 3.
	 * 
	 * CD-ROM drives have 1/3 stroke and full stroke info:
	 * (1) third = overhead + avgspeed*(ncyls/3)
	 * (2) maximum = overhead + avgspeed*ncyls   [the -1 doesn't matter]
	 * 
	 * (1) overhead = third - avgspeed*(ncyls/3)
	 * (2) maximum = third - 1/3*avgspeed*ncyls + avgspeed*ncyls
	 * (2)  maximum - third = avgspeed*ncyls * (1 - 1/3)
	 * (2)  avgspeed = (maximum - third) / (2/3 * ncyls)
	 */
	trk_read_us = round(6.0e7 / rot_speed);
	avg_rot_lat_us = trk_read_us / 2.0;

	if(seek_trk_ms > 0.f) {
		// HDD performance data
		trk2trk_us = seek_trk_ms * 1000.0;
		seek_avgspeed_us = round(((seek_max_ms - seek_trk_ms) / double(_geometry.cylinders-2)) * 1000.0);
		seek_overhead_us = trk2trk_us - seek_avgspeed_us;
	} else {
		// CD-ROM drives do not have track to track penalty when reading sequentially (it's a spiral)
		trk2trk_us = 0; 
		seek_avgspeed_us = round(((seek_max_ms - seek_third_ms) / ((2.0/3.0) * _geometry.cylinders)) * 1000.0);
		seek_overhead_us = (seek_third_ms * 1000.0) - seek_avgspeed_us * (double(_geometry.cylinders) / 3.0);
	}

	double track_bytes = (_geometry.spt * _raw_sector_bytes + _track_overhead_bytes);
	bytes_per_us = track_bytes / trk_read_us;
	sec_read_us = round(_raw_sector_bytes / bytes_per_us);

	sec_xfer_us = sec_read_us * std::max(1.0, (interleave * 0.8));
	sec2sec_us = sec_read_us * interleave;
}

/**
 * Gives the head move time of a seek.
 * Seeks are comprised of the following phases:
 * 1. acceleration (the disk arm gets moving);
 * 2. coasting (the arm is moving at full speed);
 * 3. deceleration (the arm slows down);
 * 4. settling (the head is positioned over the correct track).
 * This function returns the combined value of phases 1, 2, and 3.
 *
 * @param _cur_cyl  starting cylinder number.
 * @param _dest_cyl destination cylinder number.
 * @return seek move time in microseconds.
 */
uint32_t StorageDev::seek_move_time_us(unsigned _cur_cyl, unsigned _dest_cyl)
{
	/* We assume a linear head movement, but in the real world the head
	 * describes an arc onto the platter surface.
	 */
	const double cylinder_width = m_disk_radius / m_geometry.cylinders;
	//speed in mm/ms
	const double avg_speed = m_disk_radius /
			(((m_geometry.cylinders-1)*m_performance.seek_avgspeed_us)/1000.0);

	const double max_speed = avg_speed * m_head_speed_factor; // mm/ms
	const double accel     = avg_speed * m_head_accel_factor; // mm/ms^2

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

	PDEBUGF(LOG_V2, LOG_HDD, "%s: SEEK MOVE dist:%.2f,acc_space:%.2f,acc_time:%.0f,co_space:%.2f,co_time:%.0f,tot.move:%u\n",
			name(), distance, acc_space, acc_time, coasting_space, coasting_time, move_time);

	return move_time;
}

/**
 * Returns the rotational latency in microseconds needed to position the head
 * upon the given CHS track sector. The head is considered already at the right track.
 *
 * @param _head_position  the head position in the range 0.0-1.0 at time0.
 * @param _dest_sector    the destination CHS sector number, 1-based.
 * @return rotational latency in microseconds.
 */
uint32_t StorageDev::rotational_latency_us(double _head_position, unsigned _dest_sector)
{
	double distance;
	assert(_head_position>=0.f && _head_position<=1.f);

	double dest_hw_sector = chs_to_hw_sector(_dest_sector);
	double dest_position = m_sector_len * dest_hw_sector;
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

/**
 * Returns the tranfer time in microseconds required to read or write the given
 * amount of sectors.
 * No initial seek is calculated so the head is considered already at the right
 * cylinder. Additional seeks required to complete the transfer are taken into
 * account.
 *
 * @param _curr_time         the current time (us).
 * @param _first_lba_sector  the first sector to transfer.
 * @param _xfer_amount       the number of sectors to transfer.
 * @return  the tranfer time in microseconds.
 */
uint32_t StorageDev::transfer_time_us(uint64_t _curr_time, int64_t _xfer_lba_sector,
		int64_t _xfer_amount)
{
	if(_xfer_amount <= 0) {
		return 0;
	}

	/* 1. wait for the head to position itself upon the right sector (rotational latency)
	 * 2. transfer the needed sectors or until the end of track (transfer time)
	 * 3. if transfer not complete and next sector is in next cylinder then seek (trk2trk seek time)
	 * 4. repeat from 1. until transfer completes
	 */
	uint32_t xfer_time = 0;
	int64_t lba = _xfer_lba_sector;
	double headpos = head_position(_curr_time);
	int64_t c0,c1,h,s0,s1;
	lba_to_chs(lba, c0, h, s0);
	do {
		// *** WARNING ***
		// CHS sectors are 1-based
		int64_t transfer_cnt = m_geometry.spt - (s0-1);
		transfer_cnt = std::min(transfer_cnt, _xfer_amount);
		uint32_t time_amount = 0;
		time_amount += rotational_latency_us(headpos, s0);
		time_amount += m_performance.sec2sec_us * (transfer_cnt-1);
		time_amount += m_performance.sec_read_us;
		//time_amount += m_performance.sec_xfer_us;
		_xfer_amount -= transfer_cnt;
		xfer_time += time_amount;
		lba += transfer_cnt;
		//headpos = head_position(headpos, time_amount);
		headpos = hw_sect_to_pos(chs_to_hw_sector(s0+transfer_cnt));
		if(_xfer_amount > 0) {
			lba_to_chs(lba, c1, h, s1);
			if(c1 != c0) {
				xfer_time += m_performance.trk2trk_us;
				headpos = head_position(headpos, m_performance.trk2trk_us);
			}
			//TODO we should take into account the head switching time
			c0 = c1;
			s0 = s1;
		}
	} while(_xfer_amount);

	return xfer_time;
}

/**
 * Returns the tranfer time in microseconds required to read or write the given
 * amount of sectors.
 * No initial seek is calculated so the head is considered already at the right
 * cylinder. Additional seeks required to complete the transfer are taken into
 * account. This version uses a look ahead cache that can hold a full track in
 * memory.
 * The cache is empty when _curr_time is equal to _look_ahead_time.
 * _look_ahead_time will be updated with the the current initial cache
 * operation.
 *
 * @param _curr_time         the current time (us).
 * @param _first_lba_sector  the first sector to transfer.
 * @param _xfer_amount       the number of sectors to transfer.
 * @param _look_ahead_time   the time of the initial cache operation on the
 *                           current track (us)
 * @param _rot_latency       add the rotational latency
 * @return  the tranfer time in microseconds.
 */
uint32_t StorageDev::transfer_time_us(uint64_t _curr_time, int64_t _xfer_lba_sector,
		int64_t _xfer_amount, uint64_t &_look_ahead_time, bool _rot_latency)
{
	uint32_t tot_xfer_time = 0;
	bool cache_is_empty = (_look_ahead_time >= _curr_time);

	if(cache_is_empty) {
		// what will the time of the first sector in the cache be?
		double next_sec_dist = pos_to_hw_sect(head_position(_look_ahead_time));
		next_sec_dist = next_sec_dist - int(next_sec_dist);
		_look_ahead_time += m_performance.sec_read_us*next_sec_dist;
	}

	while(_xfer_amount) {
		// what time is it? _curr_time
		// when did the caching started? _look_ahead_time
		// which CHS sector do we need? s0
		int64_t c0,h0,s0;
		lba_to_chs(_xfer_lba_sector, c0, h0, s0);

		// what's the corresponding HW sector?
		int hw_sector = chs_to_hw_sector(s0);

		bool is_in_cache = false;
		cache_is_empty = (_look_ahead_time >= _curr_time);

		double hw_cache1, hw_cache2;
		double curr_head = head_position(_curr_time);
		if(!cache_is_empty) {
			// what's the first HW sector in cache?
			double cache_head = head_position(_look_ahead_time);
			hw_cache1 = ceil(pos_to_hw_sect(cache_head));

			// what's the last HW sector in cache?
			hw_cache2 = pos_to_hw_sect(curr_head);

			// is the cache full?
			bool cache_is_full = (_curr_time - _look_ahead_time) > m_performance.trk_read_us;

			// is s0 in cache?
			if(cache_is_full) {
				is_in_cache = true;
			} else {
				if(hw_cache2 > hw_cache1) {
					if(hw_sector>=int(hw_cache1) && hw_sector<int(hw_cache2)) {
						is_in_cache = true;
					}
				} else {
					if(hw_sector>=int(hw_cache1) || hw_sector<int(hw_cache2)) {
						is_in_cache = true;
					}
				}
			}
		}

		int sec_xfer_time = 0;
		if(!is_in_cache) {
			// is s0 partially in cache?
			bool partially_in_cache = !cache_is_empty && (int(floor(hw_cache2)) == hw_sector);
			if(partially_in_cache) {
				// rotational latency is 0
				double frac_hwc2 = hw_cache2 - int(hw_cache2);
				sec_xfer_time = m_performance.sec_read_us - m_performance.sec_read_us*frac_hwc2;
			} else {
				sec_xfer_time = m_performance.sec_read_us;
				if(_rot_latency) {
					sec_xfer_time += rotational_latency_us(curr_head, s0);
				}
			}
		}

		_xfer_lba_sector++;
		_xfer_amount--;
		_curr_time += sec_xfer_time;
		tot_xfer_time += sec_xfer_time;

		// is the next sector on the next track?
		if(_xfer_amount && (s0+1 > m_geometry.spt)) {
			int64_t c1,h1,s1;
			lba_to_chs(_xfer_lba_sector, c1,h1,s1);
			// is the next track on the next cylinder?
			if(c1 != c0) {
				// seek next cylinder and reset the cache
				_curr_time += m_performance.trk2trk_us;
				_look_ahead_time = _curr_time;
			} else if(h1 != h0) {
				// different track, reset cache
				_look_ahead_time = _curr_time;
			}
		}
	}

	return tot_xfer_time;
}
