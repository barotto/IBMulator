/*
 * Copyright (C) 2015-2020  Marco Bortolin
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
		futures[i] = std::async(std::launch::async, [_spec,i,&buffers,&_samples]() {
			PINFOF(LOG_V2, LOG_AUDIO, "loading %s for %s sound fx\n",
					_samples[i].file, _samples[i].name);
			load_audio_file(_samples[i].file, buffers[i], _spec);
		});
	}
	for(unsigned i=0; i<_samples.size(); ++i) {
		futures[i].wait();
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
	}
}

bool SoundFX::play_motor(uint64_t _time_span_ns, MixerChannel &_channel,
		bool _is_on, bool _is_changing_state,
		const AudioBuffer &_power_up, const AudioBuffer &_running,
		const AudioBuffer &_power_down)
{
	if(_is_on) {
		if(_is_changing_state) {
			PDEBUGF(LOG_V1, LOG_AUDIO, "%s: power up\n", _channel.name());
			_channel.flush();
			_channel.play(_power_up,0);
		}
		_channel.play_loop(_running);
		_channel.input_finish(_time_span_ns);
		return true;
	} else {
		if(_is_changing_state) {
			PDEBUGF(LOG_V1, LOG_AUDIO, "%s: power down\n", _channel.name());
			_channel.flush();
			_channel.play(_power_down,0);
		}
		_channel.input_finish(_time_span_ns);
		_channel.enable(false);
		return false;
	}
}
