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

#include "ibmulator.h"
#include "pcspeaker.h"
#include "program.h"

PCSpeaker g_pcspeaker;


PCSpeaker::PCSpeaker()
{
}

PCSpeaker::~PCSpeaker()
{
}

void PCSpeaker::init()
{
	config_changed();
}

void PCSpeaker::config_changed()
{
	m_rate = g_program.config().get_int(MIXER_SECTION, MIXER_RATE);
	if(m_channel) {
		g_mixer.unregister_channel(get_name());
	}
	m_channel = g_mixer.register_channel(
			std::bind(&PCSpeaker::create_samples, this, std::placeholders::_1),
			m_rate, get_name());
	//a second worth of samples
	m_samples_buffer.resize(m_rate);
	m_nsec_per_sample = 1e9/double(m_rate);
	m_samples_per_nsec = double(m_rate)/1e9;
	reset(0);
}

void PCSpeaker::reset(uint)
{
	std::lock_guard<std::mutex> lock(m_lock);
	m_events.clear();
	m_s.level = 0.0;
	m_s.samples = 0.0;
	m_s.velocity = 0.0;
}

void PCSpeaker::power_off()
{
	m_channel->enable(false);
	reset(0);
}

void PCSpeaker::save_state(StateBuf &_state)
{
	PINFOF(LOG_V1, LOG_AUDIO, "pcspeaker: saving state\n");

	StateHeader h;

	m_s.events_cnt = m_events.size();

	std::lock_guard<std::mutex> lock(m_lock);
	h.name = get_name();
	h.data_size = sizeof(m_s);
	_state.write(&m_s,h);

	if(m_s.events_cnt) {
		//trying to serialize the events.
		//maybe use something like Boost serialization lib?
		//I don't like Boost, I'd rather to not include it in the project
		//for this case only.
		h.name = std::string(get_name()) + "-evts";
		h.data_size = m_s.events_cnt * sizeof(SpeakerEvent);
		std::unique_ptr<uint8_t[]> spkrevts(new uint8_t[h.data_size]);
		uint8_t *ptr = spkrevts.get();
		for(auto ev : m_events) {
			*((SpeakerEvent*)ptr) = ev;
			ptr += sizeof(SpeakerEvent);
		}
		_state.write(spkrevts.get(),h);
	}
}

void PCSpeaker::restore_state(StateBuf &_state)
{
	PINFOF(LOG_V1, LOG_AUDIO, "pcspeaker: restoring state\n");

	StateHeader h;
	h.name = get_name();
	h.data_size = sizeof(m_s);
	_state.read(&m_s,h);

	std::lock_guard<std::mutex> lock(m_lock);
	m_events.clear();
	if(m_s.events_cnt) {
		_state.get_next_lump_header(h);
		if(h.name.compare(std::string(get_name()) + "-evts") != 0) {
			PERRF(LOG_AUDIO, "speaker events expected in state buffer, found %s\n", h.name.c_str());
			throw std::exception();
		}
		size_t expsize = m_s.events_cnt*sizeof(SpeakerEvent);
		if(h.data_size != expsize) {
			PERRF(LOG_AUDIO, "speaker events size mismatch in state buffer, expected %u, found %u\n",
					expsize, h.data_size);
			throw std::exception();
		}
		std::unique_ptr<uint8_t[]> spkrevts(new uint8_t[h.data_size]);
		uint8_t *ptr = spkrevts.get();
		_state.read(ptr,h);
		for(size_t i=0; i<m_s.events_cnt; i++) {
			m_events.push_back(*((SpeakerEvent*)ptr));
			ptr += sizeof(SpeakerEvent);
		}

		m_channel->enable(true);
	}
}

void PCSpeaker::activate()
{
	m_channel->enable(true);
}

void PCSpeaker::add_event(uint64_t _time, bool _active, bool _out)
{
	std::lock_guard<std::mutex> lock(m_lock);

	if(m_events.size()) {
		ASSERT(m_events.back().time <= _time);
	}

	SpeakerEvent event;
	event.time = _time;
	event.active = _active;
	event.out = _out;

	m_events.push_back(event);
}


