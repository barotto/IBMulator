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

IODEVICE_PORTS(PCSpeaker) = {};


PCSpeaker::PCSpeaker(Devices *_dev)
: IODevice(_dev),
m_SRC(nullptr),
m_last_time(0),
m_samples_rem(0.0)
{
}

PCSpeaker::~PCSpeaker()
{
#if HAVE_LIBSAMPLERATE
	if(m_SRC != nullptr) {
		src_delete(m_SRC);
	}
#endif
}

void PCSpeaker::install()
{
	using namespace std::placeholders;
	m_channel = g_mixer.register_channel(
		std::bind(&PCSpeaker::create_samples, this, _1, _2, _3),
		name());
	m_channel->set_disable_timeout(2500000);
}

void PCSpeaker::remove()
{
	g_mixer.unregister_channel(m_channel);
}

void PCSpeaker::config_changed()
{
	unsigned rate = g_program.config().get_int(PCSPEAKER_SECTION, PCSPEAKER_RATE);
	m_channel->set_in_spec({AUDIO_FORMAT_F32, 1, rate});
	m_outbuf.set_spec({AUDIO_FORMAT_F32, 1, rate});
	m_outbuf.reserve_us(50000);
#if HAVE_LIBSAMPLERATE
	if(m_SRC == nullptr) {
		int err;
		m_SRC = src_new(SRC_SINC_FASTEST, 1, &err);
		if(m_SRC == nullptr) {
			throw std::runtime_error(src_strerror(err));
		}
		m_pitbuf.set_spec({AUDIO_FORMAT_F32, 1, PIT_FREQ});
		m_pitbuf.reserve_us(50000);
	}
#endif
	float volume = clamp(g_program.config().get_real(PCSPEAKER_SECTION, PCSPEAKER_VOLUME),
			0.0, 10.0);
	m_channel->set_volume(volume);
	reset(0);
}

void PCSpeaker::reset(uint)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_channel->enable(false);
	m_events.clear();
	m_s.level = 0.0;
}

void PCSpeaker::power_off()
{
	reset(0);
}

void PCSpeaker::save_state(StateBuf &_state)
{
	PINFOF(LOG_V1, LOG_AUDIO, "%s: saving state\n", name());

	std::lock_guard<std::mutex> lock(m_mutex);

	_state.write(&m_s, {sizeof(m_s), name()});

	StateHeader h;
	h.data_size = m_events.size() * sizeof(SpeakerEvent);
	h.name = std::string(name()) + "-Events";
	if(h.data_size) {
		std::vector<uint8_t> evts(h.data_size);
		uint8_t *ptr = &evts[0];
		auto it = m_events.begin();
		for(size_t i=0; i<m_events.size(); i++,it++) {
			*((SpeakerEvent*)ptr) = *it;
			ptr += sizeof(SpeakerEvent);
		}
		_state.write(&evts[0], h);
	} else {
		_state.write(nullptr, h);
	}
}

void PCSpeaker::restore_state(StateBuf &_state)
{
	PINFOF(LOG_V1, LOG_AUDIO, "%s: restoring state\n", name());

	std::lock_guard<std::mutex> lock(m_mutex);

	m_channel->enable(false);
	_state.read(&m_s, {sizeof(m_s), name()});

	// events
	m_events.clear();
	std::string evtname = std::string(name()) + "-Events";
	StateHeader h;
	_state.get_next_lump_header(h);
	if(h.name.compare(evtname) != 0) {
		PERRF(LOG_AUDIO, "%s expected in state buffer, found %s\n",
				evtname.c_str(), h.name.c_str());
		throw std::exception();
	}
	if(h.data_size) {
		if(h.data_size % sizeof(SpeakerEvent)) {
			PERRF(LOG_AUDIO, "%s size mismatch in state buffer\n", evtname.c_str());
			throw std::exception();
		}
		int evtcnt = h.data_size / sizeof(SpeakerEvent);
		std::vector<SpeakerEvent> evts(evtcnt);
		_state.read((uint8_t*)&evts[0],h);
		for(int i=0; i<evtcnt; i++) {
			m_events.push_back(evts[i]);
		}
		activate();
	} else {
		_state.skip();
	}
}

void PCSpeaker::activate()
{
	//TODO maybe implement a version w/o libsaplerate?
#if HAVE_LIBSAMPLERATE
	if(!m_channel->is_enabled()) {
		m_last_time = 0;
		m_samples_rem = 0.0;
		src_reset(m_SRC);
		m_channel->enable(true);
	}
#endif
}

void PCSpeaker::add_event(uint64_t _ticks, bool _active, bool _out)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	static uint64_t last_ticks = 0;
	uint64_t elapsed = _ticks - last_ticks;
	PDEBUGF(LOG_V2, LOG_AUDIO, "PC speaker: evt: %07llu CLK, %s, %s\n",
			elapsed, (_active?" act":"!act"), (_out?"5v":"0v"));
	last_ticks = _ticks;

