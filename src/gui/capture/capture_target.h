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

#ifndef IBMULATOR_CAPTURE_TARGET_H
#define IBMULATOR_CAPTURE_TARGET_H

#include "hardware/devices/vga.h"

class CaptureTarget
{
protected:
	CaptureTarget() {}
public:
	virtual ~CaptureTarget() {}
	
	virtual std::string open(std::string _dir_path) = 0;
	virtual void close() {}
	
	virtual void push_video_frame(const FrameBuffer &, const VideoModeInfo &) {}
	virtual void push_audio_frame(uint8_t *) {}
};

#endif