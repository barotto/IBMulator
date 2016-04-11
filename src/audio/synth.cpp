/*
 * Copyright (C) 2016  Marco Bortolin
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

void Synth::reset()
{
	std::lock_guard<std::mutex> lock(m_evt_lock);
	m_events.clear();
	m_buffer.clear();
	m_frem = 0.0;
}

void Synth::set_audio_spec(const AudioSpec &_spec)
{
	m_buffer.set_spec(_spec);
	m_frames_per_ns = double(_spec.rate) / 1e9;
}

int Synth::generate(double _frames, std::function<void(AudioBuffer&,int)> _generate)
{
	double frames = _frames + m_frem;
	int iframes = frames;
	if(iframes > 0) {
		m_buffer.resize_frames(iframes);
		_generate(m_buffer, iframes);
	}
	m_frem = frames - iframes;
	return iframes;
}

std::pair<bool,int> Synth::play_events(
	uint64_t _mtime_ns, uint64_t _time_span_us, bool _prebuf,
	std::function<void(Event&)> _synthcmd,
	std::function<void(AudioBuffer&,int)> _generate)
{
	//this lock is to prevent a sudden queue clear on reset
	std::lock_guard<std::mutex> lock(m_evt_lock);

	Event event, next_event;
	uint64_t evt_dist_ns;
	int generated_frames = 0;
	next_event.time = 0;
	bool empty = m_events.empty();

	PDEBUGF(LOG_V2, LOG_AUDIO, "Synth: %d events\n", m_events.size());
	while(next_event.time < _mtime_ns) {
		empty = !m_events.try_and_copy(event);
		if(empty || event.time > _mtime_ns) {
			if(m_last_time) {
				evt_dist_ns = _mtime_ns - m_last_time;
			} else {
				evt_dist_ns = _time_span_us * 1000 * (!_prebuf);
			}
			generated_frames += generate(m_frames_per_ns*evt_dist_ns, _generate);
			break;
		} else if(m_last_time) {
			evt_dist_ns = event.time - m_last_time;
			generated_frames += generate(m_frames_per_ns*evt_dist_ns, _generate);
		}
		m_last_time = 0;

		PDEBUGF(LOG_V2, LOG_AUDIO, "Synth: %02Xh <- %02Xh\n", event.reg, event.value);
		_synthcmd(event);

		m_events.try_and_pop();
		if(!m_events.try_and_copy(next_event) || next_event.time > _mtime_ns) {
			//no more events or the next event is in the future
			next_event.time = _mtime_ns;
		}
		evt_dist_ns = next_event.time - event.time;
		generated_frames += generate(m_frames_per_ns*evt_dist_ns, _generate);
	}
	m_last_time = _mtime_ns;

	return std::pair<bool,int>(empty, generated_frames);
}

void Synth::save_state(StateBuf &_state)
{
	std::lock_guard<std::mutex> lock(m_evt_lock);

	StateHeader h{m_events.size() * sizeof(Event), "SynthEvents"};
	if(h.data_size) {
		typename std::deque<Event>::iterator it;
		m_events.acquire_iterator(it);
		std::vector<uint8_t> evts(h.data_size);
		uint8_t *ptr = &evts[0];
		for(size_t i=0; i<m_events.size(); i++,it++) {
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
}

void Synth::start_capture(const std::string &_name)
{
	std::string path = g_program.config().get_file(PROGRAM_SECTION, PROGRAM_CAPTURE_DIR, FILE_TYPE_USER);
	std::string fname = FileSys::get_next_filename(path, _name+"_", ".vgm");
	if(!fname.empty()) {
		m_vgm.open(fname);
	} else {
		throw std::exception();
	}
}

void Synth::stop_capture()
{
	try {
		m_vgm.close();
	} catch(std::exception &) {}
}

