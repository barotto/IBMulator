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


#ifndef IBMULATOR_HWBENCH_H
#define IBMULATOR_HWBENCH_H


#include <iostream>
#include <vector>
#include "chrono.h"

class HWBench
{
protected:
	uint m_min_btime;
	uint m_max_btime;
	uint m_beat_count;
	uint64_t m_upd_interval;
	bool m_reset;
	uint64_t m_icount;
	uint64_t m_ccount;

	const Chrono *m_chrono;

public:
	uint64_t   init_time;

	uint64_t ustart; //!< update start
	uint64_t uend; //!< update end
	uint64_t update_interval; //!< in us

	uint64_t bstart; //!< heart beat start
	uint64_t bend;
	uint  beat_count;
	uint  min_beat_time;
	uint  max_beat_time;
	uint  min_bps;
	uint  max_bps;
	double avg_bps;
	double avg_ips;
	double avg_cps;

	uint64_t time_elapsed;

	std::string endl;

	HWBench();
	~HWBench();

	void init(const Chrono *_chrono, uint _update_interval);

	void beat_start();
	void beat_end();
	inline void cpu_step() { m_icount++; }
	inline void cpu_cycles(uint _cycles) { m_ccount += _cycles; }

	void data_update();
};


void operator<<(std::ostream& _os, const HWBench &_bench);




#endif
