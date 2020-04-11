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
	
public:
	double avg_ips; // average CPU instructions per second
	double avg_cps; // average CPU cycles per second
	
	HWBench();
	virtual ~HWBench();

	void pause();
	void frame_start();
	
	inline void cpu_step() { m_icount++; }
	inline void cpu_cycles(unsigned _cycles) { m_ccount += _cycles; }
	
protected:
	void data_update();
};


#endif
