/*
 * Copyright (C) 2020  Marco Bortolin
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

#ifndef IBMULATOR_PACER_H
#define IBMULATOR_PACER_H

#include "chrono.h"

enum PacerWaitMethod
{
	PACER_WAIT_AUTO,
	PACER_WAIT_SLEEP,
	PACER_WAIT_BUSYLOOP
};

class Pacer
{
protected:
	Chrono m_chrono;
	int64_t m_heartbeat;
	int64_t m_next_beat_diff;
	int64_t m_loop_cost;
	int64_t m_sleep_cost;
	int64_t m_sleep_thres;
	bool m_half_busy_loop;
	bool m_skip;
	
public:
	Pacer();
	virtual ~Pacer();
	
	void calibrate(PacerWaitMethod _method);
	void calibrate(const Pacer &_p);
	void start();
	const Chrono & chrono() const { return m_chrono; }
	void set_heartbeat(int64_t _nsec) { m_heartbeat = _nsec; }
	int64_t wait();
	void skip() { m_skip = true; }
	
private:
	void set_forced_sleep();
	void set_forced_busyloop();
	std::pair<double,double> sample_sleep(int64_t _target, int _samples);
	std::pair<double,double> sample_loop(int64_t _target, int _samples);
};

#endif