#define SPKR_VOLUME 10000.0
#define SPKR_SPEED SPKR_VOLUME/60000.0
#define SPKR_ACC (SPKR_VOLUME*2.0)/(60000.0*60000.0)
#define ACC 1

#if ACC
inline void speaker_level(bool _out, double &_v0, double &_s, double _t)
{
	double a;
	if(_out) {
		a = SPKR_ACC;
	} else {
		a = -SPKR_ACC;
	}
	double v = _v0 + a * _t;
	double s = (_v0 + v) / 2.0 * _t;

	_s += s;

	if(_s < 0.0) {
		_s = 0.0;
		v = 0.0;
	} else if(_s > SPKR_VOLUME) {
		_s = SPKR_VOLUME;
		v = 0.0;
	}

	_v0 = v;
}
#else
#if 1
inline double speaker_level(bool _out, double _cur_lev, double _interval)
{
	double level = _cur_lev;
	if(_out) {
		if(_cur_lev < SPKR_VOLUME) {
			level = std::min(SPKR_VOLUME, _cur_lev + (SPKR_SPEED*_interval));
		}
	} else {
		if(_cur_lev > 0.0) {
			level = std::max(0.0, _cur_lev - (SPKR_SPEED*_interval));
		}
	}
	return level;
}
#else
inline double speaker_level(bool _out, double _cur_lev, double _interval)
{
	if(_out) {
		return SPKR_VOLUME;
	} else {
		return 0.0;
	}
}
#endif
#endif


//this method is called by the Mixer thread
void PCSpeaker::create_samples(uint64_t _time)
{
	m_lock.lock();

	size_t size = m_events.size();
	if(size==0 || m_events[0].time > _time) {
		m_lock.unlock();
		return;
	}
	uint32_t samples_cnt = 0;

	for(uint32_t i=0; i<size; i++) {
		SpeakerEvent front = m_events[0];
		uint64_t begin = front.time;
		uint64_t end = _time;
		if(i<size-1) {
			end = m_events[1].time;
			if(_time < end) {
				end = _time;
				m_events[0].time = _time;
			} else {
				m_events.pop_front();
			}
		} else {
			//we are at the last event
			if(front.active) {
				m_events[0].time = _time;
			} else {
				//the last event is a shutdown
				m_events.pop_front();
			}
		}
		if((m_s.velocity<.0 && front.out) || (m_s.velocity>.0 && !front.out)) {
			m_s.velocity = 0.0;
		}
		double v = m_s.velocity;
		double l = m_s.level;

		//interval until the next audio sample
		double interval = m_nsec_per_sample - (m_s.samples * m_nsec_per_sample);
#if ACC
		speaker_level(front.out, v, l, interval);
#else
		l = speaker_level(front.out, l, interval);
#endif

		double duration = end - begin;

		m_s.samples += (duration * m_samples_per_nsec);

		uint j;
		for(j=0; j<uint(m_s.samples); j++) {

			if(samples_cnt < m_samples_buffer.size()) {
				m_samples_buffer[samples_cnt] = int16_t(round(l));
				samples_cnt++;
			}
#if ACC
			speaker_level(front.out, v, l, m_nsec_per_sample);
#else
			l = speaker_level(front.out, l, m_nsec_per_sample);
#endif
		}

#if ACC
		speaker_level(front.out, m_s.velocity, m_s.level, duration);
#else
		m_s.level = speaker_level(front.out, m_s.level, duration);
#endif

		m_s.samples -= j;

		if(end==_time) {
			break;
		}
	}

	bool chan_enable = !m_events.empty();
	m_lock.unlock();

	if(samples_cnt>0) {
		m_channel->add_samples((uint8_t*)(m_samples_buffer.data()), samples_cnt*2);
	}

	if(!chan_enable) {
		m_s.level = 0.0;
		m_s.samples = 0.0;
		m_s.velocity = 0.0;
		m_channel->enable(false);
		PDEBUGF(LOG_V1, LOG_PIT, "speaker channel disabled\n");
	}
}


