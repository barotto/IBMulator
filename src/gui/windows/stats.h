/*
 * Copyright (C) 2015-2025  Marco Bortolin
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

#ifndef IBMULATOR_GUI_STATS_H
#define IBMULATOR_GUI_STATS_H

#include "debugtools.h"

class Machine;
class GUI;
class Mixer;


class Stats final : public DebugTools::DebugWindow
{
private:
	struct {
		Rml::Element *fps, *machine, *mixer;
	} m_stats = {};

	Machine * m_machine;
	Mixer * m_mixer;

	static event_map_t ms_evt_map;

	void on_cmd_reset(Rml::Event &);
	
public:
	Stats(GUI * _gui, Machine *_machine, Mixer *_mixer, Rml::Element *_button);

	void update() override;

protected:
	void create() override;
	event_map_t & get_event_map() override { return Stats::ms_evt_map; }

private:
	void print(std::ostream &_os, const Bench &_bench);
	void print(std::ostream &_os, const HWBench &_bench);
};


#endif
