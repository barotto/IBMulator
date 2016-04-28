/*
 * Copyright (C) 2015, 2016  Marco Bortolin
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
#include "pcspeaker.h"
#include "program.h"
#include "pit.h"
#include "machine.h"
#include <cmath>

#define SPKR_DISABLE_TIMEOUT 2500000 //in usecs

IODEVICE_PORTS(PCSpeaker) = {};


PCSpeaker::PCSpeaker(Devices *_dev)
: IODevice(_dev),
m_last_time(0),
m_samples_rem(0.0)
{
}

PCSpeaker::~PCSpeaker()
{
}

void PCSpeaker::install()
{
	m_channel = g_mixer.register_channel(
		std::bind(&PCSpeaker::create_samples, this,
		std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
		name());
	m_channel->set_disable_timeout(SPKR_DISABLE_TIMEOUT);
}

void PCSpeaker::remove()
{
	g_mixer.unregister_channel(m_channel);
}

void PCSpeaker::config_changed()
{
	uint32_t rate = g_program.config().get_int(MIXER_SECTION, MIXER_RATE);
	m_channel->set_in_spec({AUDIO_FORMAT_S16, 1, rate});
	//a second worth of samples
	m_samples_buffer.resize(rate);
	m_nsec_per_sample = 1e9/double(rate);
	m_samples_per_nsec = double(rate)/1e9;
	reset(0);
}

void PCSpeaker::reset(uint)
{
	std::lock_guard<std::mutex> lock(m_mutex);
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
	PINFOF(LOG_V1, LOG_AUDIO, "PC speaker: saving state\n");

	StateHeader h;

	m_s.events_cnt = m_events.size();

	std::lock_guard<std::mutex> lock(m_mutex);
	h.name = name();
	h.data_size = sizeof(m_s);
	_state.write(&m_s,h);

	if(m_s.events_cnt) {
		//trying to serialize the events.
		//maybe use something like Boost serialization lib?
		//I don't like Boost, I'd rather to not include it in the project
		//for this case only.
		h.name = std::string(name()) + "-evts";
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
	PINFOF(LOG_V1, LOG_AUDIO, "PC speaker: restoring state\n");
	StateHeader h;
	h.name = name();
	h.data_size = sizeof(m_s);
	_state.read(&m_s,h);

	std::lock_guard<std::mutex> lock(m_mutex);
	m_events.clear();
	m_channel->enable(false);
	m_last_time = 0;
	m_samples_rem = 0;
	if(m_s.events_cnt) {
		_state.get_next_lump_header(h);
		if(h.name.compare(std::string(name()) + "-evts") != 0) {
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
	if(!m_channel->is_enabled()) {
		m_last_time = 0;
		m_samples_rem = 0;
		m_channel->enable(true);
	}
}

void PCSpeaker::add_event(uint64_t _time, bool _active, bool _out)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	static uint64_t last_time = 0;
	uint64_t elapsed = _time - last_time;
	PDEBUGF(LOG_V2, LOG_AUDIO, "PC speaker evt: %07llu dns, %s, %s\n",
			elapsed, (_active?" act":"!act"), (_out?"5v":"0v"));
	last_time = _time;

	if(m_events.size()) {
		SpeakerEvent &evt = m_events.back();
		assert(_time >= evt.time);
		if(evt.time == _time) {
			evt.out = _out;
			evt.active = _active;
			return;
		}
	}

	m_events.push_back({_time, _active, _out});
}


const double SPKR_VOLUME = 1.0;
const double SPKR_TIME = 62012.0;
const double SPKR_SPEED = (SPKR_VOLUME/SPKR_TIME);
const double SPKR_ACC = (SPKR_VOLUME/(SPKR_TIME*SPKR_TIME));
#define ACC 0

#if ACC
inline int16_t speaker_level(bool _out, double &_v0, double &_s, double _t)
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

	return int16_t(_s*10000.0);
}
#else
#if 1
inline int16_t speaker_level(bool _out, double &_v0, double &_s, double _t)
{
	if(_out) {
		_s = std::min(1.0, _s + (SPKR_SPEED*_t));
	} else {
		_s = std::max(0.0, _s - (SPKR_SPEED*_t));
	}
	_v0 = 0;
	return int16_t(_s*10000.0);
}
#else
inline int16_t speaker_level(bool _out, double &_v0, double &_s, double _t)
{
	if(_out) {
		_s = SPKR_VOLUME;
	} else {
		_s = 0.0;
	}
	_v0 = 0;
	return int16_t(_s*10000.0);
}
#endif
#endif

size_t PCSpeaker::fill_samples_buffer_t(int _nsec, int _bstart, int16_t _value)
{
	int samples = round(_nsec * m_samples_per_nsec);
	samples = std::min(int(m_samples_buffer.size())-_bstart, samples);
	std::fill(m_samples_buffer.begin()+_bstart,
	          m_samples_buffer.begin()+_bstart+samples,
	          _value);
	return samples;
}

//this method is called by the Mixer thread
bool PCSpeaker::create_samples(uint64_t _time_span_us, bool _prebuf, bool /*_first_upd*/)
{
	//TODO this function is a mess
	m_mutex.lock();
	uint64_t pit_time = g_devices.pit()->get_pit_time_ns_mt();

	double this_slice_samples = _time_span_us * 1000 / m_nsec_per_sample;
	size_t size = m_events.size();

	PDEBUGF(LOG_V2, LOG_AUDIO, "PC speaker: mix slice: %04d usecs, samples: %.1f, evnts: %d, ",
			_time_span_us, this_slice_samples, size);

	if(size==0 || m_events[0].time > pit_time) {
		m_mutex.unlock();
		unsigned samples = unsigned(std::max(0, int(this_slice_samples + m_samples_rem)));
		if(m_channel->check_disable_time(NSEC_TO_USEC(pit_time))) {
			m_last_time = 0;
			m_samples_rem = 0.0;
			return false;
		} else if(m_last_time && samples && !_prebuf) {
			PDEBUGF(LOG_V2, LOG_AUDIO, "silence fill: %u samples\n", samples);
			m_channel->in().fill_samples<int16_t>(samples, 0);
		}
		m_last_time = pit_time;
		m_samples_rem += this_slice_samples - samples;
		if(_prebuf) {
			m_samples_rem = std::min(0.0, m_samples_rem);
		}
		m_channel->input_finish();
		return true;
	}

	m_channel->set_disable_time(0);
	uint32_t pregap = 0,  samples_cnt = 0;

	if(m_last_time && (m_events[0].time>m_last_time)) {
		//fill the gap
		pregap = fill_samples_buffer_t(m_events[0].time-m_last_time, 0, round(m_s.level));
		PDEBUGF(LOG_V2, LOG_AUDIO, "pregap fill: %d, ", pregap);
		samples_cnt = pregap;
	}

	uint64_t evnts_begin = m_events[0].time;
	uint64_t end = pit_time;
	for(size_t i=0; i<size; i++) {
		SpeakerEvent front = m_events[0];
		uint64_t begin = front.time;
		if(begin > pit_time) {
			// an event is in the future when the lock is acquired after a new
			// event and before the pit time is updated
			break;
		}
		if(i<size-1) {
			end = m_events[1].time;
			m_events.pop_front();
		} else {
			//this is the last event
			end = pit_time;
		}
		if((m_s.velocity<.0 && front.out) || (m_s.velocity>.0 && !front.out)) {
			m_s.velocity = 0.0;
		}
		double v = m_s.velocity;
		double l = m_s.level;
		//interval until the next audio sample
		double next_sample = m_nsec_per_sample - (m_s.samples * m_nsec_per_sample);
		double duration = (end - begin);
		m_s.samples += (duration * m_samples_per_nsec);

		uint j = 0;
		for(j=0; j<(uint)m_s.samples; j++,samples_cnt++) {
			if(samples_cnt >= m_samples_buffer.size()) {
				break;
			}
			int16_t sample = speaker_level(front.out, v, l, next_sample);
			m_samples_buffer[samples_cnt] = sample;
			next_sample = m_nsec_per_sample;
		}

		speaker_level(front.out, m_s.velocity, m_s.level, duration);

		PDEBUGF(LOG_V2, LOG_AUDIO, "evt:%.4f-%.4f-%.4f, ", duration, m_s.samples, m_s.level);

		m_s.samples -= j;

		if(end == pit_time) {
			if(i==size-1) {
				//last event
				// if the speaker is active or not settled continue to emulate
				if(front.active || m_s.level>0.0) {
					m_events[0].time = pit_time;
				} else {
					//the last event is a shutdown and the speaker is quiet
					m_events.pop_front();
				}
			}
			break;
		}
	}

	bool chan_disable = m_events.empty();
	m_mutex.unlock();

	PDEBUGF(LOG_V2, LOG_AUDIO, "evnts len: %llu nsec, evnts samples: %d, ",
			(end - evnts_begin), (samples_cnt - pregap));

	if(end < pit_time) {
		//fill the gap
		size_t fillsamples = fill_samples_buffer_t(pit_time-end, samples_cnt, round(m_s.level));
		PDEBUGF(LOG_V2, LOG_AUDIO, "postgap fill: %d, ", fillsamples);
		samples_cnt += fillsamples;
	}

	m_samples_rem = m_samples_rem + (this_slice_samples - samples_cnt);
	PDEBUGF(LOG_V2, LOG_AUDIO, "samples created: %d", samples_cnt);
	if(_prebuf) {
		m_samples_rem = std::min(0.0, m_samples_rem);
	} else {
		m_samples_rem = std::min(m_samples_rem, this_slice_samples);
		PDEBUGF(LOG_V2, LOG_AUDIO, ", remainder: %.1f", m_samples_rem);
	}
	PDEBUGF(LOG_V2, LOG_AUDIO, "\n");

	if(samples_cnt>0) {
		m_channel->in().add_samples(m_samples_buffer, samples_cnt);
	}
	if(chan_disable) {
		m_s.level = 0.0;
		m_s.velocity = 0.0;
		m_s.samples = 0.0;
		m_channel->set_disable_time(NSEC_TO_USEC(pit_time));
	}

	m_last_time = pit_time;

	m_channel->input_finish();

	return true;
}


