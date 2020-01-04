/*
 * Copyright (C) 2015-2020  Marco Bortolin
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
#include <cmath>
#include "chrono.h"
#include "hwbench.h"


HWBench::HWBench()
:
m_chrono(nullptr),

m_init_time(0),
m_upd_interval(1e9),

m_sim_time(0),
m_min_sim_time(LLONG_MAX),
m_max_sim_time(0),
m_sum_sim_time(0),

m_frame_time(0),
m_min_frame_time(LLONG_MAX),
m_max_frame_time(0),
m_sum_frame_time(0),
m_sum_frame_time2(0),

m_upd_frame_count(0),
m_icount(0),
m_ccount(0),

m_frame_start(0),
m_sim_start(0),
m_frame_end(0),

m_upd_start(0),
m_upd_end(0),

m_upd_reset(true),

heartbeat(0),

time_elapsed(0),
tot_frame_count(0),
late_frames(0),

min_sim_time(LLONG_MAX),
max_sim_time(0),
avg_sim_time(.0),

min_frame_time(LLONG_MAX),
max_frame_time(0),
avg_frame_time(.0),
std_frame_time(.0),

min_fps(0),
max_fps(0),
avg_fps(.0),

avg_ips(.0),
avg_cps(.0),

load(.0),
endl("\n")
{
}


HWBench::~HWBench()
{
}


void HWBench::init(const Chrono *_chrono, uint _update_interval)
{
	m_chrono = _chrono;
	m_init_time = m_chrono->get_nsec();
	m_upd_start = m_init_time;
	m_upd_interval = (_update_interval * 1.0e6);
}

void HWBench::reset()
{
	m_init_time = m_chrono->get_nsec();
	m_upd_start = m_init_time;
	tot_frame_count = 0;
	m_upd_reset = true;
}

void HWBench::frame_start()
{
	if(m_upd_reset) {
		m_upd_frame_count = 0;
		m_min_sim_time = LLONG_MAX;
		m_max_sim_time = 0;
		m_sum_sim_time = 0;
		m_min_frame_time = LLONG_MAX;
		m_max_frame_time = 0;
		m_sum_frame_time = 0;
		m_sum_frame_time2 = 0;
		m_icount = 0;
		m_ccount = 0;
		m_upd_reset = false;
	}

	m_frame_start = m_chrono->get_nsec();
}

void HWBench::sim_start()
{
	m_sim_start = m_chrono->get_nsec();
}

//GCC_ATTRIBUTE(optimize("O0")) <- it seems for gcc 4.8+ is not needed anymore
void HWBench::frame_end()
{
	m_frame_end = m_chrono->get_nsec();

	m_sim_time = m_frame_end - m_sim_start;
	m_min_sim_time = std::min(m_sim_time, m_min_sim_time);
	m_max_sim_time = std::max(m_sim_time, m_max_sim_time);
	m_sum_sim_time += m_sim_time;

	m_frame_time = m_frame_end - m_frame_start;
	m_min_frame_time = std::min(m_frame_time, m_min_frame_time);
	m_max_frame_time = std::max(m_frame_time, m_max_frame_time);
	m_sum_frame_time += m_frame_time;
	m_sum_frame_time2 += m_frame_time * m_frame_time;
	
	m_upd_frame_count++;
	tot_frame_count++;
	
	if(m_sim_time > heartbeat) {
		late_frames++;
	}

	unsigned updtime = m_frame_end - m_upd_start;
	if(updtime >= m_upd_interval) {
		data_update();
		m_upd_start = m_frame_end;
		m_upd_reset = true;
	}
}

void HWBench::data_update()
{
	time_elapsed  = m_frame_end - m_init_time;
	
	min_sim_time = m_min_sim_time;
	max_sim_time = m_max_sim_time;
	avg_sim_time = double(m_sum_sim_time) / m_upd_frame_count;

	min_frame_time = m_min_frame_time;
	max_frame_time = m_max_frame_time;
	avg_frame_time = double(m_sum_frame_time) / m_upd_frame_count;
	
	double msum2 = double(m_sum_frame_time2) / m_upd_frame_count;
	std_frame_time = sqrt(msum2 - double(avg_frame_time*avg_frame_time));
	
	double updtime = double(m_frame_end - m_upd_start);
	
	min_fps = 1.0e9 / max_sim_time;
	max_fps = 1.0e9 / min_sim_time;
	avg_fps = double(m_upd_frame_count) * 1.0e9 / updtime;

	avg_ips = double(m_icount) * 1.0e9 / updtime;
	avg_cps = double(m_ccount) * 1.0e9 / updtime;
	
	load = avg_sim_time / heartbeat;
}

std::ostream& operator<<(std::ostream& _os, const HWBench &_bench)
{
	_os.precision(6);
	_os << "Time (s): " << (_bench.time_elapsed / 1e9) << _bench.endl;
	_os << "Target FPS: " << (1.0e9 / _bench.heartbeat) << _bench.endl;
	_os << "Target Frame time (ms): " << (_bench.heartbeat / 1e6) << _bench.endl;
	_os << "-- curr. time: " << (_bench.m_frame_time / 1e6) << _bench.endl;
	_os.precision(3);
	_os << "-- min/avg/max: " << 
			(_bench.min_frame_time / 1e6) << "/" <<
			(_bench.avg_frame_time / 1e6) << "/" <<
			(_bench.max_frame_time / 1e6) << _bench.endl;
	_os.precision(6);
	_os << "-- std. dev: " << (_bench.std_frame_time / 1e6) << _bench.endl;
	_os << "-- sim. time: " << (_bench.m_sim_time / 1e6) << _bench.endl;
	_os.precision(3);
	_os << "-- min/avg/max: " << 
			(_bench.min_sim_time / 1e6) << "/" <<
			(_bench.avg_sim_time / 1e6) << "/" <<
			(_bench.max_sim_time / 1e6) << _bench.endl;
	_os.precision(6);
	_os << "Host load: " << _bench.load << _bench.endl;
	_os << "Late frames: " << _bench.late_frames << _bench.endl;

	double mhz = _bench.avg_cps / 1e6;
	double mips = _bench.avg_ips / 1e6;
	_os.precision(8);
	_os << "CPU MHz: " << mhz << _bench.endl;
	_os << "CPU MIPS: " << mips << _bench.endl;
	
	return _os;
}


