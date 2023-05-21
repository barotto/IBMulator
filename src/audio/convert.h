/*
 * Copyright (C) 2020-2023  Marco Bortolin
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

#ifndef IBMULATOR_AUDIOCONVERT_H
#define IBMULATOR_AUDIOCONVERT_H

// let's use namespaces for once, shall we?
namespace Audio
{
namespace Convert
{

	template<typename T>
	unsigned resample_mono(
			const T *_in, unsigned _in_samples, double _in_rate,
			      T *_out, unsigned _out_size, double _out_rate);
	
	template<typename T>
	unsigned resample_mono(
			const T *_in, unsigned _in_samples,
			      T *_out, unsigned _out_size,
			      double _ratio);

	template<typename T>
	unsigned resample_stereo(
			const T *_in, unsigned _in_frames, double _in_rate,
			      T *_out, unsigned _out_size, double _out_rate);
	
	template<typename T>
	unsigned resample_stereo(
			const T *_in, unsigned _in_frames,
			      T *_out, unsigned _out_size,
			      double _ratio);

	
}
}

#endif