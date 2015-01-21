/*
 * 	Copyright (c) 2015  Marco Bortolin
 *
 *	This file is part of IBMulator
 *
 *  IBMulator is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 3 of the License, or
 *	(at your option) any later version.
 *
 *	IBMulator is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with IBMulator.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "ibmulator.h"
#include "program.h"
#include "gui.h"
#include "machine.h"

#include <Rocket/Core.h>
#include <sstream>

#include "hardware/devices/floppy.h"

Status::Status(GUI * _gui)
:
Window(_gui, "status.rml")
{
	ASSERT(m_wnd);
	m_status.power_led = get_element("power_led");
	m_status.floppy_a_led = get_element("floppy_a_led");
	m_status.floppy_b_led = get_element("floppy_b_led");
	m_status.hdd_led = get_element("hdd_led");
	m_leds.power = false;
	m_leds.floppy_a = false;
	m_leds.floppy_b = false;
	m_leds.hdd = false;
}

Status::~Status()
{
}

void Status::update()
{
	bool motor;

	motor = g_floppy.get_motor_enable(0);
	if(motor && m_leds.floppy_a==false) {
		m_leds.floppy_a = true;
		m_status.floppy_a_led->SetClass("led_active", true);
	} else if(!motor && m_leds.floppy_a==true) {
		m_leds.floppy_a = false;
		m_status.floppy_a_led->SetClass("led_active", false);
	}
	motor = g_floppy.get_motor_enable(1);
	if(motor && m_leds.floppy_b==false) {
		m_leds.floppy_b = true;
		m_status.floppy_b_led->SetClass("led_active", true);
	} else if(!motor && m_leds.floppy_b==true) {
		m_leds.floppy_b = false;
		m_status.floppy_b_led->SetClass("led_active", false);
	}

	if(g_machine.is_on() && m_leds.power==false) {
		m_leds.power = true;
		m_status.power_led->SetClass("led_active", true);
	} else if(!g_machine.is_on() && m_leds.power==true) {
		m_leds.power = false;
		m_status.power_led->SetClass("led_active", false);
	}
}

void Status::ProcessEvent(Rocket::Core::Event &)
{
	//Rocket::Core::Element * el = event.GetTargetElement();
}
