/*
 * Copyright (C) 2024  Marco Bortolin
 *
 * This file is part of IBMulator
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
#ifndef IBMULATOR_HW_FLOPPYEVENTS_H
#define IBMULATOR_HW_FLOPPYEVENTS_H

#include <functional>

class FloppyEvents
{
public:
	enum EventType {
		EVENT_MEDIUM,
		EVENT_DISK_LOADING,
		EVENT_DISK_SAVING,
		EVENT_DISK_INSERTED,
		EVENT_DISK_EJECTED,
		EVENT_MOTOR_ON,
		EVENT_MOTOR_OFF
	};
	using ActivityCbFn = std::function<void(EventType,uint8_t)>;
};

#endif