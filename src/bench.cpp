/*
 * Copyright (C) 2015, 2016  Marco Bortolin
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
#include <string>
#include <iostream>
#include <iomanip>
#include <iterator>
#include <cfloat>
#include <climits>
#include "chrono.h"
#include "bench.h"


Bench::Bench()
:
m_min_ftime(LLONG_MAX),
m_max_ftime(0),
m_frame_count(0),
m_upd_interval(1e6),
m_chrono(nullptr),

init_time(0),

ustart(0),
uend(0),
update_interval(1e6),

heartbeat(DEFAULT_HEARTBEAT),

fstart(0),
fend(0),
frame_time(0),
frame_rate(0),
frame_count(0),

min_frame_time(0),
max_frame_time(0),
min_fps(0.f),
max_fps(0),
avg_fps(0),
long_frames(0),

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
	init_time = m_chrono->get_nsec();
	c_init_time = std::chrono::steady_clock::now();
	ustart = init_time;
	update_interval = m_upd_interval = _update_interval * 1.0e6;
	frame_times.reserve(_rec_buffer_size);
}

void Bench::frame_start()
{
	if(frame_reset) {
		m_frame_count = 0;
		m_min_ftime = LLONG_MAX;
		m_max_ftime = 0;
		frame_reset = false;
	}

	fstart = m_chrono->get_nsec();
}

void Bench::frame_end()
{
	fend = m_chrono->get_nsec();

	frame_time = fend - fstart;
	m_min_ftime = std::min(frame_time,m_min_ftime);
	m_max_ftime = std::max(frame_time,m_max_ftime);
	m_frame_count++;
	if(frame_time > heartbeat) {
		long_frames++;
	}
	
	int64_t updtime = fend - ustart;
	if(updtime >= m_upd_interval) {
		data_update();
		ustart = fend;
		frame_reset = true;
	}
}

void Bench::data_update()
{
	time_elapsed = fend - init_time;
	auto now = std::chrono::steady_clock::now();
	c_time_elapsed = std::chrono::duration_cast<std::chrono::duration<double>>(now - c_init_time);

	double uea = double(fend - ustart);

	frame_count = m_frame_count;

	max_frame_time = m_max_ftime;
	min_frame_time = m_min_ftime;

	avg_fps = double(frame_count) * (1.0e9 / uea);
	min_fps = 1.0e9 / max_frame_time;
	max_fps = 1.0e9 / min_frame_time;
}


void operator<<(std::ostream& _os, const Bench &_bench)
{
	_os << std::fixed << std::setprecision( 6 );
	
	_os << "Time (s): " << (_bench.time_elapsed / 1e9) << _bench.endl;
	if(CHRONO_RDTSC) {
		_os << "host time: " << _bench.c_time_elapsed.count() * 1000.0 << _bench.endl;
	}
	_os << "Frame time (ms): " << (_bench.frame_time / 1e6) << _bench.endl;
	_os << "Target FPS: " << (1.0e9 / _bench.heartbeat) << _bench.endl;
	_os << "Curr. FPS: " << _bench.avg_fps << _bench.endl; 
	//_os << "Frame count: " << _bench.frame_count << _bench.endl;
	_os << "Missed frames: " << _bench.long_frames << _bench.endl;
}


