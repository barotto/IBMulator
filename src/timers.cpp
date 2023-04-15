/*
 * Copyright (C) 2016-2023  Marco Bortolin
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
#include "timers.h"
#include "statebuf.h"
#include <cstring>


EventTimers::EventTimers()
{
	m_s.time = 0;
	m_s.next_timer_time = TIME_NEVER;
	for(unsigned i=0; i<MAX_TIMERS; i++) {
		m_s.timers[i].in_use = false;
	}
	m_mt_time = 0;
	m_next_timer = 0;
}

EventTimers::~EventTimers()
{
}

void EventTimers::save_state(StateBuf &_state)
{
	_state.write(&m_s, {sizeof(m_s), "EventTimers"});
}

void EventTimers::restore_state(StateBuf &_state)
{
	_state.check({sizeof(m_s), "EventTimers"});

	// timers MUST be registered before calling this function

	//for every timer in the savestate
	EventTimer savtimer;
	for(unsigned t=0; t<MAX_TIMERS; t++) {
		memcpy(&savtimer, _state.get_buf(), sizeof(EventTimer));
		if(savtimer.in_use) {
			unsigned mchtidx;
			// here we reset the timing period and related data only.
			for(mchtidx=0; mchtidx<MAX_TIMERS; mchtidx++) {
				if(strcmp(m_s.timers[mchtidx].name, savtimer.name) == 0) {
					break;
				}
			}
			if(mchtidx >= MAX_TIMERS) {
				PERRF(m_log_fac, "Cannot find timer '%s'\n", savtimer.name);
				throw std::exception();
			}
			if(!m_s.timers[mchtidx].in_use) {
				PERRF(m_log_fac, "Timer '%s' is not in use\n", m_s.timers[mchtidx].name);
				throw std::exception();
			}
			m_s.timers[mchtidx].period = savtimer.period;
			m_s.timers[mchtidx].time_to_fire = savtimer.time_to_fire;
			m_s.timers[mchtidx].active = savtimer.active;
			m_s.timers[mchtidx].continuous = savtimer.continuous;
			m_s.timers[mchtidx].data = savtimer.data;
		}
		_state.advance(sizeof(EventTimer));
	}

	memcpy(&m_s.time, _state.get_buf(), sizeof(uint64_t));
	_state.advance(sizeof(uint64_t));

	memcpy(&m_s.next_timer_time, _state.get_buf(), sizeof(uint64_t));
	_state.advance(sizeof(uint64_t));

	m_mt_time = m_s.time;
}

void EventTimers::init()
{
	m_next_timer = 0;
}

void EventTimers::reset()
{
	m_s.time = 0;
	m_mt_time = 0;
	m_s.next_timer_time = TIME_NEVER;
	for(unsigned i = 0; i < m_next_timer; i++) {
		if(m_s.timers[i].in_use && m_s.timers[i].active && m_s.timers[i].continuous) {
			m_s.timers[i].time_to_fire = m_s.timers[i].period;
		}
	}
}

bool EventTimers::update(uint64_t _current_time)
{
	// We need to service all the active timers, and invoke callbacks
	// from those timers which have fired.
	m_s.next_timer_time = TIME_NEVER;
	m_triggered.clear();
	for(unsigned i=0; i<m_next_timer; i++) {
		if(m_s.timers[i].in_use && m_s.timers[i].active) {
			if(m_s.timers[i].time_to_fire <= _current_time) {

				// timers need to fire in an ordered manner
				m_triggered.emplace(m_s.timers[i].time_to_fire, i);

			} else {

				// This timer is not ready to fire yet.
				if(m_s.timers[i].time_to_fire < m_s.next_timer_time) {
					m_s.next_timer_time = m_s.timers[i].time_to_fire;
				}

			}
		}
	}

	uint64_t prevtimer_time = 0;
	for(auto &timer : m_triggered) {
		unsigned thistimer = timer.second;
		uint64_t thistimer_time = timer.first;

		assert(thistimer_time >= prevtimer_time);
		assert(thistimer_time <= _current_time);

		// Call requested timer function.  It may request a different
		// timer period or deactivate etc.
		// it can even reactivate the same timer and set it to fire BEFORE the next cycle
		if(!m_s.timers[thistimer].continuous) {
			// If triggered timer is one-shot, deactive.
			m_s.timers[thistimer].active = false;
		} else {
			// Continuous timer, increment time-to-fire by period.
			m_s.timers[thistimer].time_to_fire += m_s.timers[thistimer].period;
			if(m_s.timers[thistimer].time_to_fire < m_s.next_timer_time) {
				m_s.next_timer_time = m_s.timers[thistimer].time_to_fire;
			}
		}
		if(m_callbacks[thistimer] != nullptr) {
			// the current time is when the timer fires
			// time must advance in a monotonic way (that's why we use a map)
			m_s.time = thistimer_time;
			m_mt_time = thistimer_time;

			m_callbacks[thistimer](m_s.time);

			if(m_s.timers[thistimer].time_to_fire <= _current_time) {
				// the timer set itself to fire again before or at the time point
				// we need to reorder
				return false;
			}
		}
		prevtimer_time = thistimer_time;
	}

	m_s.time = _current_time;
	m_mt_time = _current_time;

	return true;
}

void EventTimers::set_time(uint64_t _time)
{
	m_s.time = _time;
	m_mt_time = _time;
}

TimerID EventTimers::register_timer(TimerFn _func, const std::string &_name, unsigned _data)
{
	unsigned timer = NULL_TIMER_ID;

	// search for new timer
	for(unsigned i = 0; i < m_next_timer; i++) {
		// check if there's another timer with the same name
		if(m_s.timers[i].in_use && strcmp(m_s.timers[i].name, _name.c_str())==0) {
			// cannot be 2 timers with the same name
			return NULL_TIMER_ID;
		}
		if(!m_s.timers[i].in_use) {
			// free timer found
			timer = i;
			break;
		}
	}
	if(timer == NULL_TIMER_ID) {
		// If we didn't find a free slot, increment the bound.
		if(m_next_timer >= MAX_TIMERS) {
			PERRF(m_log_fac, "Too many registered timers\n");
			throw std::exception();
		}
		timer = m_next_timer;
		m_next_timer++;
	}
	m_s.timers[timer].in_use = true;
	m_s.timers[timer].period = 0;
	m_s.timers[timer].time_to_fire = 0;
	m_s.timers[timer].active = false;
	m_s.timers[timer].continuous = false;
	m_s.timers[timer].data = _data;
	snprintf(m_s.timers[timer].name, TIMER_NAME_LEN, "%s", _name.c_str());

	m_callbacks[timer] = _func;

	PDEBUGF(LOG_V2, m_log_fac, "Timer %d registered for '%s'\n", timer, _name.c_str());

	return timer;
}

void EventTimers::unregister_timer(TimerID &_timer)
{
	if(_timer == NULL_TIMER_ID || _timer>=m_next_timer) {
		PDEBUGF(LOG_V0, m_log_fac, "Invalid TimerID!\n");
		return;
	}
	if(!m_s.timers[_timer].in_use) {
		PDEBUGF(LOG_V0, m_log_fac, "Cannot unregister timer %u: not in use!\n");
		return;
	}
	m_s.timers[_timer].in_use = false;
	m_s.timers[_timer].active = false;
	m_callbacks[_timer] = nullptr;
	assert(m_next_timer > 0);
	if(_timer == m_next_timer-1) {
		// update timers tail index
		m_next_timer--;
	}
	PDEBUGF(LOG_V2, m_log_fac, "Unregistering timer %u '%s'. Remaining timers: %u\n",
			_timer, m_s.timers[_timer].name, get_timers_count());
	_timer = NULL_TIMER_ID;
}

void EventTimers::activate_timer(TimerID _timer, uint64_t _delay, uint64_t _period, bool _continuous)
{
	if(_timer == NULL_TIMER_ID || _timer>=m_next_timer) {
		PDEBUGF(LOG_V0, m_log_fac, "Invalid TimerID!\n");
		return;
	}

	if(!m_s.timers[_timer].in_use) {
		PDEBUGF(LOG_V0, m_log_fac, "Timer %u is activated but not used!\n", _timer);
		return;
	}

	if(_period == 0) {
		//use default stored in period field
		_period = m_s.timers[_timer].period;
	}

	m_s.timers[_timer].active = true;
	m_s.timers[_timer].period = _period;
	m_s.timers[_timer].time_to_fire = m_s.time + _delay;
	m_s.timers[_timer].continuous = _continuous;

	if(m_s.timers[_timer].time_to_fire < m_s.next_timer_time) {
		m_s.next_timer_time = m_s.timers[_timer].time_to_fire;
	}
}

void EventTimers::activate_timer(TimerID _timer, uint64_t _period, bool _continuous)
{
	activate_timer(_timer, _period, _period, _continuous);
}

void EventTimers::deactivate_timer(TimerID _timer)
{
	if(_timer == NULL_TIMER_ID || _timer>=m_next_timer) {
		PDEBUGF(LOG_V0, m_log_fac, "Invalid TimerID!\n");
		return;
	}

	m_s.timers[_timer].active = false;
}

uint64_t EventTimers::get_timer_eta(TimerID _timer) const
{
	if(_timer == NULL_TIMER_ID || _timer>=m_next_timer) {
		PDEBUGF(LOG_V0, m_log_fac, "Invalid TimerID!\n");
		return TIME_NEVER;
	}

	if(!m_s.timers[_timer].active) {
		// TODO does it make sense to return 0 (now)?
		return 0;
	}
	assert(m_s.timers[_timer].time_to_fire >= m_s.time);
	return (m_s.timers[_timer].time_to_fire - m_s.time);
}

void EventTimers::set_timer_callback(TimerID _timer, TimerFn _func, unsigned _data)
{
	if(_timer == NULL_TIMER_ID || _timer>=m_next_timer) {
		PDEBUGF(LOG_V0, m_log_fac, "Invalid TimerID!\n");
		return;
	}

	m_callbacks[_timer] = _func;
	m_s.timers[_timer].data = _data;
}

bool EventTimers::is_timer_active(TimerID _timer) const
{
	if(_timer == NULL_TIMER_ID || _timer>=m_next_timer) {
		PDEBUGF(LOG_V0, m_log_fac, "Invalid TimerID!\n");
		return false;
	}

	if(m_s.timers[_timer].in_use) {
		return m_s.timers[_timer].active;
	}
	return false;
}

unsigned EventTimers::get_timers_count() const
{
	unsigned count = 0;
	for(unsigned i = 0; i < m_next_timer; i++) {
		if(m_s.timers[i].in_use) {
			count++;
		}
	}
	return count;
}

const EventTimer & EventTimers::get_event_timer(TimerID _timer) const
{
	static EventTimer dummy;
	if(_timer == NULL_TIMER_ID || _timer>=m_next_timer) {
		PDEBUGF(LOG_V0, m_log_fac, "Invalid TimerID!\n");
		return dummy;
	}
	if(!m_s.timers[_timer].in_use) {
		return dummy;
	}
	return m_s.timers[_timer];
}