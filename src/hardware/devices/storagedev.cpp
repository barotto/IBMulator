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
#include "storagedev.h"
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


StorageDev::StorageDev()
:
m_sectors(0),
m_sector_data(0),
m_sector_size(0.0),
m_sector_len(0.0),
m_disk_radius(0.0),
m_track_overhead(0.0),
m_head_speed_factor(0.0),
m_head_accel_factor(0.0)
{
	memset(&m_s, 0, sizeof(m_s));
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

double StorageDev::pos_to_sect(double _head_pos) const
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
	return (((_sector-1)*m_performance.interleave) % m_geometry.spt);
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

void StorageDev::power_on(uint64_t _time)
{
	m_s.power_on_time = _time;
}

void StorageDev::power_off()
{
	memset(&m_s, 0, sizeof(m_s));
}

void StorageDev::config_changed(const char *)
{
	m_sectors = uint64_t(m_geometry.spt) * m_geometry.cylinders * m_geometry.heads;

	m_performance.trk_read_us = round(6.0e7 / m_performance.rot_speed);
	m_performance.trk2trk_us = m_performance.seek_trk * 1000.0;

	/* See comment for seek_move_time_us().
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
	 * So the average speed includes phases 1, 2, and 3.
	 */
	m_performance.seek_avgspeed_us = round(((m_performance.seek_max-m_performance.seek_trk) / double(m_geometry.cylinders-2)) * 1000.0);
	m_performance.seek_overhead_us = m_performance.trk2trk_us - m_performance.seek_avgspeed_us;

	double bytes_pt = (m_geometry.spt*m_sector_size + m_track_overhead);
	double bytes_us = bytes_pt / double(m_performance.trk_read_us);
	m_performance.sec_read_us = round(m_sector_size / bytes_us);
	m_sector_len = (1.0 / bytes_pt) * m_sector_size;
	m_performance.sec_xfer_us = double(m_performance.sec_read_us) * std::max(1.0,(double(m_performance.interleave) * 0.8f));
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

	PDEBUGF(LOG_V2, LOG_HDD, "%s: SEEK MOVE dist:%.2f,acc_space:%.2f,acc_time:%.0f,co_space:%.2f,co_time:%.0f,tot.move:%.0f\n",
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
 * amount of sectors. The head is considered already at the right track.
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
	 * 3. if transfer not complete and next sector is in next track then seek (trk2trk seek time)
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
		time_amount += m_performance.sec_read_us * m_performance.interleave * (transfer_cnt-1);
		time_amount += m_performance.sec_read_us;
		//time_amount += m_performance.sec_xfer_us;
		_xfer_amount -= transfer_cnt;
		xfer_time += time_amount;
		lba += transfer_cnt;
		headpos = head_position(headpos, time_amount);
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

