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
#include "program.h"
#include "filesys.h"
#include "capture.h"
#include "capture_imgseq.h"
#include "capture_videofile.h"
#include "gui/gui.h"

Capture::Capture(VGADisplay *_vgadisp, Mixer *_mixer)
:
m_quit(false),
m_recording(false),
m_vga_display(_vgadisp),
m_video_sink(-1),
m_mixer(_mixer),
m_audio_sink(-1)
{
}

Capture::~Capture()
{
}

void Capture::thread_start()
{
	PDEBUGF(LOG_V0, LOG_GUI, "Capture: thread started\n");
	
	while(true) {
		PDEBUGF(LOG_V1, LOG_GUI, "Capture: waiting for commands\n");
		Capture_fun_t fn;
		m_cmd_queue.wait_and_pop(fn);
		fn();
		if(m_recording) {
			capture_loop();
		}
		if(m_quit) {
			return;
		}
	}
	
	PDEBUGF(LOG_V0, LOG_GUI, "Capture: thread stopped\n");
}

void Capture::capture_loop()
{
	PDEBUGF(LOG_V1, LOG_GUI, "Capture: running\n");
	while(true) {
		Capture_fun_t fn;
		while(m_cmd_queue.try_and_pop(fn)) {
			fn();
		}
		if(!m_recording) {
			return;
		}
		try {
			VideoFrame frame;
			// This thread's frequency will be auto capped to the vga fps.
			// When the machine is paused this 'wait' will timeout within 2 frames time;
			auto result = m_video_frames.wait_for_and_pop(frame, g_machine.get_heartbeat() * 2);
			if(result == std::cv_status::no_timeout) {
				m_rec_target->push_video_frame(frame);
				
				size_t avail = m_audio_buffer.get_read_avail();
				if(avail) {
					std::vector<uint8_t> audio_stream;
					audio_stream.resize(avail);
					size_t bytes = m_audio_buffer.read(&audio_stream[0], avail);
					assert(bytes <= avail);
					assert((bytes&1) == 0);
					m_rec_target->push_audio_data((int16_t*)(&audio_stream[0]), bytes/2);
				}
			}
		} catch(std::exception &) {
			stop_capture();
			return;
		}
	}
}

void Capture::cmd_quit()
{
	m_cmd_queue.push([this] () {
		m_quit = true;
		if(m_recording) {
			stop_capture();
		}
	});
}

void Capture::cmd_start_capture()
{
	m_cmd_queue.push([this] () {
		if(m_recording) {
			stop_capture();
		}
		try {
			start_capture();
		} catch(...) {
			return;
		}
	});
}

void Capture::cmd_stop_capture()
{
	m_cmd_queue.push([this] () {
		stop_capture();
	});
}

void Capture::cmd_toggle_capture()
{
	m_cmd_queue.push([=] () {
		if(m_recording) {
			stop_capture();
		} else {
			try {
				start_capture();
			} catch(...) {
				return;
			}
		}
	});
}

void Capture::sig_config_changed(std::mutex &_mutex, std::condition_variable &_cv)
{
	m_cmd_queue.push([&] () {
		//PDEBUGF(LOG_V2, LOG_GUI, "Capture: updating configuration\n");
		std::unique_lock<std::mutex> lock(_mutex);
		if(m_recording) {
			stop_capture();
			try {
				start_capture();
			} catch(...) {}
		}
		_cv.notify_one();
	});
}

void Capture::video_sink(const FrameBuffer &_buffer, const VideoModeInfo &_mode,
	const VideoTimings &_timings)
{
	// called by the Machine thread
	m_video_frames.push(VideoFrame(_buffer, _mode, _timings));
}

void Capture::audio_sink(const std::vector<int16_t> &_data, int _category)
{
	// called bu the Mixer thread
	if(_category == ec_to_i(MixerChannelCategory::AUDIO)) {
		size_t data_len = _data.size() * 2;
		size_t written = m_audio_buffer.write((uint8_t*)(&_data[0]), data_len);
		if(written < data_len) {
			PDEBUGF(LOG_V0, LOG_GUI, "Capture: audio buffer overrun: lost data: %d bytes\n", data_len-written);
		}
	}
}

