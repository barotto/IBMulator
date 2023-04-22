/*
 * Copyright (C) 2015-2023  Marco Bortolin
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
#include "chrono.h"
#include <chrono>
#include <SDL.h>

Chrono_RDTSC::Chrono_RDTSC()
:
m_freq_hz(0),
m_cyc_ms_inv(.0),
m_cyc_us_inv(.0),
m_cyc_ns_inv(.0),
m_last_ticks(0)
{
}

#if 0
//SDL method is imprecise
void Chrono_RDTSC::calibrate()
{
	volatile uint time0 = SDL_GetTicks();
	volatile uint64_t time0_c = Chrono_RDTSC::get_ticks();
	volatile uint time1 = SDL_GetTicks();
	volatile uint64_t time1_c = Chrono_RDTSC::get_ticks();
	volatile uint elapsed = time1-time0;
	while(elapsed < 2000) {
		time1 = SDL_GetTicks();
		time1_c = Chrono_RDTSC::get_ticks();
		elapsed = time1-time0;
	}
	double freq = time1_c - time0_c;
	freq /= double(elapsed) / 1000.0;
	set_freq(freq);
}
#else
void Chrono_RDTSC::calibrate()
{
	auto time0 = std::chrono::steady_clock::now();
	volatile uint64_t time0_c = Chrono_RDTSC::get_ticks();
	auto time1 = std::chrono::steady_clock::now();
	volatile uint64_t time1_c = Chrono_RDTSC::get_ticks();
	auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(time1-time0);
	while(elapsed.count() < 1000000000LL) {
		time1 = std::chrono::steady_clock::now();
		time1_c = Chrono_RDTSC::get_ticks();
		elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(time1-time0);
	}
	double freq = time1_c - time0_c;
	freq /= double(elapsed.count()) / 1.0e9;
	set_freq(freq);
	PDEBUGF(LOG_V0, LOG_PROGRAM, "%f Hz\n", freq);
}
#endif

void Chrono_RDTSC::set_freq(uint64_t _freq_hz)
{
	m_freq_hz = _freq_hz;
	m_cyc_ms_inv = 1.0 / (double(m_freq_hz)/1.0e3);
	m_cyc_us_inv = 1.0 / (double(m_freq_hz)/1.0e6);
	m_cyc_ns_inv = 1.0 / (double(m_freq_hz)/1.0e9);
}


void Chrono_RDTSC::calibrate(const Chrono_RDTSC &_c)
{
	set_freq(_c.get_freq());
}

void Chrono_CPP11::calibrate()
{
	m_start = std::chrono::steady_clock::now();
}

void Chrono_CPP11::calibrate(const Chrono_CPP11 &_c)
{
	m_start = _c.m_start;
}
