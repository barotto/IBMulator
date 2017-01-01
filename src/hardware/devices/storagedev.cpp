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


StorageDev::StorageDev()
{
	memset(&m_s, 0, sizeof(m_s));
	m_model[40] = 0;
	m_serial[20]= 0;
	m_firmware[8] = 0;
}

void StorageDev::set_space_time(double _head_pos, uint64_t _head_time)
{
	m_s.head_pos = _head_pos;
	m_s.head_time = _head_time;
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

double StorageDev::pos_to_sect(double _head_pos) const
{
	double sector = double(m_geometry.spt) + m_track_overhead/m_sector_size;
	return (_head_pos * sector);
}

double StorageDev::sect_to_pos(double _hw_sector) const
{
	return (_hw_sector * m_sector_len);
}

int StorageDev::hw_sector_number(int _logical_sector) const
{
	return (((_logical_sector-1)*m_performance.interleave) % m_geometry.spt);
}

double StorageDev::head_position(double _last_pos, uint32_t _elapsed_time_us) const
{
	double cur_pos = _last_pos + (double(_elapsed_time_us) / m_performance.trk_read_us);
	cur_pos = cur_pos - floor(cur_pos);
	return cur_pos;
}

double StorageDev::head_position(uint64_t _time_us) const
{
	assert(_time_us >= m_s.head_time);
	uint32_t elapsed_time_us = _time_us - m_s.head_time;
	return head_position(m_s.head_pos, elapsed_time_us);
}

double StorageDev::head_position() const
{
	return m_s.head_pos;
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
	m_performance.seek_avgspeed_us = round(((m_performance.seek_max-m_performance.seek_trk) / double(m_geometry.cylinders-2)) * 1000.0);
	m_performance.seek_overhead_us = m_performance.trk2trk_us - m_performance.seek_avgspeed_us;

	double bytes_pt = (m_geometry.spt*m_sector_size + m_track_overhead);
	double bytes_us = bytes_pt / double(m_performance.trk_read_us);
	m_performance.sec_read_us = round(m_sector_size / bytes_us);
	m_sector_len = (1.0 / bytes_pt) * m_sector_size;
	m_performance.sec_xfer_us = double(m_performance.sec_read_us) * std::max(1.0,(double(m_performance.interleave) * 0.8f));
}

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

uint32_t StorageDev::rotational_latency_us(
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
			% m_geometry.spt;
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

