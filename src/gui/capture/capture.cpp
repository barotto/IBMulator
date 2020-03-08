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
#include "gui/gui.h"

Capture::Capture(VGADisplay *_vgadisp)
:
m_quit(false),
m_recording(false),
m_vga_display(_vgadisp),
m_video_sink(-1)
{
}

Capture::~Capture()
{
}

void Capture::init()
{
	m_pacer.start();
}

void Capture::calibrate(const Pacer &_p)
{
	m_pacer.calibrate(_p);
}

void Capture::start()
{
	m_quit = false;

	PDEBUGF(LOG_V1, LOG_GUI, "Capture: thread started\n");
	main_loop();
}

void Capture::main_loop()
{
	while(true) {
		Capture_fun_t fn;
		m_cmd_queue.wait_and_pop(fn);
		fn();
		
		if(m_quit) {
			return;
		}
	}
}

void Capture::capture_loop()
{
	while(true) {
		Capture_fun_t fn;
		while(m_cmd_queue.try_and_pop(fn)) {
			fn();
		}
		if(!m_recording) {
			return;
		}
		if(g_machine.is_on() && !g_machine.is_paused()) {
			try {
				VideoFrame frame;
				auto result = m_frames.wait_for_and_pop(frame, g_program.heartbeat() * 2);
				if(result == std::cv_status::no_timeout) {
					m_rec_target->push_video_frame(frame.buffer, frame.mode);
				}
			} catch(std::exception &) {
				stop_capture();
				return;
			}
		} else {
			m_pacer.wait();
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
	//this signal should be preceded by a pause command
	m_cmd_queue.push([&] () {
		//PDEBUGF(LOG_V2, LOG_GUI, "Capture: updating configuration\n");
		std::unique_lock<std::mutex> lock(_mutex);
		if(m_recording) {
			stop_capture();
		}
		_cv.notify_one();
	});
}

void Capture::video_sink(const FrameBuffer &_buffer, const VideoModeInfo &_mode,
	const VideoTimings &_timings)
{
	// called by the Machine thread
	m_frames.push(VideoFrame(_buffer, _mode, _timings));
}

void Capture::audio_sink(const std::vector<int16_t> &_data, int _category)
{
	// TODO
	UNUSED(_data);
	UNUSED(_category);
}

void Capture::start_capture()
{
	assert(!m_recording);

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
	
	static std::map<std::string, unsigned> formats = {
		{ "",    ec_to_i(CaptureFormat::PNG) },
		{ "png", ec_to_i(CaptureFormat::PNG) },
		{ "jpg", ec_to_i(CaptureFormat::JPG) }
	};
	CaptureFormat format = static_cast<CaptureFormat>(
		g_program.config().get_enum(CAPTURE_SECTION, CAPTURE_VIDEO_FORMAT, formats, ec_to_i(CaptureFormat::PNG))
	);
	int quality = g_program.config().get_int(CAPTURE_SECTION, CAPTURE_VIDEO_QUALITY, 80);
	quality = clamp(quality, 1, 100);
	
	switch(format) {
		case CaptureFormat::PNG:
		case CaptureFormat::JPG:
		{
			m_rec_target = std::make_unique<CaptureImgSeq>(format, quality);
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
	} catch(std::exception &) {
		m_rec_target->close();
		throw;
	}
	
	m_recording = true;
	
	std::string mex = "Started video recording in " + dest;
	PINFOF(LOG_V0, LOG_GUI, "%s\n", mex.c_str());
	GUI::instance()->show_message(mex.c_str());
	
	capture_loop();
}

void Capture::stop_capture()
{
	assert(m_recording);
	
	m_rec_target->close();
	m_rec_target.reset(nullptr);
	m_vga_display->unregister_sink(m_video_sink);
	m_video_sink = -1;
	m_frames.clear();
	
	m_recording = false;
	
	std::string mex = "Video recording stopped";
	PINFOF(LOG_V0, LOG_GUI, "%s\n", mex.c_str());
	GUI::instance()->show_message(mex.c_str());
}
