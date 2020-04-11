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


#ifndef IBMULATOR_BENCH_H
#define IBMULATOR_BENCH_H

#include <iostream>
#include <vector>
#include "chrono.h"

class Bench
{
protected:
	const Chrono *m_chrono;
	
	int64_t  m_init_time;
	int64_t  m_upd_interval;
	
	int64_t  m_min_load_time;
	int64_t  m_max_load_time;
	int64_t  m_sum_load_time;
	
	int64_t  m_min_frame_time;
	int64_t  m_max_frame_time;
	int64_t  m_sum_frame_time;
	int64_t  m_sum_frame_time2;
	
	uint64_t m_upd_frame_count;
	
	int64_t  m_frame_start; // frame start time
	int64_t  m_load_start;  // computation start time
	int64_t  m_frame_end;   // frame end time
	
	int64_t  m_upd_start;
	int64_t  m_upd_end;
	
	bool m_upd_reset;
	
public:
	int64_t  heartbeat; // duration in ns of each heartbeat or target frame time
	                    // measured frame time must be as close as possible to this value
	
	int64_t  time_elapsed;
	uint64_t tot_frame_count;
	unsigned late_frames;
	
	// time spent doing computation
	int64_t  load_time;
	int64_t  min_load_time;
	int64_t  max_load_time;
	double   avg_load_time;
	
	// frame time (or beat time) is the total time spent computing + sleeping
	int64_t  frame_time;
	int64_t  min_frame_time;
	int64_t  max_frame_time;
	double   avg_frame_time;
	double   std_frame_time; // Standard deviation
	
	unsigned min_fps;
	unsigned max_fps;
	double   avg_fps;
	
	std::atomic<double> load; // the load value can be used by multiple threads

	
	Bench();
	virtual ~Bench();

	void init(const Chrono *_chrono, unsigned _update_interval);
	void reset();
	void set_heartbeat(int64_t _nsec) { heartbeat = _nsec; }
	virtual void frame_start();
	virtual void load_start();
	virtual void frame_end();
	virtual void pause();
	
protected:
	virtual void data_update();
};


#endif
