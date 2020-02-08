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
#include "filesys.h"
#include "capture.h"
#include "capture_imgseq.h"
#include <SDL_image.h>


CaptureImgSeq::CaptureImgSeq(CaptureFormat _img_format)
:
CaptureTarget(),
m_format(_img_format),
m_surface(nullptr),
m_framecnt(0)
{
}

CaptureImgSeq::~CaptureImgSeq()
{
	close();
}

std::string CaptureImgSeq::open(std::string _dir_path)
{
	if(!m_dir.empty()) {
		PDEBUGF(LOG_V0, LOG_PROGRAM, "Close this target first.\n");
		throw std::exception();
	}
	
	m_dir = FileSys::get_next_dirname(_dir_path, "video_");
	if(m_dir.empty()) {
		PERRF(LOG_PROGRAM, "Error creating screen recording directory.\n");
		throw std::exception();
	}
	
	try {
		FileSys::create_dir(m_dir.c_str());
	} catch(std::exception &e) {
		PERRF(LOG_PROGRAM, "Error creating screen recording directory '%s'.\n", m_dir.c_str());
		m_dir = "";
		throw;
	}
	
	return m_dir;
}

void CaptureImgSeq::close()
{
	m_dir = "";
	m_framecnt = 0;
	free_surface();
}

void CaptureImgSeq::free_surface()
{
	if(m_surface) {
		SDL_FreeSurface(m_surface);
		m_surface = nullptr;
	}
}

void CaptureImgSeq::push_video_frame(const FrameBuffer &_fb, const VideoModeInfo &_mode)
{
	if(m_dir == "") {
		PDEBUGF(LOG_V0, LOG_PROGRAM, "This target is not open!\n");
		throw std::exception();
	}
	
	if(!(_mode == m_cur_mode) || !m_surface) {
		free_surface();
		m_surface = SDL_CreateRGBSurface(
			0,
			_mode.xres,
			_mode.yres,
			32,
			PALETTE_RMASK,
			PALETTE_GMASK,
			PALETTE_BMASK,
			PALETTE_AMASK
		);
		if(!m_surface) {
			PERRF(LOG_PROGRAM, "Error creating screen recording surface\n");
			throw std::exception();
		}
		m_cur_mode = _mode;
	}
	
	SDL_LockSurface(m_surface);
	_fb.copy_screen_to((uint8_t*)m_surface->pixels, _mode);
	SDL_UnlockSurface(m_surface);

	std::stringstream imgfile;
	imgfile << m_dir << FS_SEP << "frame_";
	imgfile.width(4);
	imgfile.fill('0');
	imgfile << m_framecnt;
	
	int result = 0;
	switch(m_format) {
		case CaptureFormat::PNG:
			imgfile << ".png";
			result = IMG_SavePNG(m_surface, imgfile.str().c_str());
			break;
		default:
			PDEBUGF(LOG_V0, LOG_PROGRAM, "Invalid recording format!\n");
			throw std::exception();
	}

	if(result < 0) {
		PERRF(LOG_PROGRAM, "Error saving frame to image file.\n");
		throw std::exception();
	}
	
	m_framecnt++;
}