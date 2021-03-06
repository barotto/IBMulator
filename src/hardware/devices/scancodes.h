/*
 * Copyright (c) 2002-2009  The Bochs Project
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

#ifndef IBMULATOR_SCANCODES_H
#define IBMULATOR_SCANCODES_H

#include "keys.h"

// Translation table of the 8042
extern unsigned char g_translation8042[256];

struct scancode
{
	const char *make;
	const char *brek;
};

// Scancodes table
extern const scancode g_scancodes[KEY_NBKEYS][3];

#endif
