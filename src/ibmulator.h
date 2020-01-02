/*
 * Copyright (C) 2015-2020  Marco Bortolin
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

#define DEFAULT_HEARTBEAT    16683333
#define CHRONO_RDTSC         false
#define USE_PREFETCH_QUEUE   true
#define PIT_CNT1_AUTO_UPDATE false


/*
 * For CPU logging options see hardware/cpu/logger.h
 */

#define PATHNAME_LEN 512

#define N_SERIAL_PORTS 1 // DONT'T TOUCH IT! IT MUST BE 1
#define N_PARALLEL_PORTS 1
#define USE_RAW_SERIAL 0
#define TRUE_CTLC 0

#include "config.h"
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <sys/types.h>
#include <cstdio>
#include <stdexcept>
#include <cassert>

#if !defined(_MSC_VER)
	#define GCC_ATTRIBUTE(x) __attribute__ ((x))
	#define LIKELY(x)    __builtin_expect((x),1)
	#define UNLIKELY(x)  __builtin_expect((x),0)
	#define ALWAYS_INLINE GCC_ATTRIBUTE(always_inline)
#else
	#define GCC_ATTRIBUTE(x)
	#define LIKELY(x)   (x)
	#define UNLIKELY(x) (x)
	#define ALWAYS_INLINE inline
#endif

#if defined(_MSC_VER) && (_MSC_VER>=1300)
	#define MSC_ALIGN(x) __declspec(align(x))
#else
	#define MSC_ALIGN(x)
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
	#define UD6_AUTO_DUMP     false  // automatic memory dump at #UD exception
	#define BOCHS_BIOS_COMPAT false  // enable legacy Bochs' BIOS compatibility

	#define LOG_DEBUG_MESSAGES    true   // enable debug messages logging
	#define LOG_MACHINE_TIME      true   // enable machine time logging
	#define LOG_MACHINE_TIME_NS   true   // enable nanosecond time logging
	#define LOG_CSIP              true   // enable CS:eIP logging
	#define DEFAULT_LOG_VERBOSITY LOG_V0 // default logger verbosity level

	#define SHOW_CURRENT_PROGRAM_NAME true // enable running DOS program name visualization
	#define ENABLE_VGA_SCREEN_RECORDING true // enable CTRL+F2 command for VGA frame recording

#else
	//RELEASE
	#define CONFIG_PARSE      true
	#define MEMORY_TRAPS      false
	#define INT_TRAPS         false
	#define INT1_PAUSE        false
	#define STOP_AT_MEM_TRAPS false
	#define STOP_AT_EXC       false
	#define STOP_AT_EXC_VEC   0
	#define UD6_AUTO_DUMP     false
	#define BOCHS_BIOS_COMPAT false

	#define LOG_DEBUG_MESSAGES    false
	#define LOG_MACHINE_TIME      false
	#define LOG_MACHINE_TIME_NS   false
	#define LOG_CSIP              false
	#define DEFAULT_LOG_VERBOSITY LOG_V0

	#define SHOW_CURRENT_PROGRAM_NAME false
	#define ENABLE_VGA_SCREEN_RECORDING false

#endif

#define OVERRIDE_VERBOSITY_LEVEL false
#define LOG_PROGRAM_VERBOSITY  LOG_V0
#define LOG_FS_VERBOSITY       LOG_V0
#define LOG_GFX_VERBOSITY      LOG_V0
#define LOG_INPUT_VERBOSITY    LOG_V0
#define LOG_GUI_VERBOSITY      LOG_V0
#define LOG_MACHINE_VERBOSITY  LOG_V0
#define LOG_MIXER_VERBOSITY    LOG_V0
#define LOG_MEM_VERBOSITY      LOG_V0
#define LOG_CPU_VERBOSITY      LOG_V0
#define LOG_MMU_VERBOSITY      LOG_V0
#define LOG_PIT_VERBOSITY      LOG_V0
#define LOG_PIC_VERBOSITY      LOG_V0
#define LOG_DMA_VERBOSITY      LOG_V0
#define LOG_KEYB_VERBOSITY     LOG_V0
#define LOG_VGA_VERBOSITY      LOG_V0
#define LOG_CMOS_VERBOSITY     LOG_V0
#define LOG_FDC_VERBOSITY      LOG_V0
#define LOG_HDD_VERBOSITY      LOG_V0
#define LOG_AUDIO_VERBOSITY    LOG_V0
#define LOG_LPT_VERBOSITY      LOG_V0
#define LOG_COM_VERBOSITY      LOG_V0

#include "syslog.h"




#endif
