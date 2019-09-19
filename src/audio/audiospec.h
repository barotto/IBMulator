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

#ifndef IBMULATOR_AUDIOSPEC_H
#define IBMULATOR_AUDIOSPEC_H

#include <SDL_audio.h>
#include <cmath>

//direct mapping to SDL2 defines
//you can therefore use SDL2 format macros
enum AudioFormat
{
	AUDIO_FORMAT_U8 = AUDIO_U8,
	AUDIO_FORMAT_S16 = AUDIO_S16,
	AUDIO_FORMAT_F32 = AUDIO_F32
};

inline double us_to_frames(uint64_t _us, unsigned _rate) {
	return double(_us) * double(_rate)/1e6;
}

inline double us_to_samples(uint64_t _us, unsigned _rate, unsigned _ch) {
	return us_to_frames(_us,_rate) * _ch;
}

inline double frames_to_us(unsigned _frames, unsigned _rate) {
	return (double(_frames) / double(_rate)) * 1e6;
}

struct AudioSpec
{
	AudioFormat format;
	unsigned channels;
	unsigned rate;      //frames per second

	bool operator==(const AudioSpec &_s) const {
		return (format==_s.format) && (channels==_s.channels) && (rate==_s.rate);
	}
	bool operator!=(const AudioSpec &_s) const {
		return !(*this==_s);
	}
	double us_to_frames(uint64_t _us) const {
		return ::us_to_frames(_us, rate);
	}
	double us_to_samples(uint64_t _us) const {
		return ::us_to_samples(_us, rate, channels);
	}
	uint64_t frames_to_us(unsigned _frames) const {
		return round(::frames_to_us(_frames, rate));
	}
	unsigned frames_to_samples(unsigned _frames) const {
		return _frames*channels;
	}
	unsigned samples_to_frames(unsigned _samples) const {
		return _samples/channels;
	}
	std::string to_string() const;
};

#endif
