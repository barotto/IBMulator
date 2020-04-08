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

#ifndef IBMULATOR_CAPTURE_VIDEOFILE_H
#define IBMULATOR_CAPTURE_VIDEOFILE_H

#include "avi.h"
#include "capture_target.h"

class CaptureVideoFile : public CaptureTarget
{
	std::string m_file_path;
	std::string m_dir_path;
	AVIWriteOptions m_avi_options;
	AVIFile m_avi;
	VideoModeInfo m_cur_mode;
	VideoTimings m_cur_timings;
	
public:

	CaptureVideoFile(
		unsigned _video_encoder, unsigned _video_quality,
		unsigned _audio_encoder, unsigned _audio_quality,
		unsigned _audio_ch, unsigned _audio_freq);
	virtual ~CaptureVideoFile();
	
	virtual std::string open(std::string _dir_path);
	virtual void close();
	virtual bool has_audio() const { return true; }
	
	void push_video_frame(const VideoFrame &_vf);
	void push_audio_data(const int16_t *_samples, uint32_t _count);
	
private:

	void open_AVI(const VideoFrame &_vf);
};

#endif