/*
 * Copyright (C) 2015, 2016  Marco Bortolin
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



Stats::Stats(Machine *_machine, GUI * _gui, Mixer *_mixer)
:
Window(_gui, "stats.rml")
{
	assert(m_wnd);
	m_stats.fps = get_element("FPS");
	g_program.get_bench().endl = "<br />";

	m_machine = _machine;
	m_stats.machine = get_element("machine");
	m_machine->get_bench().endl = "<br />";

	m_mixer = _mixer;
	m_stats.mixer = get_element("mixer");
}

Stats::~Stats()
{
}


void Stats::update()
{
	std::stringstream ss;
	ss << g_program.get_bench();
	m_stats.fps->SetInnerRML(ss.str().c_str());

	ss.str("");
	uint64_t vtime = m_machine->get_virt_time_ns();
	ss << "vtime: " <<  vtime << "<br />";
	HWBench &hwb = m_machine->get_bench();
	int64_t vdiff = int64_t(hwb.time_elapsed) - int64_t(vtime)/1000;
	ss << "vdiff: " << int64_t(vdiff/1.0e3) << "<br />";
	ss << hwb;

	//read the DOS clock from MEM 0040h:006Ch
	uint32_t ticks = g_memory.read_dword_notraps(0x0400 + 0x006C);
	uint hour      = ticks / 65543;
	uint remainder = ticks % 65543;
	uint minute   = remainder / 1092;
	remainder = remainder % 1092;
	uint second    = remainder / 18.21;
	uint hundredths = fmod(double(remainder),18.21) * 100;
	ss << "DOS clock: " << hour << ":" << minute << ":" << second << "." << hundredths;
	ss << "<br />";
	hour = g_cmos.get_reg(4);
	hour = ((hour>>4)*10) + (hour&0xF);
	minute = g_cmos.get_reg(2);
	minute = ((minute>>4)*10) + (minute&0xF);
	second = g_cmos.get_reg(0);
	second = ((second>>4)*10) + (second&0xF);
	ss << "RTC clock: " << hour << ":" << minute << ":" << second;

	m_stats.machine->SetInnerRML(ss.str().c_str());

	HWBench &mixb = m_mixer->get_bench();
	ss.str("");
	ss << "avg bps: " << mixb.avg_bps << "<br />";
	ss << "beats: " << mixb.beat_count << "<br />";
	ss << "status: " << m_mixer->get_audio_status() << "<br />";
	ss << "buffer: " << m_mixer->get_buffer_read_avail() << "<br />";
	ss << "delay: " << m_mixer->get_buffer_len() << "<br />";
	m_stats.mixer->SetInnerRML(ss.str().c_str());
}

void Stats::ProcessEvent(Rocket::Core::Event &)
{
	//Rocket::Core::Element * el = event.GetTargetElement();
}
