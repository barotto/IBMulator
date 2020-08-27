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
Bench(),
m_icount(0),
m_ccount(0),
m_virt_start(0),
m_virt_end(0),
avg_ips(.0),
avg_cps(.0),
virt_frame_time(0),
vtime_ratio(1.0),
cavg_vtime_ratio(1.0)
{
}


HWBench::~HWBench()
{
}

void HWBench::start()
{
	Bench::start();
	
	vtime_ratio = 1.0;
	cavg_vtime_ratio = 1.0;
}

void HWBench::frame_start(uint64_t _virt_ns)
{
	if(m_upd_reset) {
		m_icount = 0;
		m_ccount = 0;
	}
	m_virt_start = _virt_ns;
	
	Bench::frame_start();
}

void HWBench::frame_end(uint64_t _virt_ns)
{
	Bench::frame_end();
	
	m_virt_end = _virt_ns;
	
	virt_frame_time = m_virt_end - m_virt_start;
	vtime_ratio = double(virt_frame_time) / double(frame_time);
	cavg_vtime_ratio = cavg_vtime_ratio + ( vtime_ratio - cavg_vtime_ratio ) / 60.0;
}

bool HWBench::is_stressed()
{
	int vtime_ratio_1000 = round(cavg_vtime_ratio * 1000.0);
	return (load > 0.95 && vtime_ratio_1000 < 999);
}

void HWBench::reset_values()
{
	Bench::reset_values();
	
	m_icount = 0;
	m_ccount = 0;
	avg_ips = .0;
	avg_cps = .0;
	virt_frame_time = 0;
}

void HWBench::data_update()
{
	Bench::data_update();

	double updtime = double(m_frame_end - m_upd_start);
	avg_ips = double(m_icount) * 1.0e9 / updtime;
	avg_cps = double(m_ccount) * 1.0e9 / updtime;
}


