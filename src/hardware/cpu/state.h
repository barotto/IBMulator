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

#ifndef IBMULATOR_CPU_STATE_H
#define IBMULATOR_CPU_STATE_H

enum CPUActivityState {
	CPU_STATE_ACTIVE = 0,
	CPU_STATE_HALT,
	CPU_STATE_SHUTDOWN,
	CPU_STATE_POWEROFF
};

struct CPUState
{
	uint64_t icount;         // instructions count
	uint64_t ccount;         // cycles count
	uint32_t activity_state;
	uint32_t pending_event;
	uint32_t event_mask;
	bool     async_event;
	bool     debug_trap;

	/* What events to inhibit at any given time. Certain instructions
	 * inhibit interrupts, some debug exceptions and single-step traps.
	 */
	unsigned inhibit_mask;
	uint64_t inhibit_icount;

	bool HRQ; // DMA Hold Request
	bool EXT; /* EXT is 1 if an external event (ie., a single step, an
	           * external interrupt, an #MF exception, or an #MP exception)
	           * caused the interrupt; 0 if not (ie., an INT instruction or
	           * other exceptions) (cfr. B-50)
	           */
};

struct CPUCycles
{
	int eu;
	int bu;
	int decode;
	int io;
	int bus;
	int refresh;

	ALWAYS_INLINE
	inline int sum() const {
		return eu + bu + decode + io + bus + refresh;
	}
};

#endif
