/*
 * Copyright (C) 2024  Marco Bortolin
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

#ifndef IBMULATOR_HW_CDROM_EVENTS_H
#define IBMULATOR_HW_CDROM_EVENTS_H

#include <functional>

class CdRomEvents
{
public:
	enum EventType {
		MEDIUM,         // GUI should update (low priority)
		MEDIUM_LOADING, // GUI should update (low priority)
		DOOR_OPENING,   // door is opening (play fx, LED blinking)
		DOOR_CLOSING,   // door is closing (play fx, LED blinking)
		SPINNING_UP,    // disc is spinning up (LED blinking)
		READ_DATA,      // data is being read (LED blinking)
		POWER_OFF       // unit is turned off (LED off)
	};

	using ActivityCbFn = std::function<void(EventType what, uint64_t led_duration)>;
};

#endif

