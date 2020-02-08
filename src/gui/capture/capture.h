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

#ifndef IBMULATOR_CAPTURE_H
#define IBMULATOR_CAPTURE_H

#include "mixer.h"
#include "shared_queue.h"
#include "pacer.h"
#include "capture_target.h"

typedef std::function<void()> Capture_fun_t;

enum class CaptureMode {
	NONE,
	IMGSEQ,
	VIDEOFILE
};
enum class CaptureFormat {
	NONE,
	PNG,
	JPG,
	ZMBV
};

class Capture
{
private:
	Pacer m_pacer;
	bool m_quit;
	bool m_recording;
	std::unique_ptr<CaptureTarget> m_rec_target;
	shared_queue<Capture_fun_t> m_cmd_queue;
	VGADisplay *m_vga_display;
	std::shared_ptr<MixerChannel> m_silence_channel;
	
	void main_loop();
	void capture_loop();
	bool create_silence_samples(uint64_t _time_span_us, bool _prebuf, bool _first_upd);
	
public:
	Capture(VGADisplay *_vgadisp);
	~Capture();

	void init();
	void calibrate(const Pacer &_c);
	void start();
	void set_heartbeat(int64_t _ns);

	void sig_config_changed(std::mutex &_mutex, std::condition_variable &_cv);
	void cmd_quit();
	void cmd_start_capture();
	void cmd_stop_capture();
	void cmd_toggle_capture();

private:
	void start_capture();
	void stop_capture();

};

#endif