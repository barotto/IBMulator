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

#ifndef IBMULATOR_H
#define IBMULATOR_H

#define IBMULATOR_STATE_VERSION 2

#define DEFAULT_HEARTBEAT    16683333
#define CHRONO_RDTSC         false

// For CPU logging options see hardware/cpu/logger.h

#include "config.h"
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <sys/types.h>
#include <cstdio>
#include <stdexcept>

#if defined(__GNUC__)
	// both gcc and clang define __GNUC__
	#define GCC_ATTRIBUTE(x) __attribute__ ((x))
	#define LIKELY(x)    __builtin_expect((x),1)
	#define UNLIKELY(x)  __builtin_expect((x),0)
	#define ALWAYS_INLINE GCC_ATTRIBUTE(always_inline)
#else
	#error unsupported compiler
#endif

#define UNUSED(x) ((void)x)

#ifndef NDEBUG
	//DEBUG
	#define CONFIG_PARSE      true   // enable ini file parsing
	#define MEMORY_TRAPS      true   // enable memory traps
	#define INT_TRAPS         true   // enable interrupt traps
	#define INT1_PAUSE        true   // pause emulation at INT 1
	#define STOP_AT_MEM_TRAPS false  // pause emulation at memory traps hit
	#define STOP_AT_EXC       false  // pause emulation at exception defined in STOP_AT_EXC_VEC
	#define STOP_AT_EXC_VEC   0x3000 // bitmask of exceptions to pause at
	#define STOP_AT_POST_CODE 0      // POST code to pause emulation at. 0 to disable.
	#define UD6_AUTO_DUMP     false  // automatic memory dump at #UD exception
	#define BOCHS_BIOS_COMPAT false  // enable legacy Bochs' BIOS compatibility

	#define LOG_DEBUG_MESSAGES    true   // enable debug messages logging
	#define LOG_MACHINE_TIME      true   // enable machine time logging
	#define LOG_MACHINE_TIME_NS   true   // enable nanosecond time logging
	#define LOG_CSIP              true   // enable CS:eIP logging
	#define DEFAULT_LOG_VERBOSITY LOG_V0 // default logger verbosity level

	#define SHOW_CURRENT_PROGRAM_NAME true // enable running DOS program name visualization
	
	#include <cassert>
#else
	//RELEASE
	#define CONFIG_PARSE      true
	#define MEMORY_TRAPS      false
	#define INT_TRAPS         false
	#define INT1_PAUSE        false
	#define STOP_AT_MEM_TRAPS false
	#define STOP_AT_EXC       false
	#define STOP_AT_EXC_VEC   0
	#define STOP_AT_POST_CODE 0
	#define UD6_AUTO_DUMP     false
	#define BOCHS_BIOS_COMPAT false

	#define LOG_DEBUG_MESSAGES    false
	#define LOG_MACHINE_TIME      false
	#define LOG_MACHINE_TIME_NS   false
	#define LOG_CSIP              false
	#define DEFAULT_LOG_VERBOSITY LOG_V0

	#define SHOW_CURRENT_PROGRAM_NAME false

	#define ENABLE_ASSERTS false // enable the assert macro

	#if ENABLE_ASSERTS
		#undef NDEBUG
		#include <cassert>
		#define NDEBUG
	#else
		#include <cassert>
	#endif
#endif


#include "syslog.h"


#endif
