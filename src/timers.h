/*
 * Copyright (C) 2016-2022  Marco Bortolin
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

#ifndef IBMULATOR_TIMERS_H
#define IBMULATOR_TIMERS_H

#include "statebuf.h"
#include "limits.h"

#define NULL_TIMER_ID 10000
#define TIME_NEVER ULLONG_MAX
#define US_TO_NS(us) (us*1000)
#define MSEC_PER_SECOND (1'000L)
#define USEC_PER_SECOND (1'000'000L)
#define NSEC_PER_SECOND (1'000'000'000L)
#define INV_USEC_PER_SECOND_D (0.000001)
#define NSEC_TO_USEC(nsec) (nsec/1000)
#define NSEC_TO_SEC(nsec) (double((nsec)) / 1'000'000'000.0)
#define SEC_TO_NSEC(sec) (double(sec) * 1'000'000'000.0)
#define MAX_TIMERS 24
#define TIMER_NAME_LEN 20

constexpr uint64_t operator"" _us ( unsigned long long int _t ) { return _t * MSEC_PER_SECOND; }
constexpr uint64_t operator"" _ms ( unsigned long long int _t ) { return _t * USEC_PER_SECOND; }
constexpr uint64_t operator"" _s  ( unsigned long long int _t ) { return _t * NSEC_PER_SECOND; }
constexpr uint64_t operator"" _hz ( unsigned long long int _hz ) { return (NSEC_PER_SECOND / _hz); }

constexpr double hz_to_time(uint64_t _hz) { return (double(NSEC_PER_SECOND) / _hz); }

constexpr uint64_t cycles_to_time(uint64_t _cycles, uint32_t _freq_hz)
{
	return _freq_hz ? (_cycles * hz_to_time(_freq_hz)) : TIME_NEVER;
}

constexpr uint64_t time_to_cycles(uint64_t _time, uint32_t _freq_hz)
{
	return _time * (_freq_hz / double(NSEC_PER_SECOND));
}

typedef unsigned TimerID;
typedef std::function<void(uint64_t)> TimerFn;

struct EventTimer {
	bool     in_use = false;              // Timer is in-use (currently registered)
	uint64_t period = TIME_NEVER;         // Timer periodocity
	uint64_t time_to_fire = TIME_NEVER;   // Time to fire next
	bool     active = false;              // false=inactive, true=active.
	bool     continuous = false;          // false=one-shot timer, true=continuous periodicity.
	unsigned data = 0;                    // Optional data
	char     name[TIMER_NAME_LEN] = {0};  // A human readable C-string name for this timer
};

class EventTimers
{
protected:
	struct {
		EventTimer timers[MAX_TIMERS];
		uint64_t time;
		uint64_t next_timer_time;
	} m_s;
	std::atomic<uint64_t> m_mt_time;
	unsigned m_next_timer;
	TimerFn m_callbacks[MAX_TIMERS];
	std::multimap<uint64_t,TimerID> m_triggered;
	unsigned m_log_fac = LOG_MACHINE;

public:
	EventTimers();
	~EventTimers();

	void init();
	void reset();
	void save_state(StateBuf &_state);
	void restore_state(StateBuf &_state);
	bool update(uint64_t _current_time);

	void set_time(uint64_t _time);

	uint64_t get_time() const { return m_s.time; }
	uint64_t get_time_mt() const { return m_mt_time; }
	uint64_t get_next_timer_time() const { return m_s.next_timer_time; }

	TimerID register_timer(TimerFn _func, const std::string &_name, unsigned _data = 0);
	void unregister_timer(TimerID &_timer);
	void activate_timer(TimerID _timer, uint64_t _delay, uint64_t _period, bool _continuous);
	void activate_timer(TimerID _timer, uint64_t _period, bool _continuous);
	uint64_t get_timer_eta(TimerID _timer) const;
	void deactivate_timer(TimerID _timer);
	void set_timer_callback(TimerID _timer, TimerFn _func, unsigned _data);
	bool is_timer_active(TimerID _timer) const;

	unsigned get_timers_max() const { return m_next_timer; }
	unsigned get_timers_count() const;
	const EventTimer & get_event_timer(TimerID _timer) const;

	void set_log_facility(unsigned _fac) {
		m_log_fac = _fac;
	}
};

#endif
