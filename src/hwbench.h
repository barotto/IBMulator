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
#include "chrono.h"

class HWBench
{
protected:
	int64_t  m_min_btime;
	int64_t  m_max_btime;
	int64_t  m_sum_btime;
	int64_t  m_beat_time;
	uint64_t m_beat_count;
	int64_t  m_upd_interval;
	uint64_t m_icount;
	uint64_t m_ccount;
	bool m_reset;
	const Chrono *m_chrono;

public:
	int64_t init_time;

	int64_t ustart; //!< update start
	int64_t uend; //!< update end

	int64_t  heartbeat;
	
	int64_t  bstart; //!< heart beat start
	int64_t  bend;
	unsigned beat_count;
	uint64_t total_beat_count;
	unsigned missed_frames;
	int64_t  min_beat_time;
	int64_t  max_beat_time;
	double   avg_beat_time;
	unsigned min_bps;
	unsigned max_bps;
	double avg_bps;
	double avg_ips;
	double avg_cps;
	std::atomic<double> load; // the load value is used in the gui thread to decide
	                          // if it's safe to keep the synchro between threads 

	int64_t time_elapsed;

	std::string endl;

	HWBench();
	~HWBench();

	void init(const Chrono *_chrono, uint _update_interval);
	void reset();
	void set_heartbeat(int64_t _nsec) { heartbeat = _nsec; }
	void beat_start();
	void beat_end();
	inline void cpu_step() { m_icount++; }
	inline void cpu_cycles(uint _cycles) { m_ccount += _cycles; }

	void data_update();
	
	friend std::ostream& operator<<(std::ostream& out, const HWBench &_bench);
};


std::ostream& operator<<(std::ostream& _os, const HWBench &_bench);




#endif
