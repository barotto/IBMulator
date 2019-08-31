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


#ifndef IBMULATOR_BENCH_H
#define IBMULATOR_BENCH_H

#include <chrono>
#include <iostream>
#include <vector>
#include "chrono.h"


class Bench
{
protected:
	int64_t m_min_ftime;
	int64_t m_max_ftime;
	int64_t m_frame_count;
	int64_t m_upd_interval;
	

	const Chrono *m_chrono;

public:
	int64_t init_time;
	std::chrono::steady_clock::time_point c_init_time;

	int64_t ustart; //!< update start
	int64_t uend; //!< update end
	int64_t update_interval;

	int64_t heartbeat;
	
	int64_t fstart;
	int64_t fend;
	int64_t frame_time;
	double frame_rate;
	unsigned frame_count;
	int64_t min_frame_time;
	int64_t max_frame_time;
	unsigned min_fps;
	unsigned max_fps;
	double avg_fps;
	unsigned long_frames;
	
	int64_t time_elapsed;
	std::chrono::duration<double> c_time_elapsed;
	std::vector<int64_t> frame_times;

	bool frame_reset;
	std::string endl;

	Bench();
	~Bench();

	void init(const Chrono *_chrono, int _update_interval, int _rec_buffer_size);
	void set_heartbeat(int64_t _nsec) { heartbeat = _nsec; }
	
	void frame_start();
	void frame_end();
	void data_update();
};


void operator<<(std::ostream& _os, const Bench &_bench);




#endif
