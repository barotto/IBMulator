/*
 * Copyright (C) 2020-2023  Marco Bortolin
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
#include "pacer.h"
#include <cmath>
#include <climits>
#include <thread>
#ifdef _WIN32
#include "wincompat.h"
#endif
#include <SDL.h>

static inline void sleep_for(int64_t _ns)
{
	#ifdef _WIN32
	// std::this_thread::sleep_for() and nanosleep() by default take a minimum
	// of 15ms and are therefore useless. To increase the resolution
	// timeBeginPeriod() and timeEndPeriod() must be used. Since timers have a
	// bit of complexity, just use SDL which will take care of the details.
	// SDL_Delay() (which uses Sleep()) has a 1ms granularity and a ~1ms cost,
	// which are values I can work with.
	SDL_Delay(_ns/1000000);
	#else
	// on Linux sleep_for() is actually very good, with a minimum sleep time of
	// only ~54000ns and a very high precision (usually within 1000ns).
	// TODO other operating systems behaviour is unknown
	std::this_thread::sleep_for( std::chrono::nanoseconds(_ns) );
	#endif
}

Pacer::Pacer()
:
m_heartbeat(0),
m_frame_time_diff(0),
m_loop_cost(0),
m_sleep_cost(0),
m_sleep_thres(1),
m_skip(false),
m_external_sync(false)
{
}

Pacer::~Pacer()
{
}

static bool is_within(double _v1, double _v2, double _t)
{
	return (_v1 <= _v2*_t);
}

static bool is_within(double _avg, double _target, double _sdev, double _t)
{
	return (_avg > _target*(1.0 - 1.0*_t)) && (_avg < _target*(1.0 + 1.0*_t)) && (_sdev < _avg*_t);
}

void Pacer::calibrate(PacerWaitMethod _method)
{
	PINFO(LOG_V0, "Calibrating...\n");
	
	m_chrono.calibrate();
	
	switch(_method) {
		case PACER_WAIT_SLEEP:
			set_forced_sleep();
			PINFOF(LOG_V0, LOG_PROGRAM, "Timing forced to thread sleep.\n");
			PINFOF(LOG_V1, LOG_PROGRAM, " Sleep cost: %lld ns\n", m_sleep_cost);
			return;
		case PACER_WAIT_BUSYLOOP:
			set_forced_busyloop();
			PINFOF(LOG_V0, LOG_PROGRAM, "Timing forced to busy loop.\n");
			PINFOF(LOG_V1, LOG_PROGRAM, " Loop cost: %lld ns\n", m_loop_cost);
			return;
		case PACER_WAIT_AUTO:
		default:
			break;
	}
	
	// I don't actually know what I'm doing here, schedulers are a tough topic.
	// My goal is to determine the minimum _reliable_ sleep time. It does not
	// have to be precise, just to be within reasonable limits that I can try
	// to compensate for using a busy loop.
	// I'm sure there's some "official" way to gather this information but my
	// google-fu is not up to the task. Although I'm not interested in "official"
	// numbers, rather in actually obtainable ones. I understand these numbers
	// are affected by the current system load, but I'm assuming if you're using
	// this emulator you're doing so like you would a videogame.
	
	double avg, std;
	double msavg = 0.0;
	
	std::tie(avg,std) = sample_loop(1e6, 100);
	PDEBUGF(LOG_V0, LOG_PROGRAM, "Loop cost (avg/sdev): %.3f/%.3f ns\n", avg, std);
	if(is_within(std, avg, 0.2)) {
		m_loop_cost = int64_t(avg + std);
	} else {
		m_loop_cost = 0;
	}

	m_sleep_cost = 0;
	m_sleep_thres = LLONG_MAX;
	
#ifndef _WIN32
	// try to sleep for 1 ns. I'm not expecting to actually sleep for such a low
	// period, instead I'm trying to determine the lowest possible period of time
	// that it takes to call a non-zero sleep.
	std::tie(avg,std) = sample_sleep(1, 50);
	PDEBUGF(LOG_V0, LOG_PROGRAM, "Sleep cost (avg/sdev): %.3f/%.3f ns\n", avg, std);
	if(is_within(std,avg,0.2) && avg < 100000) {
		// sleep cost seems to be reasonably defined
		m_sleep_cost = int64_t(avg + std);
		// try to sleep for a value close to that cost
		// it'll be no more than 0.5ms
		m_sleep_thres = m_sleep_cost * 5;
		std::tie(avg,std) = sample_sleep(m_sleep_thres - m_sleep_cost, 50);
		PDEBUGF(LOG_V0, LOG_PROGRAM, "Tried to sleep for %lld ns: avg %.3f sdev %.3f ns\n", m_sleep_thres, avg, std);
		if(is_within(avg, m_sleep_thres, std, 0.1)) {
			PINFOF(LOG_V0, LOG_PROGRAM, "This system has high precision timing. Impressive, very nice.\n");
			goto report;
		}
	}
#endif
	
	// try 1 millisecond resolution
	msavg = 0.0;
	for(int64_t thres=1e6; thres<=5e6; thres+=1e6) {
		std::tie(avg,std) = sample_sleep(thres, 10);
		PDEBUGF(LOG_V0, LOG_PROGRAM, "Tried to sleep for %.1f ms: avg %.6f, sdev %.6f ms\n", 
				thres/1.0e6, avg/1.0e6, std/1.0e6);
		if(avg > thres) {
			msavg += (avg - thres) ;
		} else {
			msavg += (thres - avg);
		}
	}
	msavg /= 5.0;
	m_sleep_cost = msavg;
	m_sleep_thres = 2e6;

	PINFOF(LOG_V0, LOG_PROGRAM, "This system has low precision timing.\n");

report:
	PINFOF(LOG_V1, LOG_PROGRAM, " Sleep cost: %lld ns, sleep threshold: %lld ns\n",
		m_sleep_cost, m_sleep_thres);
	PINFOF(LOG_V2, LOG_PROGRAM, " Loop cost: %lld\n", m_loop_cost);
}

void Pacer::calibrate(const Pacer &_p)
{
	m_chrono.calibrate(_p.m_chrono);
	m_sleep_cost = _p.m_sleep_cost;
	m_sleep_thres = _p.m_sleep_thres;
	m_loop_cost = _p.m_loop_cost;
}

void Pacer::start()
{
	m_chrono.start();
	m_frame_time_diff = 0;
}

int64_t Pacer::wait(int64_t _load_time, int64_t _prev_frame_time)
{
	if(m_skip || m_external_sync) {
		m_skip = false;
		m_frame_time_diff = 0;
		m_chrono.start();
		return 0;
	}

	int64_t time_slept = 0;
	if(_load_time < m_heartbeat) {
		if(_prev_frame_time) {
			m_frame_time_diff += m_heartbeat - _prev_frame_time;
		}
		int64_t t0, t1, diff;
		int64_t sleep_time = (m_heartbeat - _load_time) + m_frame_time_diff;
		//PDEBUGF(LOG_V2, LOG_MACHINE, "  fdiff=%lld, sleep for %d ns\n", fdiff, sleep_time);
		t0 = m_chrono.get_nsec();
		if(sleep_time > 0) {
			int64_t delay_ns = sleep_time - m_sleep_cost;
			if(delay_ns > m_sleep_thres) {
				//PDEBUGF(LOG_V2, LOG_MACHINE, "  delay %d\n", delay_ns);
				sleep_for(delay_ns);
			}
			t1 = m_chrono.get_nsec();
			diff = sleep_time - (t1 - t0);
			if(diff > m_loop_cost) {
				//PDEBUGF(LOG_V2, LOG_MACHINE, "  loop %d\n", diff);
				int64_t tloop, tstart;
				tloop = tstart = t1;
				while((tloop - tstart) < (diff - m_loop_cost)) {
					tloop = m_chrono.get_nsec();
				}
			}
		}
		t1 = m_chrono.get_nsec();
		if(t1 < t0) {
			PDEBUGF(LOG_V0, LOG_PROGRAM, "negative time span: t1:%lld < t0:%lld\n", t1, t0);
		}
		time_slept = t1 - t0;
		//PDEBUGF(LOG_V2, LOG_MACHINE, "  t0=%lld, t1=%lld, time_slept=%lld, rem=%d\n", t0, t1, time_slept, m_frame_time_diff);
	} else {
		m_frame_time_diff = 0;
	}
	
	m_chrono.start();
	
	return time_slept;
}

void Pacer::set_forced_sleep()
{
	double avg, std;
	std::tie(avg,std) = sample_sleep(1, 50);
	if(is_within(std,avg,0.2) && avg < 100000) {
		m_sleep_cost = int64_t(avg + std);
	} else {
		m_sleep_cost = 0;
	}
	m_sleep_thres = 0; // always use sleep
	m_loop_cost = LLONG_MAX; // never compensate undershoots with busy loops
}

void Pacer::set_forced_busyloop()
{
	double avg, std;
	std::tie(avg,std) = sample_loop(1e6, 100);
	if(is_within(std, avg, 0.2)) {
		m_loop_cost = int64_t(avg + std);
	} else {
		m_loop_cost = 0;
	}
	m_sleep_cost = 0;
	m_sleep_thres = LLONG_MAX; // never use sleep
}

std::pair<double,double> Pacer::sample_sleep(int64_t _target_ns, int _samples)
{
	int64_t sum = 0, sum2 = 0;
	for(int i=0; i<_samples; i++) {
		int64_t t0 = m_chrono.get_nsec();
		sleep_for(_target_ns);
		int64_t t1 = m_chrono.get_nsec();
		int64_t diff = t1 - t0;
		sum += diff;
		sum2 += diff * diff;
		
		// simulate a load, otherwise the scheduler will de-prioritize this
		// thread, putting it to sleep for progressively longer times.
		// the kernel assumes that because the thread is constantly asking to sleep
		// it has nothing useful to do.
		int64_t tloop, tstart;
		tloop = tstart = m_chrono.get_nsec();
		while((tloop - tstart) < 1e6) {
			tloop = m_chrono.get_nsec();
		}
	}
	double avg = double(sum) / _samples;
	double std = sqrt((double(sum2) / _samples) - double(avg*avg));
	
	return {avg,std};
}

std::pair<double,double> Pacer::sample_loop(int64_t _target, int _samples)
{
	int64_t sum = 0, sum2 = 0;
	for(int i=0; i<_samples; i++) {
		int64_t tloop, tstart;
		int64_t t0 = m_chrono.get_nsec();
		tloop = tstart = m_chrono.get_nsec();
		while((tloop - tstart) < _target) {
			tloop = m_chrono.get_nsec();
		}
		int64_t t1 = m_chrono.get_nsec();
		
		int64_t diff = (t1 - t0) - _target;
		if(diff < 0) {
			diff = 0;
		}
		sum += diff;
		sum2 += diff * diff;
	}
	double avg = double(sum) / _samples;
	double std = sqrt((double(sum2) / _samples) - double(avg*avg));
	
	return {avg,std};
}