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

#ifndef IBMULATOR_GUI_STATUS_H
#define IBMULATOR_GUI_STATUS_H

#include <RmlUi/Core/EventListener.h>

class GUI;
class FloppyCtrl;
class StorageCtrl;
class Serial;


class Status final : public Window
{
public:
	enum IND {
		PWR, FLP_A, FLP_B, HDD, CDROM, NET, AUDREC, VIDREC,
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

	// TODO encapsulate all this into an LED class?
	TimerID m_cdrom_led_timer = NULL_TIMER_ID;
	int64_t m_cdrom_led_activity = 0;
	bool m_cdrom_led_on = false;
	std::mutex m_cdrom_mutex;
	void cdrom_activity_cb(CdRomEvents::EventType _what, uint64_t _duration);
	void cdrom_led_timer(uint64_t);

public:
	Status(GUI * _gui, Machine *_machine);

	void update() override;
	void config_changed(bool) override;
	bool would_handle(Rml::Input::KeyIdentifier, int) override { return false; }

	void set_indicator(IND _ind, LED _s) {
		m_indicators[_ind].set(_s);
	}

	void ProcessEvent(Rml::Event &) {}

protected:
	void create() override;
};


#endif
