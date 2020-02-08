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

#ifndef IBMULATOR_CAPTURE_IMGSEQ_H
#define IBMULATOR_CAPTURE_IMGSEQ_H

#include "capture_target.h"
#include <SDL.h>

class CaptureImgSeq : public CaptureTarget
{
	CaptureFormat m_format;
	std::string m_dir;
	SDL_Surface *m_surface;
	VideoModeInfo m_cur_mode;
	int m_framecnt;
	
	void free_surface();
	
public:
	CaptureImgSeq(CaptureFormat _img_format);
	virtual ~CaptureImgSeq();
	
	virtual std::string open(std::string _dir_path);
	virtual void close();
	
	virtual void push_video_frame(const FrameBuffer &_fb, const VideoModeInfo &_mode);
};

#endif