void Capture::start_capture()
{
	assert(!m_recording);

	PDEBUGF(LOG_V1, LOG_GUI, "Capture: starting recording\n");
	
	std::string destdir;
	try {
		destdir = g_program.config().find_file(CAPTURE_SECTION, CAPTURE_DIR);
	} catch(std::exception &e) {
		PERRF(LOG_GUI, "Capture: cannot find the destination directory\n");
		throw;
	}
	if(destdir.empty()) {
		PERRF(LOG_GUI, "Capture: cannot find the destination directory\n");
		throw std::exception();
	}
	
	// enum classes are a PITA!
	static std::map<std::string, unsigned> modes = {
		{ "",    ec_to_i(CaptureMode::AVI) },
		{ "png", ec_to_i(CaptureMode::PNG) },
		{ "jpg", ec_to_i(CaptureMode::JPG) },
		{ "avi", ec_to_i(CaptureMode::AVI) },
	};
	CaptureMode mode = static_cast<CaptureMode>(
		g_program.config().get_enum(CAPTURE_SECTION, CAPTURE_VIDEO_MODE, modes, ec_to_i(CaptureMode::AVI))
	);
	
	int video_quality = g_program.config().get_int(CAPTURE_SECTION, CAPTURE_VIDEO_QUALITY, 80);
	video_quality = clamp(video_quality, 1, 100);
	
	switch(mode) {
		case CaptureMode::PNG:
		case CaptureMode::JPG:
		{
			m_rec_target = std::make_unique<CaptureImgSeq>(mode, video_quality);
			break;
		}
		case CaptureMode::AVI:
		{
			static std::map<std::string, unsigned> encoders = {
				{ "",     AVI_VIDEO_ZMBV },
				{ "zmbv", AVI_VIDEO_ZMBV },
				{ "mpng", AVI_VIDEO_MPNG },
				{ "bmp",  AVI_VIDEO_BMP  }
			};
			unsigned video_encoder =
				g_program.config().get_enum(CAPTURE_SECTION, CAPTURE_VIDEO_FORMAT, encoders, AVI_VIDEO_ZMBV);
			
			SDL_AudioSpec spec = m_mixer->get_audio_spec();
			if(SDL_AUDIO_BITSIZE(spec.format) != 16) {
				PERRF(LOG_GUI, "Unsupported audio bit depth\n");
				throw std::exception();
			}
			
			m_rec_target = std::make_unique<CaptureVideoFile>(
					video_encoder,
					video_quality,
					AVI_AUDIO_PCM,
					0,
					spec.channels,
					spec.freq
			);
			
			m_audio_buffer.set_size(spec.freq * spec.channels * 2);
			break;
		}
		default:
			PDEBUGF(LOG_V0, LOG_GUI, "Capture: invalid recording mode\n");
			throw std::exception();
	}
	
	std::string dest = m_rec_target->open(destdir); // can throw
	
	m_vga_display->enable_buffering(true);
	
	try {
		m_video_sink = m_vga_display->register_sink(
			std::bind(&Capture::video_sink, this, 
				std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)
		);

		if(m_rec_target->has_audio()) {
			m_audio_sink = m_mixer->register_sink(
				std::bind(&Capture::audio_sink, this, 
					std::placeholders::_1, std::placeholders::_2)
			);
		}
		
	} catch(std::exception &) {
		m_rec_target->close();
		throw;
	}
	
	m_recording = true;
	
	std::string mex = "Started video recording to " + dest;
	PINFOF(LOG_V0, LOG_GUI, "%s\n", mex.c_str());
	GUI::instance()->show_message(mex.c_str());
}

void Capture::stop_capture()
{
	assert(m_recording);
	
	m_rec_target->close();
	m_rec_target.reset(nullptr);
	
	m_vga_display->unregister_sink(m_video_sink);
	m_video_sink = -1;
	m_video_frames.clear();
	
	m_mixer->unregister_sink(m_audio_sink);
	m_audio_sink = -1;
	m_audio_buffer.clear();
	
	m_recording = false;
	
	std::string mex = "Video recording stopped";
	PINFOF(LOG_V0, LOG_GUI, "%s\n", mex.c_str());
	GUI::instance()->show_message(mex.c_str());
}
