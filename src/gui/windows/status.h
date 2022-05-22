/*
 * Copyright (C) 2015-2022  Marco Bortolin
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

#include <RmlUi/Core/EventListener.h>

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
		HIDDEN, IDLE, ACTIVE, ATTN, INVALID
	};
private:
	struct Indicator {
		static constexpr const char* cls[] = {
			"hidden", "idle", "active", "attn"
		};
		Rml::Element *el = nullptr;
		LED status = LED::INVALID;

		bool is(LED _s) const { return status == _s; }
		void set(LED _s);
	};

	Indicator m_indicators[IND_CNT];

	Machine *m_machine;
	const FloppyCtrl *m_floppy = nullptr;
	const StorageCtrl *m_hdd = nullptr;
	const Serial *m_serial = nullptr;

public:
	Status(GUI * _gui, Machine *_machine);
	~Status();

	virtual void create();
	virtual void update();
	virtual void config_changed(bool);

	void set_indicator(IND _ind, LED _s) {
		m_indicators[_ind].set(_s);
	}

	void ProcessEvent(Rml::Event &) {}
};

#endif
