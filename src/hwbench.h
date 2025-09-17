/*
 * Copyright (C) 2015-2025  Marco Bortolin
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


#ifndef IBMULATOR_HWBENCH_H
#define IBMULATOR_HWBENCH_H


#include <iostream>
#include <vector>
#include "bench.h"

class HWBench : public Bench
{
protected:

	uint64_t m_icount;
	uint64_t m_ccount;
	uint64_t m_virt_start;
	uint64_t m_virt_end;
	
public:
	double avg_ips; // average CPU instructions per second
	double avg_cps; // average CPU cycles per second
	uint64_t virt_frame_time;
	std::atomic<double> vtime_ratio;      // virtual/real speed ratio
	std::atomic<double> cavg_vtime_ratio; // cumulative average of virtual/real time ratio over the last 60 frames
	
	HWBench();
	virtual ~HWBench();

	void start() override;
	void reset_values() override;
	using Bench::frame_start;
	using Bench::frame_end;
	void frame_start(uint64_t _virt_ns);
	void frame_end(uint64_t _virt_ns);
	
	void cpu_step() { m_icount++; }
	void cpu_cycles(unsigned _cycles) { m_ccount += _cycles; }
	
	bool is_stressed();
	
protected:
	void data_update();
};


#endif
