/*
 * Copyright (C) 2015-2020  Marco Bortolin
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
#include "gui.h"
#include "machine.h"
#include "mixer.h"
#include "stats.h"
#include "hardware/memory.h"
#include "hardware/devices/cmos.h"

#include <Rocket/Core.h>
#include <sstream>
#include <iomanip>

event_map_t Stats::ms_evt_map = {
	GUI_EVT( "close", "click", DebugTools::DebugWindow::on_close )
};

Stats::Stats(Machine *_machine, GUI * _gui, Mixer *_mixer, RC::Element *_button)
:
DebugTools::DebugWindow(_gui, "stats.rml", _button)
{
	assert(m_wnd);
	m_stats.fps = get_element("FPS");

	m_machine = _machine;
	m_stats.machine = get_element("machine");

	m_mixer = _mixer;
	m_stats.mixer = get_element("mixer");
}

Stats::~Stats()
{
}

void Stats::update()
{
	if(!m_enabled) {
		return;
	}
	std::stringstream ss;
	print(ss, g_program.get_bench());

	m_stats.fps->SetInnerRML(ss.str().c_str());

	ss.str("");
	
	HWBench &hwb = m_machine->get_bench();
	print(ss, hwb);

	//read the DOS clock from MEM 0040h:006Ch
	uint32_t ticks = g_memory.dbg_read_dword(0x0400 + 0x006C);
	uint hour      = ticks / 65543;
	uint remainder = ticks % 65543;
	uint minute    = remainder / 1092;
	remainder      = remainder % 1092;
	uint second    = remainder / 18.21;
	uint hundredths = fmod(double(remainder),18.21) * 100;
	ss << "DOS clock: " << hour << ":" << minute << ":" << second << "." << hundredths;
	ss << "<br />";
	CMOS *cmos = m_machine->devices().cmos();
	hour = cmos->get_reg(4);
	hour = ((hour>>4)*10) + (hour&0xF);
	minute = cmos->get_reg(2);
	minute = ((minute>>4)*10) + (minute&0xF);
	second = cmos->get_reg(0);
	second = ((second>>4)*10) + (second&0xF);
	ss << "RTC clock: " << hour << ":" << minute << ":" << second;

	m_stats.machine->SetInnerRML(ss.str().c_str());

	HWBench &mixb = m_mixer->get_bench();
	ss.str("");
	//ss << std::fixed << std::setprecision( 3 );
	ss << "Mode: " <<
			m_mixer->get_audio_spec().freq << " Hz, " <<
			SDL_AUDIO_BITSIZE(m_mixer->get_audio_spec().format) << " bit, " << 
			(m_mixer->get_audio_spec().channels==1?"mono":"stereo") << 
			"<br />";
	
	ss << "Curr. FPS: " << mixb.avg_fps << "<br />";
	ss << "Status: ";
	switch(m_mixer->get_audio_status()) {
		case SDL_AUDIO_STOPPED: ss << "stopped"; break;
		case SDL_AUDIO_PLAYING: ss << "playing"; break;
		case SDL_AUDIO_PAUSED: ss << "paused"; break;
		default: ss << "unknown!"; break;
	}
	ss << "<br />";
	ss << "Buffer size: " << m_mixer->get_buffer_read_avail() << "<br />";
	ss << "Delay (us): " << m_mixer->get_buffer_len() << "<br />";
	m_stats.mixer->SetInnerRML(ss.str().c_str());
}

static const std::string endline = "<br />";

void Stats::print(std::ostream &_os, const Bench &_bench)
{
	_os << std::fixed;
	_os.precision(6);
	_os << "Time (s): " << (_bench.time_elapsed / 1e9) << endline;
	_os << "Target FPS: " << (1.0e9 / _bench.heartbeat) << endline;
	_os << "Curr. FPS: " << _bench.avg_fps << endline; 
	_os << "Target Frame time (ms): " << (_bench.heartbeat / 1e6) << endline;
	_os << "-- curr. time: " << (_bench.frame_time / 1e6) << endline;
	_os.precision(3);
	_os << "-- min/avg/max: " << 
			(_bench.min_frame_time / 1e6) << "/" <<
			(_bench.avg_frame_time / 1e6) << "/" <<
			(_bench.max_frame_time / 1e6) << endline;
	_os.precision(6);
	_os << "-- std. dev: " << (_bench.std_frame_time / 1e6) << endline;
	_os << "-- render time: " << (_bench.load_time / 1e6) << endline;
	_os.precision(3);
	_os << "-- min/avg/max: " << 
			(_bench.min_load_time / 1e6) << "/" <<
			(_bench.avg_load_time / 1e6) << "/" <<
			(_bench.max_load_time / 1e6) << endline;
	_os.precision(6);
	_os << "Load: " << _bench.load << endline;
}

void Stats::print(std::ostream &_os, const HWBench &_bench)
{
	_os << std::fixed;
	_os.precision(6);
	_os << "Time (s): " << (_bench.time_elapsed / 1e9) << endline;
	_os << "Target FPS: " << (1.0e9 / _bench.heartbeat) << endline;
	_os << "Target Frame time (ms): " << (_bench.heartbeat / 1e6) << endline;
	_os << "-- curr. time: " << (_bench.frame_time / 1e6) << endline;
	_os.precision(3);
	_os << "-- min/avg/max: " << 
			(_bench.min_frame_time / 1e6) << "/" <<
			(_bench.avg_frame_time / 1e6) << "/" <<
			(_bench.max_frame_time / 1e6) << endline;
	_os.precision(6);
	_os << "-- std. dev: " << (_bench.std_frame_time / 1e6) << endline;
	_os << "-- sim. time: " << (_bench.load_time / 1e6) << endline;
	_os.precision(3);
	_os << "-- min/avg/max: " << 
			(_bench.min_load_time / 1e6) << "/" <<
			(_bench.avg_load_time / 1e6) << "/" <<
			(_bench.max_load_time / 1e6) << endline;
	_os.precision(6);
	_os << "Host load: " << _bench.load << endline;
	_os << "Late frames: " << _bench.late_frames << endline;

	double mhz = _bench.avg_cps / 1e6;
	double mips = _bench.avg_ips / 1e6;
	_os.precision(8);
	_os << "CPU MHz: " << mhz << endline;
	_os << "CPU MIPS: " << mips << endline;
	
	uint64_t vtime = m_machine->get_virt_time_ns_mt();
	_os << "CPU clock (ns): " <<  vtime << "<br />";
	int64_t vdiff = _bench.time_elapsed - int64_t(vtime);
	_os << "CPU clock diff: " << int64_t(vdiff/1.0e6) << "<br />";
}