/*
 * 	Copyright (c) 2015  Marco Bortolin
 *
 *	This file is part of IBMulator
 *
 *  IBMulator is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 3 of the License, or
 *	(at your option) any later version.
 *
 *	IBMulator is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with IBMulator.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "ibmulator.h"
#include <string>
#include <iostream>
#include <iterator>
#include <cfloat>
#include <climits>
#include "chrono.h"
#include "bench.h"


Bench::Bench()
:
m_min_btime(INT_MAX),
m_max_btime(0),
m_min_ftime(INT_MAX),
m_max_ftime(0),
m_beat_count(0),
m_frame_count(0),
m_upd_interval(1000),

ustart(0),
uend(0),
update_interval(1000),

bstart(0),
bend(0),
beat_count(0),
min_beat_time(0),
max_beat_time(0),
min_bps(0.f),
max_bps(0),
avg_bps(.0f),

fstart(0),
fend(0),
frame_rate(0),
frame_count(0),

min_frame_time(0),
max_frame_time(0),
min_fps(0.f),
max_fps(0),
avg_fps(0),

frameskip(0),

time_elapsed(0),
frame_reset(true),
endl("\n")
{
}


Bench::~Bench()
{
}


void Bench::init(const Chrono *_chrono, int _update_interval, int _rec_buffer_size)
{
	m_chrono = _chrono;
	init_time = m_chrono->get_msec();
	c_init_time = std::chrono::steady_clock::now();
	ustart = init_time;
	update_interval = m_upd_interval = _update_interval;
	frame_times.reserve(_rec_buffer_size);
}


void Bench::frame_start()
{
	if(frame_reset) {
		m_beat_count = 0;
		m_frame_count = 0;
		m_min_btime = INT_MAX;
		m_max_btime = 0;
		m_min_ftime = INT_MAX;
		m_max_ftime = 0;
		//frame_times.clear();
		frame_reset = false;
	}

	fstart = m_chrono->get_msec();
}


void Bench::frame_end()
{
	fend = m_chrono->get_msec();

	uint ftime = fend - fstart;
	m_min_ftime = std::min(ftime,m_min_ftime);
	m_max_ftime = std::max(ftime,m_max_ftime);
	//frame_times.push_back(ftime);
	m_frame_count++;

	uint updtime = fend - ustart;
	if(updtime >= m_upd_interval) {
		data_update();
		ustart = fend;
		frame_reset = true;
		if(updtime>update_interval) {
			m_upd_interval = update_interval - (updtime-update_interval);
		}
		else if(updtime<update_interval) {
			m_upd_interval = update_interval + (update_interval-updtime);
		}
	}
}


void Bench::beat_start()
{
	bstart = m_chrono->get_msec();
}


void Bench::beat_end()
{
	bend = m_chrono->get_msec();

	uint btime = bend - bstart;
	m_min_btime = std::min(btime,m_min_btime);
	m_max_btime = std::max(btime,m_max_btime);
	m_beat_count++;
}


void Bench::data_update()
{
	time_elapsed = fend - init_time;
	auto now = std::chrono::steady_clock::now();
	c_time_elapsed = std::chrono::duration_cast<std::chrono::duration<double>>(now - c_init_time);

	float uea = float(fend - ustart);
	//float uea = update_interval;

	beat_count = m_beat_count;
	frame_count = m_frame_count;

	max_beat_time = m_max_btime;
	min_beat_time = m_min_btime;

	max_frame_time = m_max_ftime;
	min_frame_time = m_min_ftime;

	avg_bps = float(beat_count * 1000) / uea;
	min_bps = 1000.f / max_beat_time;
	max_bps = 1000.f / min_beat_time;

	avg_fps = float(frame_count * 1000) / uea;
	min_fps = 1000.f / max_frame_time;
	max_fps = 1000.f / min_frame_time;
}


void operator<<(std::ostream& _os, const Bench &_bench)
{
	_os << "time: " << _bench.time_elapsed << _bench.endl;
	if(CHRONO_RDTSC) {
		_os << "host time: " << _bench.c_time_elapsed.count() * 1000.0 << _bench.endl;
	}
	_os << "avg bps: " << _bench.avg_bps << _bench.endl;
	_os << "avg fps: " << _bench.avg_fps << _bench.endl;

	_os << "beats: " << _bench.beat_count << _bench.endl;
	_os << "frames: " << _bench.frame_count << _bench.endl;
	//_os << "tot frames: " << _bench.frame_count << _bench.endl;
	//_os << "min ftime: " << _bench.min_frame_time << " (" << _bench.max_fps << " fps)"<< _bench.endl;
	_os << "max btime: " << _bench.max_beat_time << " (" << _bench.min_bps << " bps)"<< _bench.endl;
	_os << "max ftime: " << _bench.max_frame_time << " (" << _bench.min_fps << " fps)"<< _bench.endl;

	_os << "fskip: " << _bench.frameskip;
}


