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

#include "ibmulator.h"
#include "filesys.h"
#include "capture.h"
#include "capture_videofile.h"


CaptureVideoFile::CaptureVideoFile(
	unsigned _video_encoder, unsigned _video_quality,
	unsigned _audio_encoder, unsigned _audio_quality,
	unsigned _audio_ch, unsigned _audio_freq)
:
CaptureTarget()
{
	m_avi_options.video_encoder = _video_encoder;
	m_avi_options.video_quality = _video_quality;
	m_avi_options.audio_encoder = _audio_encoder;
	m_avi_options.audio_quality = _audio_quality;
	m_avi_options.audio_channels = _audio_ch;
	m_avi_options.audio_freq = _audio_freq;
	
	m_avi_options.video_srcpixels = SDL_AllocFormat(PALETTE_SDL2_FORMAT);
	if(!m_avi_options.video_srcpixels) {
		PERRF(LOG_GUI, "Error creating screen recording target\n");
		throw std::exception();
	}

	// these values are set in open_AVI()
	m_avi_options.video_width = 0;
	m_avi_options.video_height = 0;
	m_avi_options.video_rate = 0;
	m_avi_options.video_scale = 0;

	PDEBUGF(LOG_V1, LOG_GUI, "Recording to AVI file\n");
}

CaptureVideoFile::~CaptureVideoFile()
{
	CaptureVideoFile::close();
	if(m_avi_options.video_srcpixels) {
		SDL_FreeFormat(m_avi_options.video_srcpixels);
		m_avi_options.video_srcpixels = nullptr;
	}
}

std::string CaptureVideoFile::open(std::string _dir_path)
{
	if(!m_file_path.empty()) {
		PDEBUGF(LOG_V0, LOG_GUI, "Close this target first!\n");
		throw std::exception();
	}
	if(!FileSys::is_directory(_dir_path.c_str())) {
		PERRF(LOG_GUI, "The destination directory does not exits.\n");
		throw std::exception();
	}
	m_dir_path = _dir_path;
	m_file_path = FileSys::get_next_filename(m_dir_path, "video_", ".avi");
	if(m_file_path.empty()) {
		PERRF(LOG_GUI, "Error creating screen recording target file.\n");
		throw std::exception();
	}
	
	return m_file_path;
}

void CaptureVideoFile::open_AVI(const VideoFrame &_vf)
{
	m_avi_options.video_width = _vf.mode.xres;
	m_avi_options.video_height = _vf.mode.yres;
	
	m_avi_options.video_rate = uint32_t(_vf.timings.clock / _vf.timings.cwidth);
	m_avi_options.video_scale = _vf.timings.htotal * _vf.timings.vtotal;

	try {
		m_avi.open_write(m_file_path.c_str(), m_avi_options);
		PDEBUGF(LOG_V1, LOG_GUI, "Opened AVI video file %s\n", m_file_path.c_str());
	} catch(std::logic_error &e) {
		PERRF(LOG_GUI, "Error creating screen recording file '%s': %s\n", m_file_path.c_str(), e.what());
		// close the file now so that we won't try to finalize the AVI later
		m_avi.close_file();
		close();
		throw;
	}
	
	PINFOF(LOG_V0, LOG_GUI, "Video: %dx%d, %.02f Hz, %s, %s\n",
			_vf.mode.xres, _vf.mode.yres, _vf.timings.vfreq,
			m_avi.video_encoder()->name(), m_avi.video_encoder()->format_string().c_str()
			);
	PINFOF(LOG_V0, LOG_GUI, "Audio: 16-bit, %d ch., %d Hz, Uncompressed PCM\n",
			m_avi_options.audio_channels, m_avi_options.audio_freq
			);

	m_cur_mode = _vf.mode;
	m_cur_timings = _vf.timings;
}

void CaptureVideoFile::close()
{
	if(m_avi.is_open()) {
		uint32_t frames = m_avi.video_frames_count();
		const double fps = double(m_avi_options.video_rate) / m_avi_options.video_scale;
		uint32_t seconds = double(frames) / fps;
		uint32_t minutes = seconds / 60;
		uint32_t hours = minutes / 60;
		uint32_t rframes = frames - seconds*fps;
		seconds %= 60;
		PINFOF(LOG_V0, LOG_GUI, "Recorded %d frames, duration: %02d:%02d:%02d.%d\n",
				frames, hours, minutes, seconds, rframes);
		PINFOF(LOG_V1, LOG_GUI, "AVI file size: %d bytes\n", m_avi.file_size());
		try {
			m_avi.close();
		} catch(std::runtime_error &e) {
			PERRF(LOG_GUI, "Error writing to file: %s\n", e.what());
			m_avi.close_file();
		}
	}
	m_file_path = "";
}

void CaptureVideoFile::push_video_frame(const VideoFrame &_vf)
{
	if(m_file_path == "") {
		PDEBUGF(LOG_V0, LOG_GUI, "This target is not open!\n");
		throw std::exception();
	}
	if(!m_avi.is_open_write()) {
		// the first frame sets the avi file video properties
		open_AVI(_vf); // can throw
	} else {
		bool mode_changed = (
			_vf.timings.vfreq != m_cur_timings.vfreq || // this float comparison is ok
			_vf.mode.xres != m_cur_mode.xres ||
			_vf.mode.yres != m_cur_mode.yres
		);
		bool size_limit = m_avi.write_size_limit_reached();
		if(mode_changed || size_limit) {
			if(mode_changed) {
				PINFOF(LOG_V1, LOG_GUI, "Video mode changed, closing video file\n");
			} else {
				PINFOF(LOG_V1, LOG_GUI, "File size limit reached, closing video file\n");
			}
			close();
			open(m_dir_path);
			PDEBUGF(LOG_V0, LOG_GUI, "Opening new video file\n");
			open_AVI(_vf); // can throw
		}
	}
	
	try {
		m_avi.write_video_frame(&_vf.buffer[0], _vf.buffer.pitch());
	} catch(std::logic_error &e) {
		PERRF(LOG_GUI, "Error during screen recording: %s\n", e.what());
		throw;
	} catch(...) {
		PERRF(LOG_GUI, "Unknown error during screen recording.\n");
		throw;
	}
}

void CaptureVideoFile::push_audio_data(const int16_t *_samples, uint32_t _count)
{
	assert(m_avi.is_open_write());
	
	try {
		m_avi.write_audio_samples(_samples, _count);
	} catch(std::logic_error &e) {
		PERRF(LOG_GUI, "Error during screen recording: %s\n", e.what());
		throw;
	} catch(...) {
		PERRF(LOG_GUI, "Unknown error during screen recording.\n");
		throw;
	}
}