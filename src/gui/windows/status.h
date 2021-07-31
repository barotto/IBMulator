/*
 * Copyright (C) 2015-2021  Marco Bortolin
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

#ifndef IBMULATOR_GUI_STATUS_H
#define IBMULATOR_GUI_STATUS_H

#include <Rocket/Core/EventListener.h>

class GUI;
class FloppyCtrl;
class StorageCtrl;
class Serial;

class Status : public Window
{
public:
	enum IND {
		PWR, FLP_A, FLP_B, HDD, NET, AUDREC, VIDREC,
		IND_CNT
	};
	enum class LED : int {
		HIDDEN, IDLE, ACTIVE, ERROR, UNSET
	};
private:
	struct Indicator {
		static constexpr const char* cls[] = {
			"hidden", "idle", "active", "error"
		};
		Rocket::Core::Element *el = nullptr;
		LED status = LED::UNSET;

		bool is(LED _s) const { return status == _s; }
		void set(LED _s);
	};

	Indicator m_indicators[IND_CNT];

	Machine *m_machine;
	const FloppyCtrl *m_floppy;
	const StorageCtrl *m_hdd;
	const Serial *m_serial;

public:
	Status(GUI * _gui, Machine *_machine);
	~Status();

	void update();
	void config_changed();
	void set_indicator(IND _ind, LED _s) {
		m_indicators[_ind].set(_s);
	}

	void ProcessEvent(Rocket::Core::Event & event);
};

#endif
