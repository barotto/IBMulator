/*
 * Copyright (C) 2016-2020  Marco Bortolin
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

typedef std::function<void(uint64_t)> timer_fun_t;

#define US_TO_NS(us) (us*1000)
#define USEC_PER_SECOND (1000000)
#define NSEC_PER_SECOND (1000000000L)
#define INV_USEC_PER_SECOND_D (0.000001)
#define NSEC_TO_USEC(nsec) (nsec/1000)
#define MAX_TIMERS 24
#define NULL_TIMER_HANDLE 10000
#define TIMER_NAME_LEN 20

constexpr uint64_t operator"" _us ( unsigned long long int _t ) { return _t * 1000L; }
constexpr uint64_t operator"" _ms ( unsigned long long int _t ) { return _t * 1000000L; }
constexpr uint64_t operator"" _s  ( unsigned long long int _t ) { return _t * 1000000000L; }

struct Timer {
	bool        in_use;       // Timer is in-use (currently registered)
	uint64_t    period;       // Timer periodocity
	uint64_t    time_to_fire; // Time to fire next
	bool        active;       // false=inactive, true=active.
	bool        continuous;   // false=one-shot timer, true=continuous periodicity.
	timer_fun_t fire;         // A callback function for when the timer fires.
	char        name[TIMER_NAME_LEN];
};

class EventTimers
{
private:
	struct {
		Timer timers[MAX_TIMERS];
		uint64_t time;
		uint64_t next_timer_time;
	} m_s;
	std::atomic<uint64_t> m_mt_time;
	uint m_num_timers;

public:
	EventTimers();
	~EventTimers();

	void init();
	void reset();
	void save_state(StateBuf &_state);
	void restore_state(StateBuf &_state);
	bool update(uint64_t _current_time);

	inline uint64_t get_time() const { return m_s.time; }
	inline uint64_t get_time_mt() const { return m_mt_time; }

	unsigned register_timer(timer_fun_t _func, const char *_name);
	void unregister_timer(unsigned _timer);
	void activate_timer(unsigned _timer, uint64_t _period, bool _continuous);
	void activate_timer(unsigned _timer, uint64_t _start, uint64_t _period, bool _continuous);
	uint64_t get_timer_eta(unsigned _timer) const;
	void deactivate_timer(unsigned _timer);
	void set_timer_callback(unsigned _timer, timer_fun_t _func);
	inline bool is_timer_active(unsigned _timer) const {
		assert(_timer < m_num_timers);
		return m_s.timers[_timer].active;
	}
/*
	const Timer & operator[](unsigned _timer) const {
		assert(_timer < m_num_timers);
		return m_s.timers(_timer);
	}
*/
};

#endif