#if HAVE_LIBSAMPLERATE
	if(m_events.size()) {
		SpeakerEvent &evt = m_events.back();
		assert(_ticks >= evt.ticks);
		if(evt.ticks == _ticks) {
			evt.out = _out;
			evt.active = _active;
			return;
		}
	}
	m_events.push_back({_ticks, _active, _out});
#endif
}

// this function is called by the Mixer thread
bool PCSpeaker::create_samples(uint64_t _time_span_us, bool _prebuf, bool /*_first_upd*/)
{
	m_mutex.lock();

	uint64_t pit_ticks = g_devices.pit()->get_pit_ticks_mt();

	double needed_frames = double(_time_span_us) * double(m_outbuf.rate())/1e6;
	size_t size = m_events.size();

	PDEBUGF(LOG_V2, LOG_AUDIO, "PC speaker: mix time: %04d usecs, samples: %.1f, evnts: %d, ",
			_time_span_us, needed_frames, size);

	if(size==0 || m_events[0].ticks > pit_ticks) {
		m_mutex.unlock();
		unsigned samples = unsigned(std::max(0, int(needed_frames + m_samples_rem)));
		if(m_channel->check_disable_time(NSEC_TO_USEC(pit_ticks*PIT_CLK_TIME))) {
			m_last_time = 0;
			PDEBUGF(LOG_V2, LOG_AUDIO, "\n");
			return false;
		} else if(m_last_time && samples && !_prebuf) {
			PDEBUGF(LOG_V2, LOG_AUDIO, "silence fill: %u samples\n", samples);
			m_channel->in().fill_samples<float>(samples, 0.0);
		}
		m_last_time = pit_ticks;
		m_samples_rem += needed_frames - samples;
		if(_prebuf) {
			m_samples_rem = std::min(0.0, m_samples_rem);
		}
		m_channel->input_finish();
		return true;
	}

	m_pitbuf.clear();
	m_outbuf.clear();

	m_channel->set_disable_time(0);
	uint32_t samples = 0;

	if(m_last_time && (m_events[0].ticks>m_last_time)) {
		//fill the gap
		samples = m_events[0].ticks - m_last_time;
		m_pitbuf.fill_samples<float>(samples, m_s.level);
		PDEBUGF(LOG_V2, LOG_AUDIO, "pregap fill: %d, ", samples);
	}

	uint64_t evnts_begin = m_events[0].ticks;
	uint64_t end = pit_ticks;
	for(size_t i=0; i<size; i++) {
		SpeakerEvent front = m_events[0];
		uint64_t begin = front.ticks;
		if(begin > pit_ticks) {
			// an event is in the future when the lock is acquired after a new
			// event and before the pit time is updated
			break;
		}
		if(i<size-1) {
			end = m_events[1].ticks;
			m_events.pop_front();
		} else {
			//this is the last event
			// if the speaker is active continue
			if(front.active) {
				m_events[0].ticks = pit_ticks;
			} else {
				//the last event is a shutdown
				m_events.pop_front();
			}
			end = pit_ticks;
		}

		m_s.level = (front.out)?1.0:0.0;
		m_pitbuf.fill_samples<float>(end - begin, m_s.level);

		if(end == pit_ticks) {
			break;
		}
	}

	bool chan_disable = m_events.empty();
	m_mutex.unlock();

	PDEBUGF(LOG_V2, LOG_AUDIO, "evnts len: %llu nsec, PIT ticks: %d, ",
			(end - evnts_begin), m_pitbuf.frames());

	if(end < pit_ticks) {
		//fill the gap
		samples = m_events[0].ticks - m_last_time;
		m_pitbuf.fill_samples<float>(samples, m_s.level);
		PDEBUGF(LOG_V2, LOG_AUDIO, "postgap fill: %d, ", samples);
	}

	// rate conversion from 1.193MHz to current speaker rate
	m_pitbuf.convert_rate(m_outbuf, m_pitbuf.frames(), m_SRC);

	m_samples_rem = m_samples_rem + (needed_frames - m_outbuf.frames());
	PDEBUGF(LOG_V2, LOG_AUDIO, "audio samples: %d", m_outbuf.frames());
	if(_prebuf) {
		m_samples_rem = std::min(0.0, m_samples_rem);
	} else {
		m_samples_rem = std::min(m_samples_rem, needed_frames);
		PDEBUGF(LOG_V2, LOG_AUDIO, ", remainder: %.1f", m_samples_rem);
	}
	PDEBUGF(LOG_V2, LOG_AUDIO, "\n");

	m_channel->in().add_frames(m_outbuf);

	if(chan_disable) {
		m_s.level = 0.0;
		m_channel->set_disable_time(NSEC_TO_USEC(pit_ticks*PIT_CLK_TIME));
	}

	m_last_time = pit_ticks;
	m_channel->input_finish();
	return true;
}

