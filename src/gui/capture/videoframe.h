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

#ifndef IBMULATOR_VIDEOFRAME_H
#define IBMULATOR_VIDEOFRAME_H

struct VideoFrame
{
	FrameBuffer buffer;
	VideoModeInfo mode;
	VideoTimings timings;
	
	VideoFrame() {}
	VideoFrame(const FrameBuffer &_b, const VideoModeInfo &_m, const VideoTimings &_t):
		buffer(_b), mode(_m), timings(_t){}
	VideoFrame(VideoFrame &&_vf):
		buffer(std::move(_vf.buffer)),
		mode(_vf.mode),
		timings(_vf.timings) {}
	VideoFrame(const VideoFrame &) = delete;
	~VideoFrame() {}
	
	VideoFrame & operator=(const VideoFrame &) = delete;
	VideoFrame & operator=(VideoFrame &&_vf) {
		buffer = std::move(_vf.buffer);
		mode = _vf.mode;
		timings = _vf.timings;
		return *this;
	}
};

#endif