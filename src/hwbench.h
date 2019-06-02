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
	unsigned m_min_btime;
	unsigned m_max_btime;
	unsigned m_sum_btime;
	unsigned m_beat_count;
	unsigned m_upd_interval;
	uint64_t m_icount;
	uint64_t m_ccount;
	unsigned m_heartbeat;
	bool m_reset;
	const Chrono *m_chrono;

public:
	uint64_t init_time;

	uint64_t ustart; //!< update start
	uint64_t uend; //!< update end
	uint64_t update_interval; //!< in us

	uint64_t bstart; //!< heart beat start
	uint64_t bend;
	unsigned beat_count;
	unsigned min_beat_time;
	unsigned max_beat_time;
	double   avg_beat_time;
	unsigned min_bps;
	unsigned max_bps;
	double avg_bps;
	double avg_ips;
	double avg_cps;
	double load;

	uint64_t time_elapsed;

	std::string endl;

	HWBench();
	~HWBench();

	void init(const Chrono *_chrono, uint _update_interval);
	void set_heartbeat(unsigned _usec) { m_heartbeat = _usec; }
	void beat_start();
	void beat_end();
	inline void cpu_step() { m_icount++; }
	inline void cpu_cycles(uint _cycles) { m_ccount += _cycles; }

	void data_update();
};


void operator<<(std::ostream& _os, const HWBench &_bench);




#endif
