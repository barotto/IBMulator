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
#include <cfloat>
#include <climits>
#include "chrono.h"
#include "hwbench.h"


HWBench::HWBench()
:
m_min_btime(UINT_MAX),
m_max_btime(0),
m_sum_btime(0),
m_beat_time(0),
m_beat_count(0),
m_upd_interval(1e9),
m_icount(0),
m_ccount(0),
m_reset(true),
m_chrono(nullptr),

init_time(0),
ustart(0),
uend(0),

heartbeat(0),

bstart(0),
bend(0),
beat_count(0),
long_frames(0),
min_beat_time(LLONG_MAX),
max_beat_time(0),
avg_beat_time(.0),
min_bps(0),
max_bps(0),
avg_bps(.0),
avg_ips(.0),
avg_cps(.0),
load(.0),

time_elapsed(0),
endl("\n")
{
}


HWBench::~HWBench()
{
}


void HWBench::init(const Chrono *_chrono, uint _update_interval)
{
	m_chrono = _chrono;
	init_time = m_chrono->get_nsec();
	ustart = init_time;
	m_upd_interval = (_update_interval * 1.0e6);
}

void HWBench::reset()
{
	init_time = m_chrono->get_nsec();
	ustart = init_time;
	m_reset = true;
}

void HWBench::beat_start()
{
	if(m_reset) {
		m_beat_count = 0;
		m_min_btime = LLONG_MAX;
		m_max_btime = 0;
		m_sum_btime = 0;
		m_icount = 0;
		m_ccount = 0;
		m_reset = false;
	}

	bstart = m_chrono->get_nsec();
}

//GCC_ATTRIBUTE(optimize("O0")) <- it seems for gcc 4.8+ is not needed anymore
void HWBench::beat_end()
{
	bend = m_chrono->get_nsec();

	m_beat_time = bend - bstart;

	m_min_btime = std::min(m_beat_time, m_min_btime);
	m_max_btime = std::max(m_beat_time, m_max_btime);
	m_sum_btime += m_beat_time;
	m_beat_count++;
	if(m_beat_time > heartbeat) {
		long_frames++;
	}

	unsigned updtime = bend - ustart;
	if(updtime >= m_upd_interval) {
		data_update();
		ustart = bend;
		m_reset = true;
	}
}

void HWBench::data_update()
{
	time_elapsed  = bend - init_time;
	beat_count    = m_beat_count;
	max_beat_time = m_max_btime;
	min_beat_time = m_min_btime;
	avg_beat_time = double(m_sum_btime) / m_beat_count;

	double updtime = double(bend - ustart);
	
	avg_bps = double(beat_count) * 1.0e9 / updtime;
	min_bps = 1.0e9 / max_beat_time;
	max_bps = 1.0e9 / min_beat_time;

	avg_ips = double(m_icount) * 1.0e9 / updtime;
	avg_cps = double(m_ccount) * 1.0e9 / updtime;
	
	load = avg_beat_time / heartbeat;
}

std::ostream& operator<<(std::ostream& _os, const HWBench &_bench)
{
	_os.precision(6);
	_os << "Time (s): " << (_bench.time_elapsed / 1e9) << _bench.endl;
	_os << "Frame time (ms): " << (_bench.m_beat_time / 1e6) << _bench.endl;
	_os << "Target FPS: " << (1.0e9 / _bench.heartbeat) << _bench.endl;
	_os << "Curr. FPS: " << _bench.avg_bps << _bench.endl;
	_os << "Host load: " << _bench.load << _bench.endl;
	_os << "Missed frames: " << _bench.long_frames << _bench.endl; 

	double mhz = _bench.avg_cps / 1e6;
	double mips = _bench.avg_ips / 1e6;
	_os.precision(8);
	_os << "CPU MHz: " << mhz << _bench.endl;
	_os << "CPU MIPS: " << mips << _bench.endl;
	
	return _os;
}


