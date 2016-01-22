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

#ifndef IBMULATOR_GUI_DEVSTATUS_H
#define IBMULATOR_GUI_DEVSTATUS_H

#include <Rocket/Core/EventListener.h>

class Machine;
class GUI;

class DevStatus : public Window
{
private:

	struct {
		Rocket::Core::Element *irq_e[16], *irr_e[16], *imr_e[16], *isr_e[16];
		uint16_t irq, irr, imr, isr;
	} m_pic;

	struct {
		Rocket::Core::Element *mode[3], *cnt[3], *gate[3], *out[3], *in[3];
	} m_pit;
	void update_pic(uint16_t _irq, uint16_t _irr, uint16_t _imr, uint16_t _isr, uint _irqn);
	void update_pit(uint cnt);

public:

	DevStatus(GUI * _gui);
	~DevStatus();

	void update();

	void ProcessEvent(Rocket::Core::Event & event);
};

#endif
