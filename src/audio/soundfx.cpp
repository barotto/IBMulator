/*
 * Copyright (C) 2015-2024  Marco Bortolin
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
#include "soundfx.h"
#include "program.h"
#include <future>


std::vector<AudioBuffer> SoundFX::load_samples(const AudioSpec &_spec, const samples_t &_samples)
{
	std::vector<std::future<void>> futures(_samples.size());
	std::vector<AudioBuffer> buffers(_samples.size());
	for(unsigned i=0; i<_samples.size(); ++i) {
		if(!_samples[i].file.empty()) {
			futures[i] = std::async(std::launch::async, [_spec,i,&buffers,&_samples]() {
				PINFOF(LOG_V2, LOG_AUDIO, "loading %s for %s sound fx\n",
						_samples[i].file.c_str(), _samples[i].name.c_str());
				load_audio_file(_samples[i].file.c_str(), buffers[i], _spec);
			});
		}
	}
	for(unsigned i=0; i<_samples.size(); ++i) {
		if(futures[i].valid()) {
			futures[i].wait();
		}
	}
	return buffers;
}

void SoundFX::load_audio_file(const char *_filename, AudioBuffer &_sample, const AudioSpec &_spec)
{
	try {
		std::string path = g_program.config().get_file_path(_filename, FILE_TYPE_ASSET);
		WAVFile wav;
		wav.open_read(path.c_str());
		_sample.load(wav);
		if(_spec != _sample.spec()) {
			PDEBUGF(LOG_V1, LOG_AUDIO, "converting from %s to %s\n",
					_sample.spec().to_string().c_str(),
					_spec.to_string().c_str());
			_sample.convert(_spec);
		}
	} catch(std::exception &e) {
		PERRF(LOG_AUDIO, "SoundFX: %s: %s\n", _filename, e.what());
		_sample.clear();
	}
}

bool SoundFX::play_motor(uint64_t _time_span_ns, MixerChannel &_channel,
		bool _is_on, bool _is_changing_state,
		const AudioBuffer &_power_up, const AudioBuffer &_running,
		const AudioBuffer &_power_down, bool _symmetric)
{
	// Mixer thread
	if(_is_on) {
		if(_is_changing_state) {
			PDEBUGF(LOG_V1, LOG_AUDIO, "%s: power up\n", _channel.name());
			_channel.flush();
			_channel.play(_power_up);
			m_spinup_time_us = g_mixer.elapsed_time_us();
		} else {
			_channel.play_loop(_running);
		}
		_channel.input_finish(_time_span_ns);
		return true;
	} else {
		if(_is_changing_state) {
			PDEBUGF(LOG_V1, LOG_AUDIO, "%s: power down\n", _channel.name());
			uint64_t offset_us = 0;
			if(_symmetric) {
				uint64_t current_time_us = g_mixer.elapsed_time_us();
				uint64_t spinup_elapsed_us = current_time_us - m_spinup_time_us;
				if(spinup_elapsed_us < _power_down.duration_us()) {
					offset_us = _power_down.duration_us() - spinup_elapsed_us;
				}
			}
			_channel.flush();
			_channel.play_from_offset_us(_power_down, offset_us, 0);
			_channel.play_silence_us(EFFECTS_MIN_DUR_US);
		}
		_channel.input_finish(_time_span_ns);
		_channel.enable(false);
		return false;
	}
}
