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


#ifndef IBMULATOR_CHRONO_H
#define IBMULATOR_CHRONO_H

#include "ibmulator.h"
#include <chrono>

#define RDTSCP(low) __asm__ __volatile__ ("rdtscp" : "=A" (low));

/*
 * Chronograph that use the RDTSC instruction to keep the time.
 * It is slightly faster than Chrono_CPP11 but it only works on a CPU that has a
 * constant rate TSC (Intel Pentium 4 and later and AMD K10 and later).
 */
class Chrono_RDTSC
{
public:
	typedef uint64_t tick;
	typedef uint64_t duration;

protected:
	uint64_t m_freq_hz;
	double m_cyc_ms_inv;
	double m_cyc_us_inv;
	double m_cyc_ns_inv;

	uint64_t m_last_ticks;

	GCC_ATTRIBUTE(always_inline)
	static inline uint64_t get_ticks()
	{
	    uint32_t low, high;
	    __asm__ __volatile__ ("rdtscp" : "=a" (low), "=d" (high));
	    return ((uint64_t)high << 32) | low;
	}

	inline uint64_t elapsed_ticks() const {
		uint64_t now = get_ticks();
		uint64_t elapsed = now - m_last_ticks;
		return elapsed;
	}

	void set_freq(uint64_t _freq_hz);

public:

	Chrono_RDTSC();

	void calibrate();
	void calibrate(const Chrono_RDTSC &);

	inline uint64_t get_freq() const { return m_freq_hz; }

	inline uint64_t get_usec() const {
		return uint64_t(double(get_ticks())*m_cyc_us_inv);
	}

	inline uint64_t get_msec() const {
		return uint64_t(double(get_ticks())*m_cyc_ms_inv);
	}

	inline uint64_t get_nsec(duration _ticks) const {
		return uint64_t(double(_ticks)*m_cyc_ns_inv);
	}

	inline uint64_t get_usec(duration _ticks) const {
		return uint64_t(double(_ticks)*m_cyc_us_inv);
	}

	inline uint64_t get_msec(duration _ticks) const {
		return uint64_t(double(_ticks)*m_cyc_ms_inv);
	}

	inline tick start() {
		m_last_ticks = get_ticks();
		return m_last_ticks;
	}

	inline uint64_t elapsed_usec() const {
		return get_usec(elapsed_ticks());
	}
	inline uint64_t elapsed_msec() const {
		return get_msec(elapsed_ticks());
	}
};


class Chrono_CPP11
{
public:
	typedef std::chrono::high_resolution_clock::time_point  tick;

protected:

	tick m_start;
	tick m_last_ticks;

public:

	Chrono_CPP11() {}

	void calibrate() { m_start = std::chrono::high_resolution_clock::now(); }
	void calibrate(const Chrono_CPP11 &_c) { m_start = _c.m_start; }

	uint64_t get_freq() const { return 0; }

	inline uint64_t get_usec() const {
		tick now = std::chrono::high_resolution_clock::now();
		std::chrono::microseconds elapsed =
				std::chrono::duration_cast<std::chrono::microseconds>(now - m_start);
		return static_cast<uint64_t>(elapsed.count());
	}
	inline uint64_t get_msec() const {
		tick now = std::chrono::high_resolution_clock::now();
		std::chrono::milliseconds elapsed =
				std::chrono::duration_cast<std::chrono::milliseconds>(now - m_start);
		return static_cast<uint64_t>(elapsed.count());
	}

	inline uint64_t get_usec(tick _now) const {
		std::chrono::microseconds elapsed =
				std::chrono::duration_cast<std::chrono::microseconds>(_now - m_start);
		return static_cast<uint64_t>(elapsed.count());
	}

	inline uint64_t get_msec(tick _now) const {
		std::chrono::milliseconds elapsed =
				std::chrono::duration_cast<std::chrono::milliseconds>(_now - m_start);
		return static_cast<uint64_t>(elapsed.count());
	}

	inline tick start() {
		m_last_ticks = std::chrono::high_resolution_clock::now();
		return m_last_ticks;
	}

	inline uint64_t elapsed_usec() const {
		tick now = std::chrono::high_resolution_clock::now();
		std::chrono::microseconds elapsed =
				std::chrono::duration_cast<std::chrono::microseconds>(now - m_last_ticks);
		return static_cast<uint64_t>(elapsed.count());
	}
	inline uint64_t elapsed_msec() const {
		tick now = std::chrono::high_resolution_clock::now();
		std::chrono::milliseconds elapsed =
				std::chrono::duration_cast<std::chrono::milliseconds>(now - m_last_ticks);
		return static_cast<uint64_t>(elapsed.count());
	}
};

#if CHRONO_RDTSC
	typedef Chrono_RDTSC Chrono;
#else
	typedef Chrono_CPP11 Chrono;
#endif

#endif
