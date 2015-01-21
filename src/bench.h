/*
 * 	Copyright (c) 2015  Marco Bortolin
 *
 *	This file is part of IBMulator
 *
 *  IBMulator is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 3 of the License, or
 *	(at your option) any later version.
 *
 *	IBMulator is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with IBMulator.  If not, see <http://www.gnu.org/licenses/>.
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
	uint m_min_btime;
	uint m_max_btime;
	uint m_min_ftime;
	uint m_max_ftime;
	uint m_beat_count;
	uint m_frame_count;
	uint m_upd_interval;

	const Chrono *m_chrono;

public:
	uint64_t init_time;
	std::chrono::steady_clock::time_point c_init_time;

	uint ustart; //!< update start
	uint uend; //!< update end
	uint update_interval; //!< in ms

	uint bstart; //!< heart beat start
	uint bend;
	uint beat_count;
	uint min_beat_time;
	uint max_beat_time;
	uint min_bps;
	uint max_bps;
	float avg_bps;

	uint fstart;
	uint fend;
	float frame_rate;
	uint frame_count;
	uint min_frame_time;
	uint max_frame_time;
	uint min_fps;
	uint max_fps;
	float avg_fps;

	uint frameskip;

	uint time_elapsed;
	std::chrono::duration<double> c_time_elapsed;
	std::vector<int> frame_times;

	bool frame_reset;
	std::string endl;

	Bench();
	~Bench();

	void init(const Chrono *_chrono, int _update_interval, int _rec_buffer_size);

	void beat_start();
	void beat_end();

	void frame_start();
	void frame_end();
	void data_update();
};


void operator<<(std::ostream& _os, const Bench &_bench);




#endif
