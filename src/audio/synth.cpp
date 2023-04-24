/*
 * Copyright (C) 2016-2023  Marco Bortolin
 *
 * This file is part of IBMulator
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
#include "synth.h"
#include "program.h"
#include "filesys.h"
#include <regex>

Synth::Synth()
:
m_chips{nullptr,nullptr},
m_rate(0),
m_frames_per_ns(0),
m_last_time(0),
m_new_data(true),
m_fr_rem(0.0),
m_synthcmd_fn(nullptr),
m_generate_fn(nullptr),
m_capture_fn(nullptr)
{
}

void Synth::install(std::string _name, uint64_t _chtimeout_ns,
		synthfunc_t _synthcmd,
		genfunc_t   _generate,
		captfunc_t  _capture)
{
	m_name = _name;
	m_synthcmd_fn = _synthcmd;
	m_generate_fn = _generate;
	m_capture_fn = _capture;

	using namespace std::placeholders;
	m_channel = g_mixer.register_channel(
		std::bind(&Synth::create_samples, this, _1, _2, _3),
		m_name, MixerChannel::AUDIOCARD);
	m_channel->set_disable_timeout(_chtimeout_ns);
	if(_capture) {
		m_channel->register_capture_clbk(std::bind(
				&Synth::on_capture, this, _1));
	}
}

void Synth::remove()
{
	g_mixer.unregister_channel(m_channel);
	if(m_chips[0]) {
		m_chips[0]->remove();
	}
	if(m_chips[1]) {
		m_chips[1]->remove();
	}
}

void Synth::reset()
{
	std::lock_guard<std::mutex> lock(m_evt_lock);
	m_channel->enable(false);
	m_events.clear();
	m_fr_rem = 0.0;
	if(m_chips[0]) {
		m_chips[0]->reset();
	}
	if(m_chips[1]) {
		m_chips[1]->reset();
	}
}

void Synth::power_off()
{
	m_channel->enable(false);
}

void Synth::config_changed(const AudioSpec &_spec, float _volume, std::string _filters)
{
	m_channel->set_in_spec(_spec);
	m_frames_per_ns = _spec.rate / 1e9;
	if(m_chips[0]) {
		m_chips[0]->config_changed(_spec.rate);
	}
	if(m_chips[1]) {
		m_chips[1]->config_changed(_spec.rate);
	}
	m_channel->set_volume(_volume);
	m_channel->set_filters(_filters);
}

void Synth::set_chip(int _id, SynthChip *_chip)
{
	assert(_id == 0 || _id == 1);
	m_chips[_id] = _chip;
}

unsigned Synth::generate(AudioBuffer &_outbuffer, uint64_t _delta_ns)
{
	// called by the Mixer thread
	
	double dframes = m_frames_per_ns * _delta_ns;
	int frames = round(dframes + m_fr_rem);
	if(frames > 0) {
		unsigned f = _outbuffer.frames();
		_outbuffer.resize_frames(f + frames);
		m_generate_fn(_outbuffer, f * _outbuffer.spec().channels, frames);
	}
	m_fr_rem += dframes - frames;
	PDEBUGF(LOG_V2, LOG_MIXER, "%s: frames needed:%.1f, generated:%d, rem:%.1f\n",
			m_name.c_str(), dframes, frames, m_fr_rem);
	return frames;
}

bool Synth::create_samples(uint64_t _time_span_ns, bool, bool)
{
	// called by the Mixer thread
	
	// this lock is to prevent a sudden queue clear on reset
	std::lock_guard<std::mutex> lock(m_evt_lock);

	uint64_t mtime_ns = g_machine.get_virt_time_ns_mt();

	Event event, next_event;
	unsigned generated_frames = 0;
	next_event.time = 0;
	bool empty = m_events.empty();
	double needed_frames = double(_time_span_ns) * m_channel->in_spec().rate/1e9;
	
	static AudioBuffer outbuffer;
	outbuffer.set_spec(m_channel->in_spec());
	
	PDEBUGF(LOG_V2, LOG_MIXER, "%s: %d events\n", m_name.c_str(), m_events.size());
	while(next_event.time < mtime_ns) {
		empty = !m_events.try_and_copy(event);
		if(empty || event.time > mtime_ns) {
			if(is_silent() && m_channel->check_disable_time(mtime_ns)) {
				m_last_time = 0;
				PDEBUGF(LOG_V1, LOG_MIXER, "%s: exiting with %d samples without finishing...\n",
						m_name.c_str(), generated_frames);
				return false;
			} else if(m_last_time) {
				generated_frames += generate(outbuffer, mtime_ns - m_last_time);
			}
			break;
		} else if(m_last_time) {
			generated_frames += generate(outbuffer, event.time - m_last_time);
		}
		m_last_time = 0;

		PDEBUGF(LOG_V2, LOG_MIXER, "%s: %02Xh <- %02Xh\n", m_name.c_str(), event.reg, event.value);
		m_synthcmd_fn(event);

		m_events.try_and_pop();
		if(!m_events.try_and_copy(next_event) || next_event.time > mtime_ns) {
			//no more events or the next event is in the future
			next_event.time = mtime_ns;
		}
		if(next_event.time > event.time) {
			generated_frames += generate(outbuffer, next_event.time - event.time);
		}
	}
	m_last_time = mtime_ns;
	int preframes = needed_frames - generated_frames;
	if(m_new_data && preframes > 0) {
		m_channel->in().fill_frames_silence(preframes);
	} else {
		preframes = 0;
	}
	m_new_data = false;
	m_channel->in().add_frames(outbuffer);
	m_channel->input_finish();

	PDEBUGF(LOG_V2, LOG_MIXER, "%s: update: %04llu nsecs, frames needed: %.1f, generated: %d+%d\n",
			m_name.c_str(), _time_span_ns, needed_frames, preframes, generated_frames);

	if(!empty) {
		m_channel->set_disable_time(mtime_ns);
	}
	return true;
}

void Synth::save_state(StateBuf &_state)
{
	std::lock_guard<std::mutex> lock(m_evt_lock);

	if(m_chips[0]) {
		m_chips[0]->save_state(_state);
	}
	if(m_chips[1]) {
		m_chips[1]->save_state(_state);
	}

	typename std::deque<Event>::iterator it;
	size_t evt_count;
	evt_count = m_events.acquire_iterator(it);
	StateHeader h{evt_count * sizeof(Event), "SynthEvents"};
	if(evt_count) {
		std::vector<uint8_t> evts(h.data_size);
		uint8_t *ptr = &evts[0];
		for(size_t i=0; i<evt_count; i++,it++) {
			*((Event*)ptr) = *it;
			ptr += sizeof(Event);
		}
		m_events.release_iterator();
		_state.write(&evts[0], h);
	} else {
		_state.write(nullptr, h);
	}
}

void Synth::restore_state(StateBuf &_state)
{
	StateHeader h;

	std::lock_guard<std::mutex> lock(m_evt_lock);

	m_events.clear();
	m_last_time = 0;

	if(m_chips[0]) {
		m_chips[0]->restore_state(_state);
	}
	if(m_chips[1]) {
		m_chips[1]->restore_state(_state);
	}

	_state.get_next_lump_header(h);
	if(h.name.compare("SynthEvents") != 0) {
		PERRF(LOG_AUDIO, "SynthEvents expected in state buffer, found %s\n",
				h.name.c_str());
		throw std::exception();
	}
	if(h.data_size) {
		if(h.data_size % sizeof(Event)) {
			PERRF(LOG_AUDIO, "SynthEvents size mismatch in state buffer\n");
			throw std::exception();
		}
		int evtcnt = h.data_size / sizeof(Event);
		std::vector<Event> evts(evtcnt);
		_state.read((uint8_t*)&evts[0],h);
		for(int i=0; i<evtcnt; i++) {
			m_events.push(evts[i]);
		}
	} else {
		_state.skip();
	}
	if(has_events() || !is_silent()) {
		p_enable_channel();
	}
}

void Synth::on_capture(bool _start)
{
	if(_start) {
		std::string path = g_program.config().get_file(CAPTURE_SECTION, CAPTURE_DIR, FILE_TYPE_USER);
		std::string name = m_name;
		std::transform(name.begin(), name.end(), name.begin(), ::tolower);
		name = std::regex_replace(name, std::regex("\\s+"), "");
		name = std::regex_replace(name, std::regex("\\W+"), "");
		std::string fname = FileSys::get_next_filename(path, name+"_", ".vgm");
		if(!fname.empty()) {
			m_vgm.open(fname);
			if(m_capture_fn) {
				m_capture_fn(true, m_vgm);
			}
			PINFOF(LOG_V0, LOG_MIXER, "%s: started audio capturing to '%s'\n",
					m_name.c_str(), m_vgm.name());
		} else {
			throw std::exception();
		}
	} else {
		try {
			if(m_capture_fn) {
				m_capture_fn(false, m_vgm);
			}
			m_vgm.close();
		} catch(std::exception &) {}
	}
}

void Synth::enable_channel()
{
	if(!m_channel->is_enabled()) { 
		std::lock_guard<std::mutex> lock(m_evt_lock);
		p_enable_channel();
	}
}

void Synth::p_enable_channel()
{
	m_last_time = 0;
	m_new_data = true;
	m_channel->enable(true);
	PDEBUGF(LOG_V1, LOG_AUDIO, "%s: enabled\n", m_name.c_str());
}

bool Synth::is_silent()
{
	bool silent = true;
	if(m_chips[0]) {
		silent = silent && m_chips[0]->is_silent();
	}
	if(m_chips[1]) {
		silent = silent && m_chips[1]->is_silent();
	}
	return silent;
}
