/*
 * Copyright (C) 2016  Marco Bortolin
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
	memset(&m_s, 0, sizeof(m_s));
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

	//for every timer in the savestate
	for(uint t=0; t<MAX_TIMERS; t++) {
		Timer *savtimer = (Timer*)_state.get_buf();
		if(savtimer->in_use) {
			uint mchtidx;
			// find the correct machine timer, which MUST be already registered.
			// here we reset the timing period and related data only.
			for(mchtidx=0; mchtidx<MAX_TIMERS; mchtidx++) {
				if(strcmp(m_s.timers[mchtidx].name, savtimer->name) == 0) {
					break;
				}
			}
			if(mchtidx>=MAX_TIMERS) {
				PERRF(LOG_MACHINE, "cant find timer %s\n", savtimer->name);
				throw std::exception();
			}
			if(!m_s.timers[mchtidx].in_use) {
				PERRF(LOG_MACHINE, "timer %s is not in use\n", m_s.timers[mchtidx].name);
				throw std::exception();
			}
			m_s.timers[mchtidx].period = savtimer->period;
			m_s.timers[mchtidx].time_to_fire = savtimer->time_to_fire;
			m_s.timers[mchtidx].active = savtimer->active;
			m_s.timers[mchtidx].continuous = savtimer->continuous;
		}
		_state.advance(sizeof(Timer));
	}
	m_s.time = *(uint64_t*)_state.get_buf();
	m_s.next_timer_time = *(uint64_t*)_state.get_buf();

	m_mt_time = m_s.time;
}


void EventTimers::init()
{
	m_num_timers = 0;
}

void EventTimers::reset()
{
	m_s.time = 0;
	m_s.next_timer_time = 0;
	m_mt_time = 0;
	for(unsigned i = 0; i < m_num_timers; i++) {
		if(m_s.timers[i].in_use && m_s.timers[i].active && m_s.timers[i].continuous) {
			m_s.timers[i].time_to_fire = m_s.timers[i].period;
		}
	}
}

bool EventTimers::update(uint64_t _current_time)
{
	// We need to service all the active timers, and invoke callbacks
	// from those timers which have fired.
	m_s.next_timer_time = (uint64_t) -1;
	static std::multimap<uint64_t, ushort> triggered;
	triggered.clear();
	for(unsigned i=0; i<m_num_timers; i++) {
		if(m_s.timers[i].active) {
			if(m_s.timers[i].time_to_fire <= _current_time) {

				//timers need to fire in an ordered manner
				triggered.insert(std::pair<uint64_t,ushort>(m_s.timers[i].time_to_fire,i));

			} else {

				// This timer is not ready to fire yet.
				if(m_s.timers[i].time_to_fire < m_s.next_timer_time) {
					m_s.next_timer_time = m_s.timers[i].time_to_fire;
				}

			}
		}
	}

	uint64_t prevtimer_time = 0;
	for(auto timer : triggered) {
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
		if(m_s.timers[thistimer].fire != nullptr) {
			//the current time is when the timer fires
			//time must advance in a monotonic way (that's why we use a map)
			m_s.time = thistimer_time;
			m_mt_time = thistimer_time;
			m_s.timers[thistimer].fire(m_s.time);
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

unsigned EventTimers::register_timer(timer_fun_t _func, const char *_name)
{
	unsigned timer = NULL_TIMER_HANDLE;

	if(m_num_timers >= MAX_TIMERS) {
    	PERR("register_fn: too many registered timers\n");
    	throw std::exception();
	}

	// search for new timer
	for(unsigned i = 0; i < m_num_timers; i++) {
		//check if there's another timer with the same name
		if(m_s.timers[i].in_use && strcmp(m_s.timers[i].name, _name)==0) {
			//cannot be 2 timers with the same name
			return NULL_TIMER_HANDLE;
		}
		if((!m_s.timers[i].in_use) && (timer==NULL_TIMER_HANDLE)) {
			//free timer found
			timer = i;
		}
	}
	if(timer == NULL_TIMER_HANDLE) {
		// If we didn't find a free slot, increment the bound m_num_timers.
		timer = m_num_timers;
		m_num_timers++;
	}
	m_s.timers[timer].in_use = true;
	m_s.timers[timer].period = 0;
	m_s.timers[timer].time_to_fire = 0;
	m_s.timers[timer].active = false;
	m_s.timers[timer].continuous = false;
	m_s.timers[timer].fire = _func;
	snprintf(m_s.timers[timer].name, TIMER_NAME_LEN, "%s", _name);

	return timer;
}

void EventTimers::unregister_timer(unsigned _timer)
{
	if(_timer == NULL_TIMER_HANDLE) {
		return;
	}
	assert(_timer < MAX_TIMERS);
	m_s.timers[_timer].in_use = false;
	m_s.timers[_timer].active = false;
	m_s.timers[_timer].fire = nullptr;
	_timer = NULL_TIMER_HANDLE;
}

void EventTimers::activate_timer(unsigned _timer, uint64_t _period, bool _continuous)
{
	assert(_timer<m_num_timers);

	if(_period == 0) {
		//use default stored in period field
		_period = m_s.timers[_timer].period;
	}

	m_s.timers[_timer].active = true;
	m_s.timers[_timer].period = _period;
	m_s.timers[_timer].time_to_fire = m_s.time + _period;
	m_s.timers[_timer].continuous = _continuous;

	if(m_s.timers[_timer].time_to_fire < m_s.next_timer_time) {
		m_s.next_timer_time = m_s.timers[_timer].time_to_fire;
	}
}

void EventTimers::deactivate_timer(unsigned _timer)
{
	assert(_timer<m_num_timers);

	m_s.timers[_timer].active = false;
}

uint64_t EventTimers::get_timer_eta(unsigned _timer) const
{
	assert(_timer < m_num_timers);

	if(!m_s.timers[_timer].active) {
		return 0;
	}
	assert(m_s.timers[_timer].time_to_fire >= m_s.time);
	return (m_s.timers[_timer].time_to_fire - m_s.time);
}

void EventTimers::set_timer_callback(unsigned _timer, timer_fun_t _func)
{
	assert(_timer<m_num_timers);

	m_s.timers[_timer].fire = _func;
}
