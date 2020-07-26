/*
 * Copyright (C) 2020  Marco Bortolin
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
#include "convert.h"

namespace Audio
{
namespace Convert
{


template <typename T>
unsigned resample_mono(
		const T *_in, unsigned _in_samples, double _in_rate,
		      T *_out, unsigned _out_size, double _out_rate)
{
	// _out_size is the capacity of _out in number of elements (samples)
	double rate_ratio = _in_rate / _out_rate;
	unsigned out_samples = unsigned(double(_in_samples) * rate_ratio) + 1;
	out_samples = std::min(out_samples, _out_size);
	
	double src_sample = 0;
	unsigned dst_sample = 0;
	
	while(out_samples--) {
		if(unsigned(src_sample) >= _in_samples) {
			// shouldn't happen!
			break;
		}
		
		_out[dst_sample] = _in[unsigned(src_sample)];

		dst_sample += 1;
		src_sample += 1.0 / rate_ratio;
	}
	
	return dst_sample;
}

template <typename T>
unsigned resample_stereo(
		const T *_in, unsigned _in_frames, double _in_rate,
		      T *_out, unsigned _out_size, double _out_rate)
{
	// TODO FIXME not tested!
	
	// _out_size is the capacity of _out in number of elements (samples)
	double rate_ratio = _in_rate / _out_rate;
	unsigned out_frames = unsigned(double(_in_frames) * rate_ratio) + 1;
	_out_size &= ~1; // it's stereo, so force the value even
	out_frames = std::min(out_frames, _out_size/2);
	
	double src_frame = 0;
	unsigned dst_sample = 0;
	
	while(out_frames--) {
		if(unsigned(src_frame) >= _in_frames) {
			// shouldn't happen!
			break;
		}
		
		_out[dst_sample]     = _in[unsigned(src_frame)*2];
		_out[dst_sample + 1] = _in[unsigned(src_frame)*2 + 1];

		dst_sample += 2;
		src_frame  += 1.0 / rate_ratio;
	}
	
	// return the generated samples, not frames
	return dst_sample;
}

template
unsigned resample_mono<uint8_t>(
		const uint8_t *_in, unsigned _in_samples, double _in_rate,
		      uint8_t *_out, unsigned _out_size, double _out_rate);


}
}