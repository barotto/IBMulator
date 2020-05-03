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
#include "bench.h"


Bench::Bench()
:
m_chrono(nullptr),

m_init_time(0),
m_upd_interval(1e9),

m_min_load_time(LLONG_MAX),
m_max_load_time(0),
m_sum_load_time(0),

m_min_frame_time(LLONG_MAX),
m_max_frame_time(0),
m_sum_frame_time(0),
m_sum_frame_time2(0),

m_upd_frame_count(0),

m_frame_start(0),
m_load_start(0),
m_frame_end(0),

m_upd_start(0),
m_upd_end(0),
m_upd_count(0),

m_upd_reset(true),

heartbeat(0),

time_elapsed(0),
tot_frame_count(0),
late_frames(0),

load_time(0),
min_load_time(0),
max_load_time(0),
avg_load_time(.0),

frame_time(0),
min_frame_time(0),
max_frame_time(0),
avg_frame_time(.0),
std_frame_time(.0),
cavg_frame_time(.0),
cavg_std_frame_time(.0),

min_fps(0),
max_fps(0),
avg_fps(.0),

load(.0)
{
}

Bench::~Bench()
{
}

void Bench::init(const Chrono *_chrono, unsigned _upd_interval_ms)
{
	m_chrono = _chrono;
	m_upd_interval = (_upd_interval_ms * 1.0e6);
}

void Bench::start()
{
	m_init_time = m_chrono->get_nsec();
	m_upd_start = m_init_time;
	tot_frame_count = 0;
	
	reset_values();
}

void Bench::set_heartbeat(int64_t _nsec)
{
	heartbeat = _nsec;

	reset_values();
}

void Bench::reset_values()
{
	load = .0;
	load_time = 0;
	min_load_time = 0;
	max_load_time = 0;
	avg_load_time = .0;
	
	frame_time = 0;
	min_frame_time = 0;
	max_frame_time = 0;
	avg_frame_time = .0;
	std_frame_time = .0;

	cavg_frame_time = .0;
	cavg_std_frame_time = .0;
	
	m_upd_reset = true;
	m_upd_count = 0;
}

void Bench::frame_start()
{
	m_frame_start = m_chrono->get_nsec();
	
	if(m_upd_reset) {
		m_upd_frame_count = 0;
		m_min_load_time = LLONG_MAX;
		m_max_load_time = 0;
		m_sum_load_time = 0;
		m_min_frame_time = LLONG_MAX;
		m_max_frame_time = 0;
		m_sum_frame_time = 0;
		m_sum_frame_time2 = 0;
		m_upd_reset = false;
		m_upd_start = m_frame_start;
	}
}

void Bench::load_start()
{
	m_load_start = m_chrono->get_nsec();
}

//GCC_ATTRIBUTE(optimize("O0")) <- it seems for gcc 4.8+ is not needed anymore
void Bench::frame_end()
{
	m_frame_end = m_chrono->get_nsec();

	load_time = m_frame_end - m_load_start;
	m_min_load_time = std::min(load_time, m_min_load_time);
	m_max_load_time = std::max(load_time, m_max_load_time);
	m_sum_load_time += load_time;

	frame_time = m_frame_end - m_frame_start;
	m_min_frame_time = std::min(frame_time, m_min_frame_time);
	m_max_frame_time = std::max(frame_time, m_max_frame_time);
	m_sum_frame_time += frame_time;
	m_sum_frame_time2 += frame_time * frame_time;
	
	m_upd_frame_count++;
	tot_frame_count++;
	
	if(load_time > heartbeat) {
		late_frames++;
	}

	unsigned updtime = m_frame_end - m_upd_start;
	if(!m_upd_reset && updtime >= m_upd_interval) {
		data_update();
		m_upd_reset = true;
	}
}

void Bench::data_update()
{
	m_upd_count++;
	
	time_elapsed  = m_frame_end - m_init_time;
	
	min_load_time = m_min_load_time;
	max_load_time = m_max_load_time;
	avg_load_time = double(m_sum_load_time) / m_upd_frame_count;

	min_frame_time = m_min_frame_time;
	max_frame_time = m_max_frame_time;
	avg_frame_time = double(m_sum_frame_time) / m_upd_frame_count;
	cavg_frame_time = cavg_frame_time + (avg_frame_time - cavg_frame_time) / m_upd_count;
	
	double msum2 = double(m_sum_frame_time2) / m_upd_frame_count;
	std_frame_time = sqrt(msum2 - double(avg_frame_time*avg_frame_time));
	cavg_std_frame_time = cavg_std_frame_time + (std_frame_time - cavg_std_frame_time) / m_upd_count;
	
	double updtime = double(m_frame_end - m_upd_start);
	
	min_fps = 1.0e9 / max_load_time;
	max_fps = 1.0e9 / min_load_time;
	avg_fps = double(m_upd_frame_count) * 1.0e9 / updtime;
	
	load = avg_load_time / heartbeat;
}

